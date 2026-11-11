#!/usr/bin/env python3
"""Keep a USB serial connection open and print firmware events."""

import argparse
import logging

import serial


BAUD_RATE = 115200
READ_TIMEOUT_SECONDS = 0.2
WRITE_TIMEOUT_SECONDS = 1.0
LOGGER = logging.getLogger("bridge")


def log_serial_lines(connection):
    """Retain incomplete lines across read timeouts."""
    pending = bytearray()
    while True:
        chunk = connection.read(min(connection.in_waiting or 1, 4096))
        if not chunk:
            continue
        pending.extend(chunk)
        while b"\n" in pending:
            line, _, remainder = pending.partition(b"\n")
            pending = remainder
            LOGGER.info("RX %s", line.rstrip(b"\r").decode("utf-8", errors="replace"))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/cu.usbserial-10")
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
            log_serial_lines(connection)
    except KeyboardInterrupt:
        LOGGER.info("Stopped")
        return 0
    except (serial.SerialException, OSError) as error:
        LOGGER.error("Serial connection failed: %s", error)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
