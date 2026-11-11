"""Shared fake serial connection for bridge tests."""

import queue
import threading
import time


class FakeSerial:
    is_open = True

    def __init__(self):
        self.writes = []
        self.error = None
        self.partial = False
        self.writing = False
        self.overlap = False
        self.in_waiting = 1
        self.incoming = queue.Queue()
        self.closed = threading.Event()

    def read(self, size):
        try:
            chunk = self.incoming.get(timeout=0.01)
        except queue.Empty:
            return b""
        if isinstance(chunk, Exception):
            raise chunk
        return chunk

    def close(self):
        self.is_open = False
        self.closed.set()

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
