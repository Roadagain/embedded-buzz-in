"""Translate local HTTP requests into bridge operations."""

import json
import logging
from http.server import BaseHTTPRequestHandler


LOGGER = logging.getLogger("bridge")
COMMANDS = {"/reset": b"r", "/correct": b"o", "/wrong": b"x"}


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
            if self.path == "/mode":
                body = (json.dumps(bridge.input_mode()) + "\n").encode("utf-8")
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
