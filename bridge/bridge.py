#!/usr/bin/env python3
"""Forward local HTTP actions to USB serial and print firmware events."""

import argparse
import logging
from pathlib import Path
import re
import shutil
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial


BAUD_RATE = 115200
READ_TIMEOUT_SECONDS = 0.2
WRITE_TIMEOUT_SECONDS = 1.0
LOGGER = logging.getLogger("bridge")
COMMANDS = {"/reset": b"r", "/correct": b"o", "/wrong": b"x"}
FIRST_PRESS = re.compile(r"PRESS child=[1-9]\d* rank=1(?: elapsed_ms=\d+)?")


class PressSound:
    """Play first-place presses without blocking reception or queuing sounds."""

    def __init__(self, filename):
        self.process = None
        self.filename = None
        self.player = None
        if filename is None:
            return
        path = Path(filename).expanduser().resolve()
        self.player = shutil.which("afplay")
        if not path.is_file() or self.player is None:
            LOGGER.warning("Press sound disabled: sound file or afplay not found: %s", path)
            return
        self.filename = str(path)

    def handle_line(self, line):
        if self.process is not None:
            status = self.process.poll()
            if status is not None:
                if status != 0:
                    LOGGER.warning("Press sound playback failed (exit status %s)", status)
                self.process = None
        if self.filename is None or FIRST_PRESS.fullmatch(line) is None:
            return
        if self.process is not None:
            # Do not accumulate delayed sounds when presses arrive close together.
            LOGGER.debug("Press sound skipped: playback already active")
            return
        try:
            self.process = subprocess.Popen(
                [self.player, self.filename],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            LOGGER.info("Playing press sound: %s", self.filename)
        except OSError as error:
            LOGGER.warning("Press sound could not start: %s", error)

    def close(self):
        if self.process is None:
            return
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.process = None


def log_serial_lines(connection, stop, on_line=None):
    """Retain incomplete lines across read timeouts."""
    pending = bytearray()
    while not stop.is_set():
        chunk = connection.read(min(connection.in_waiting or 1, 4096))
        if not chunk:
            continue
        pending.extend(chunk)
        while b"\n" in pending:
            line, _, remainder = pending.partition(b"\n")
            pending = remainder
            decoded = line.rstrip(b"\r").decode("utf-8", errors="replace")
            LOGGER.info("RX %s", decoded)
            if on_line is not None:
                on_line(decoded)


class SerialBridge:
    def __init__(self, connection, sound=None):
        self.connection = connection
        self.sound = sound
        self.lock = threading.Lock()
        self.available = True

    def send(self, command):
        with self.lock:
            if not self.available or not self.connection.is_open:
                return False
            try:
                if self.connection.write(command) != len(command):
                    raise serial.SerialTimeoutException("Incomplete serial write")
            except (serial.SerialException, OSError) as error:
                self.available = False
                LOGGER.error("Serial write failed: %s", error)
                return False
            LOGGER.info("TX %s", command.decode("ascii"))
            return True

    def receive(self, stop):
        try:
            log_serial_lines(self.connection, stop, self.sound.handle_line if self.sound else None)
        except (serial.SerialException, OSError) as error:
            with self.lock:
                self.available = False
            LOGGER.error("Serial read failed: %s", error)


def make_handler(bridge):
    class Handler(BaseHTTPRequestHandler):
        def end_headers(self):
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def do_GET(self):
            command = COMMANDS.get(self.path)
            if command is None:
                self.send_error(404, "Unknown action")
                return
            if bridge.send(command):
                status, body = 202, b"Sent to serial; execution is not confirmed.\n"
            else:
                status, body = 503, b"Serial connection unavailable; command was not retried.\n"
            self.send_response(status)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, message, *args):
            LOGGER.info("HTTP %s", message % args)

    return Handler


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/cu.usbserial-10")
    parser.add_argument("--http-port", type=int, default=58912, help="Local HTTP port (default: 58912)")
    parser.add_argument("--press-sound", help="Audio file for rank=1 presses (macOS afplay; default: silent)")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    sound = PressSound(args.press_sound)

    try:
        with serial.Serial(
            port=args.port,
            baudrate=BAUD_RATE,
            timeout=READ_TIMEOUT_SECONDS,
            write_timeout=WRITE_TIMEOUT_SECONDS,
        ) as connection:
            LOGGER.info("Connected to %s at %s bps", args.port, BAUD_RATE)
            bridge = SerialBridge(connection, sound)
            with ThreadingHTTPServer(("127.0.0.1", args.http_port), make_handler(bridge)) as server:
                stop = threading.Event()
                receiver = threading.Thread(target=bridge.receive, args=(stop,), name="serial-reader")
                receiver.start()
                try:
                    LOGGER.info("HTTP listening on http://127.0.0.1:%s", server.server_port)
                    server.serve_forever()
                finally:
                    stop.set()
                    receiver.join()
    except KeyboardInterrupt:
        LOGGER.info("Stopped")
        return 0
    except (serial.SerialException, OSError) as error:
        LOGGER.error("Bridge failed: %s", error)
        return 1
    finally:
        sound.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
