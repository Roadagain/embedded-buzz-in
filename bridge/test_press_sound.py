import threading
import unittest
from unittest.mock import Mock, patch

from press_sound import PressSound
from serial_connection import log_serial_lines


class PressSoundTests(unittest.TestCase):
    def setUp(self):
        player = patch("press_sound.shutil.which", return_value="/usr/bin/afplay")
        file_check = patch("press_sound.Path.is_file", return_value=True)
        launch = patch("press_sound.subprocess.Popen")
        self.addCleanup(player.stop)
        self.addCleanup(file_check.stop)
        self.addCleanup(launch.stop)
        player.start()
        file_check.start()
        self.launch = launch.start()
        self.launch.return_value.poll.return_value = None

    def test_only_first_place_press_plays(self):
        sound = PressSound("sound with spaces.aiff")
        for line in ("BOOT", "RESET", "PRESS child=2 rank=2 elapsed_ms=10",
                     "PRESS child=1 rank=10", "PRESS child=0 rank=1",
                     "JUDGE result=wrong child=1", "ERROR no_answerer"):
            sound.handle_line(line)
        self.launch.assert_not_called()
        sound.handle_line("PRESS child=2 rank=1 elapsed_ms=0")
        self.launch.assert_called_once()
        command = self.launch.call_args.args[0]
        self.assertEqual(command[0], "/usr/bin/afplay")
        self.assertTrue(command[1].endswith("/sound with spaces.aiff"))
        self.launch.return_value.wait.assert_not_called()

    def test_overlapping_presses_are_not_queued(self):
        sound = PressSound("sound.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        sound.handle_line("PRESS child=2 rank=1")
        self.launch.assert_called_once()
        self.launch.return_value.poll.return_value = 0
        sound.handle_line("PRESS child=2 rank=1")
        self.assertEqual(self.launch.call_count, 2)

    def test_sound_is_optional(self):
        PressSound(None).handle_line("PRESS child=1 rank=1")
        self.launch.assert_not_called()

    def test_wrong_answer_plays_for_waiting_child(self):
        sound = PressSound("sound.aiff")
        sound.handle_line("PRESS child=1 rank=1 elapsed_ms=0")
        sound.handle_line("PRESS child=2 rank=2 elapsed_ms=10")
        self.launch.assert_called_once()
        self.launch.return_value.poll.return_value = 0
        sound.handle_line("JUDGE result=wrong child=1")
        self.assertEqual(self.launch.call_count, 2)
        self.assertEqual(sound.ranking, {1: 2})

    def test_promotion_restarts_active_sound_without_waiting(self):
        first, second = Mock(), Mock()
        first.poll.return_value = second.poll.return_value = None
        self.launch.side_effect = [first, second]
        sound = PressSound("sound.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        sound.handle_line("PRESS child=2 rank=2")
        sound.handle_line("JUDGE result=wrong child=1")
        first.terminate.assert_called_once()
        first.wait.assert_not_called()
        self.assertEqual(self.launch.call_count, 2)
        first.poll.return_value = 0
        sound.handle_line("ERROR no_answerer")
        self.assertEqual(sound.retired, [])
        sound.close()
        second.terminate.assert_called_once()
        second.wait.assert_called_once_with(timeout=0.5)

    def test_wrong_without_waiting_child_does_not_play(self):
        sound = PressSound("sound.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        sound.handle_line("JUDGE result=wrong child=1")
        self.launch.assert_called_once()
        self.assertEqual(sound.ranking, {})

    def test_reset_boot_and_correct_clear_waiting_children(self):
        for event in ("RESET", "BOOT", "JUDGE result=correct child=1"):
            with self.subTest(event=event):
                self.launch.reset_mock()
                sound = PressSound("sound.aiff")
                sound.handle_line("PRESS child=1 rank=1")
                sound.handle_line("PRESS child=2 rank=2")
                sound.handle_line(event)
                sound.handle_line("JUDGE result=wrong child=1")
                self.launch.assert_called_once()
                self.assertEqual(sound.ranking, {})

    def test_rejoining_child_can_be_promoted_again(self):
        sound = PressSound("sound.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        sound.handle_line("PRESS child=2 rank=2")
        self.launch.return_value.poll.return_value = 0
        sound.handle_line("JUDGE result=wrong child=1")
        sound.handle_line("PRESS child=1 rank=2")
        sound.handle_line("JUDGE result=wrong child=2")
        self.assertEqual(self.launch.call_count, 3)
        self.assertEqual(sound.ranking, {1: 1})
        sound.handle_line("JUDGE result=wrong child=1")
        self.assertEqual(self.launch.call_count, 3)

    def test_missing_file_or_player_disables_sound(self):
        with patch("press_sound.Path.is_file", return_value=False):
            sound = PressSound("missing.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        with patch("press_sound.shutil.which", return_value=None):
            sound = PressSound("sound.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        self.launch.assert_not_called()

    def test_playback_failures_do_not_stop_serial_reception(self):
        sound = PressSound("sound.aiff")
        self.launch.side_effect = OSError("player unavailable")
        stop = threading.Event()
        chunks = iter([b"PRESS child=1 ra", b"", b"nk=1 elapsed_ms=0\r\nRESET\n"])
        connection = Mock(in_waiting=1024)

        def read(size):
            try:
                return next(chunks)
            except StopIteration:
                stop.set()
                return b""

        connection.read.side_effect = read
        with self.assertLogs("bridge", level="INFO") as logs:
            log_serial_lines(connection, stop, sound.handle_line)
        self.launch.assert_called_once()
        self.assertTrue(any("RX RESET" in line for line in logs.output))
        self.assertTrue(any("could not start" in line for line in logs.output))

    def test_nonzero_exit_is_logged_and_next_press_can_play(self):
        sound = PressSound("sound.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        self.launch.return_value.poll.return_value = 1
        with self.assertLogs("bridge", level="WARNING") as logs:
            sound.handle_line("PRESS child=2 rank=1")
        self.assertTrue(any("playback failed" in line for line in logs.output))
        self.assertEqual(self.launch.call_count, 2)

    def test_shutdown_stops_and_reaps_playback(self):
        sound = PressSound("sound.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        sound.close()
        self.launch.return_value.terminate.assert_called_once()
        self.launch.return_value.wait.assert_called_once_with(timeout=0.5)
        self.assertIsNone(sound.process)

    def test_reset_stops_sound_and_discards_old_ranking(self):
        first, second = Mock(), Mock()
        first.poll.return_value = second.poll.return_value = None
        self.launch.side_effect = [first, second]
        sound = PressSound("sound.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        sound.handle_line("PRESS child=2 rank=2")
        sound.reset()
        first.terminate.assert_called_once()
        first.wait.assert_called_once_with(timeout=0.5)
        self.assertEqual(sound.ranking, {})
        self.assertIsNone(sound.process)
        sound.handle_line("JUDGE result=wrong child=1")
        self.launch.assert_called_once()
        sound.handle_line("PRESS child=2 rank=1")
        self.assertEqual(self.launch.call_count, 2)


if __name__ == "__main__":
    unittest.main()
