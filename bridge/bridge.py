#!/usr/bin/env python3
"""Forward local HTTP actions to USB serial and print firmware events."""

import argparse
import logging
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial


BAUD_RATE = 115200
READ_TIMEOUT_SECONDS = 0.2
WRITE_TIMEOUT_SECONDS = 1.0
LOGGER = logging.getLogger("bridge")
COMMANDS = {"/reset": b"r", "/correct": b"o", "/wrong": b"x"}


def log_serial_lines(connection, stop):
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
            LOGGER.info("RX %s", line.rstrip(b"\r").decode("utf-8", errors="replace"))


class SerialBridge:
    def __init__(self, connection):
        self.connection = connection
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
            log_serial_lines(self.connection, stop)
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
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    try:
        with serial.Serial(
            port=args.port,
            baudrate=BAUD_RATE,
            timeout=READ_TIMEOUT_SECONDS,
            write_timeout=WRITE_TIMEOUT_SECONDS,
        ) as connection:
            LOGGER.info("Connected to %s at %s bps", args.port, BAUD_RATE)
            bridge = SerialBridge(connection)
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
