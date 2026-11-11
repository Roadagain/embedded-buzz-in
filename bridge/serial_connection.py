"""Maintain USB serial communication and reconnect after failures."""

import logging
import threading

import serial


BAUD_RATE = 115200
READ_TIMEOUT_SECONDS = 0.2
WRITE_TIMEOUT_SECONDS = 1.0
RECONNECT_INTERVAL_SECONDS = 1.0
LOGGER = logging.getLogger("bridge")


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
    def __init__(self, port, *, on_line=None, on_disconnect=None, connection_factory=None):
        self.port = port
        self.connection = None
        self.connection_factory = connection_factory or serial.Serial
        self.on_line = on_line
        self.on_disconnect = on_disconnect
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
                log_serial_lines(connection, stop, self.on_line, self.disconnected)
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
                if self.on_disconnect is not None:
                    self.on_disconnect()
            stop.wait(RECONNECT_INTERVAL_SECONDS)
