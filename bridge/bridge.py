#!/usr/bin/env python3
"""Forward local HTTP actions to USB serial and print firmware events."""

import argparse
import logging
import threading
from http.server import ThreadingHTTPServer

import serial

from http_server import make_handler
from press_sound import PressSound
from serial_connection import SerialBridge


LOGGER = logging.getLogger("bridge")


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
