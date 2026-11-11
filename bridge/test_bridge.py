import concurrent.futures
from contextlib import closing
import http.client
import threading
import time
import unittest
from unittest.mock import Mock, patch
from http.server import ThreadingHTTPServer

import serial

from bridge import PressSound, SerialBridge, log_serial_lines, make_handler


class PressSoundTests(unittest.TestCase):
    def setUp(self):
        player = patch("bridge.shutil.which", return_value="/usr/bin/afplay")
        file_check = patch("bridge.Path.is_file", return_value=True)
        launch = patch("bridge.subprocess.Popen")
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
        with patch("bridge.Path.is_file", return_value=False):
            sound = PressSound("missing.aiff")
        sound.handle_line("PRESS child=1 rank=1")
        with patch("bridge.shutil.which", return_value=None):
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


class FakeSerial:
    is_open = True

    def __init__(self):
        self.writes = []
        self.error = None
        self.partial = False
        self.writing = False
        self.overlap = False

    def write(self, data):
        if self.error:
            raise self.error
        if self.writing:
            self.overlap = True
        self.writing = True
        time.sleep(0.005)
        self.writes.append(data)
        self.writing = False
        return 0 if self.partial else len(data)


class HttpBridgeTests(unittest.TestCase):
    def setUp(self):
        self.connection = FakeSerial()
        self.bridge = SerialBridge(self.connection)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.bridge))
        self.worker = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.01})
        self.worker.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.worker.join()

    def request(self, path, method="GET"):
        with closing(http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)) as client:
            client.request(method, path)
            response = client.getresponse()
            body = response.read()
            return response.status, response.getheader("Cache-Control"), body

    def test_actions_send_exactly_one_byte(self):
        for path in ("/reset", "/correct", "/wrong"):
            status, cache, body = self.request(path)
            self.assertEqual(status, 202)
            self.assertEqual(cache, "no-store")
            self.assertIn(b"execution is not confirmed", body)
        self.assertEqual(self.connection.writes, [b"r", b"o", b"x"])

    def test_unknown_urls_and_other_methods_do_not_send(self):
        for path in ("/", "/favicon.ico", "/reset/", "/reset?command=x"):
            status, cache, _ = self.request(path)
            self.assertEqual(status, 404)
            self.assertEqual(cache, "no-store")
        self.assertEqual(self.request("/wrong", "POST")[0], 501)
        self.assertEqual(self.request("/wrong", "HEAD")[0], 501)
        self.assertEqual(self.connection.writes, [])

    def test_unavailable_connection_returns_503(self):
        self.connection.is_open = False
        status, cache, _ = self.request("/reset")
        self.assertEqual(status, 503)
        self.assertEqual(cache, "no-store")
        self.assertEqual(self.connection.writes, [])

    def test_write_failure_is_not_retried(self):
        self.connection.error = serial.SerialTimeoutException("timeout")
        self.assertEqual(self.request("/wrong")[0], 503)
        self.connection.error = None
        self.assertEqual(self.request("/wrong")[0], 503)
        self.assertEqual(self.connection.writes, [])

    def test_partial_write_is_not_accepted(self):
        self.connection.partial = True
        self.assertEqual(self.request("/correct")[0], 503)
        self.assertEqual(self.connection.writes, [b"o"])

    def test_concurrent_requests_serialize_writes(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            responses = list(executor.map(self.request, ["/wrong"] * 8))
        self.assertTrue(all(response[0] == 202 for response in responses))
        self.assertEqual(self.connection.writes, [b"x"] * 8)
        self.assertFalse(self.connection.overlap)


if __name__ == "__main__":
    unittest.main()
