#!/usr/bin/env python3
"""Forward local HTTP actions to USB serial and print firmware events."""

import argparse
import json
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
RECONNECT_INTERVAL_SECONDS = 1.0
LOGGER = logging.getLogger("bridge")
COMMANDS = {"/reset": b"r", "/correct": b"o", "/wrong": b"x"}
PRESS = re.compile(r"PRESS child=([1-9]\d*) rank=([1-9]\d*)(?: elapsed_ms=\d+)?")
WRONG = re.compile(r"JUDGE result=wrong child=([1-9]\d*)")


class PressSound:
    """Play first-place presses and promotions without queuing sounds."""

    def __init__(self, filename):
        self.process = None
        self.retired = []
        self.ranking = {}
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
        # Reap interrupted playback without waiting in the serial receive loop.
        self.retired = [process for process in self.retired if process.poll() is None]
        if self.process is not None:
            status = self.process.poll()
            if status is not None:
                if status != 0:
                    LOGGER.warning("Press sound playback failed (exit status %s)", status)
                self.process = None
        promoted = False
        should_play = False
        if line in ("BOOT", "RESET") or line.startswith("JUDGE result=correct child="):
            self.ranking.clear()
        press = PRESS.fullmatch(line)
        wrong = WRONG.fullmatch(line)
        if press is not None:
            child, rank = map(int, press.groups())
            if rank == 1:
                self.ranking.clear()
                should_play = True
            self.ranking[rank] = child
        elif wrong is not None and self.ranking.get(1) == int(wrong.group(1)):
            self.ranking = {rank - 1: child for rank, child in self.ranking.items() if rank > 1}
            promoted = 1 in self.ranking
            should_play = promoted
        if self.filename is None or not should_play:
            return
        if self.process is not None:
            if not promoted:
                LOGGER.debug("Press sound skipped: playback already active")
                return
            # A promotion must sound even immediately after the first press.
            self.process.terminate()
            self.retired.append(self.process)
            self.process = None
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
        processes = self.retired + ([self.process] if self.process is not None else [])
        for process in processes:
            if process.poll() is not None:
                continue
            process.terminate()
            try:
                process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        self.process = None
        self.retired = []


def log_serial_lines(connection, stop, on_line=None, disconnected=None):
    """Retain incomplete lines across read timeouts."""
    pending = bytearray()
    while not stop.is_set() and not (disconnected and disconnected.is_set()):
        chunk = connection.read(min(connection.in_waiting or 1, 4096))
        if stop.is_set() or (disconnected and disconnected.is_set()):
            return
        if not chunk:
            continue
        pending.extend(chunk)
        while b"\n" in pending:
            if stop.is_set() or (disconnected and disconnected.is_set()):
                return
            line, _, remainder = pending.partition(b"\n")
            pending = remainder
            decoded = line.rstrip(b"\r").decode("utf-8", errors="replace")
            LOGGER.info("RX %s", decoded)
            if on_line is not None:
                on_line(decoded)


class SerialBridge:
    def __init__(self, port, sound=None, connection_factory=None):
        self.port = port
        self.connection = None
        self.connection_factory = connection_factory or serial.Serial
        self.sound = sound
        self.lock = threading.Lock()
        self.available = False
        self.disconnected = threading.Event()

    def status(self):
        with self.lock:
            connected = self.available and self.connection is not None and self.connection.is_open
            return {"status": "connected" if connected else "disconnected", "port": self.port}

    def send(self, command):
        # A request waiting for the write lock must not move to a new USB session.
        connection = self.connection
        with self.lock:
            if (connection is None or connection is not self.connection
                    or not self.available or not connection.is_open):
                return False
            try:
                if connection.write(command) != len(command):
                    raise serial.SerialTimeoutException("Incomplete serial write")
            except (serial.SerialException, OSError) as error:
                self.available = False
                self.disconnected.set()
                LOGGER.error("Serial write failed: %s", error)
                return False
            LOGGER.info("TX %s", command.decode("ascii"))
            return True

    def receive(self, stop):
        while not stop.is_set():
            try:
                connection = self.connection_factory(
                    port=self.port,
                    baudrate=BAUD_RATE,
                    timeout=READ_TIMEOUT_SECONDS,
                    write_timeout=WRITE_TIMEOUT_SECONDS,
                )
            except (serial.SerialException, OSError) as error:
                LOGGER.warning("Serial connection failed; retrying: %s", error)
                stop.wait(RECONNECT_INTERVAL_SECONDS)
                continue
            try:
                with self.lock:
                    if stop.is_set():
                        return
                    self.disconnected.clear()
                    self.connection = connection
                    self.available = True
                LOGGER.info("Connected to %s at %s bps", self.port, BAUD_RATE)
                log_serial_lines(connection, stop, self.sound.handle_line if self.sound else None,
                                 self.disconnected)
            except (serial.SerialException, OSError) as error:
                LOGGER.warning("Serial read failed; reconnecting: %s", error)
            finally:
                with self.lock:
                    self.available = False
                    self.connection = None
                    self.disconnected.set()
                    try:
                        connection.close()
                    except (serial.SerialException, OSError) as error:
                        LOGGER.warning("Serial close failed: %s", error)
                if self.sound is not None:
                    self.sound.close()
                    self.sound.ranking.clear()
            stop.wait(RECONNECT_INTERVAL_SECONDS)


def make_handler(bridge):
    class Handler(BaseHTTPRequestHandler):
        def end_headers(self):
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def do_GET(self):
            if self.path == "/status":
                body = (json.dumps(bridge.status()) + "\n").encode("utf-8")
                self.respond(200, body, "application/json; charset=utf-8")
                return
            command = COMMANDS.get(self.path)
            if command is None:
                self.send_error(404, "Unknown action")
                return
            if bridge.send(command):
                status, body = 202, b"Sent to serial; execution is not confirmed.\n"
            else:
                status, body = 503, b"Serial connection unavailable; command was not retried.\n"
            self.respond(status, body)

        def respond(self, status, body, content_type="text/plain; charset=utf-8"):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
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
    parser.add_argument("--press-sound", help="Audio file for first presses and promotions (macOS afplay; default: silent)")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    sound = PressSound(args.press_sound)

    try:
        bridge = SerialBridge(args.port, sound)
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
