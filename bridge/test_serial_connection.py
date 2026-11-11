import concurrent.futures
import queue
import threading
import time
import unittest
from unittest.mock import patch

import serial

from press_sound import PressSound
from serial_connection import SerialBridge
from test_support import FakeSerial


class ReconnectionTests(unittest.TestCase):
    def setUp(self):
        retry = patch("serial_connection.RECONNECT_INTERVAL_SECONDS", 0.01)
        retry.start()
        self.addCleanup(retry.stop)
        self.stop = threading.Event()
        self.results = queue.Queue()
        self.sound = PressSound(None)
        self.bridge = SerialBridge("test-port", self.sound, self.open_connection)
        self.worker = threading.Thread(target=self.bridge.receive, args=(self.stop,))
        self.worker.start()
        self.addCleanup(self.shutdown)

    def open_connection(self, **kwargs):
        try:
            result = self.results.get_nowait()
        except queue.Empty:
            raise serial.SerialException("USB not connected")
        if isinstance(result, Exception):
            raise result
        return result

    def wait_for(self, predicate):
        deadline = time.monotonic() + 3
        while not predicate():
            if time.monotonic() > deadline:
                self.fail("Timed out waiting for serial worker")
            time.sleep(0.005)

    def connect(self):
        connection = FakeSerial()
        self.results.put(connection)
        self.wait_for(lambda: self.bridge.connection is connection)
        return connection

    def shutdown(self):
        self.stop.set()
        self.worker.join(timeout=3)
        self.assertFalse(self.worker.is_alive())

    def test_start_disconnected_and_recover_without_replaying_commands(self):
        self.assertEqual(self.bridge.status()["status"], "disconnected")
        self.assertFalse(self.bridge.send(b"x"))
        self.results.put(serial.SerialException("Still unavailable"))
        connection = self.connect()
        self.assertEqual(self.bridge.status()["status"], "connected")
        self.assertEqual(connection.writes, [])
        self.assertTrue(self.bridge.send(b"r"))
        self.assertEqual(connection.writes, [b"r"])

    def test_read_failure_closes_session_and_does_not_replay(self):
        first = self.connect()
        self.assertTrue(self.bridge.send(b"o"))
        first.incoming.put(serial.SerialException("USB removed"))
        self.assertTrue(first.closed.wait(3))
        self.assertFalse(self.bridge.send(b"x"))
        second = self.connect()
        self.assertEqual(first.writes, [b"o"])
        self.assertEqual(second.writes, [])
        self.assertTrue(self.bridge.send(b"r"))
        self.assertEqual(second.writes, [b"r"])

    def test_write_failure_reconnects_without_retrying_failed_command(self):
        first = self.connect()
        first.error = serial.SerialTimeoutException("write timeout")
        self.assertFalse(self.bridge.send(b"x"))
        self.assertTrue(first.closed.wait(3))
        second = self.connect()
        self.assertEqual(second.writes, [])
        self.assertTrue(self.bridge.send(b"o"))
        self.assertEqual(second.writes, [b"o"])

    def test_reconnection_discards_old_rankings_and_partial_lines(self):
        first = self.connect()
        first.incoming.put(b"PRESS child=1 rank=1\nPRESS child=2 rank=2\nPRESS child=")
        self.wait_for(lambda: self.sound.ranking == {1: 1, 2: 2})
        first.incoming.put(serial.SerialException("USB removed"))
        self.assertTrue(first.closed.wait(3))
        second = self.connect()
        self.assertEqual(self.sound.ranking, {})
        second.incoming.put(b"1 rank=1\nPRESS child=2 rank=1\n")
        self.wait_for(lambda: self.sound.ranking == {1: 2})

    def test_shutdown_closes_connected_port(self):
        connection = self.connect()
        self.shutdown()
        self.assertTrue(connection.closed.is_set())
        self.assertEqual(self.bridge.status()["status"], "disconnected")

    def test_request_waiting_for_lock_does_not_use_new_session(self):
        old = self.connect()
        captured = threading.Event()
        acquired = threading.Event()
        lock = self.bridge.lock

        class ObservedLock:
            def __enter__(self):
                captured.set()
                lock.acquire()
                acquired.set()

            def __exit__(self, *args):
                lock.release()

        lock.acquire()
        self.bridge.lock = ObservedLock()
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
            future = executor.submit(self.bridge.send, b"x")
            try:
                self.assertTrue(captured.wait(3))
                self.assertFalse(acquired.is_set())
                replacement = FakeSerial()
                self.bridge.connection = replacement
            finally:
                lock.release()
            self.assertFalse(future.result(timeout=3))
        with lock:
            self.bridge.connection = old
            self.bridge.lock = lock
        self.assertEqual(replacement.writes, [])


if __name__ == "__main__":
    unittest.main()
