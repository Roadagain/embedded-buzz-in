import concurrent.futures
from contextlib import closing
import http.client
from http.server import ThreadingHTTPServer
import json
import threading
import unittest

import serial

from http_server import make_handler
from serial_connection import SerialBridge
from test_support import FakeSerial


class HttpBridgeTests(unittest.TestCase):
    def setUp(self):
        self.connection = FakeSerial()
        self.bridge = SerialBridge("test-port")
        self.bridge.connection = self.connection
        self.bridge.available = True
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

    def test_status_reports_connection_without_sending_commands(self):
        status, cache, body = self.request("/status")
        self.assertEqual(status, 200)
        self.assertEqual(cache, "no-store")
        self.assertEqual(json.loads(body), {"status": "connected", "port": "test-port"})
        self.bridge.available = False
        status, cache, body = self.request("/status")
        self.assertEqual(status, 200)
        self.assertEqual(cache, "no-store")
        self.assertEqual(json.loads(body)["status"], "disconnected")
        self.assertEqual(self.connection.writes, [])

    def test_unknown_urls_and_other_methods_do_not_send(self):
        for path in ("/", "/favicon.ico", "/reset/", "/reset?command=x"):
            status, cache, _ = self.request(path)
            self.assertEqual(status, 404)
            self.assertEqual(cache, "no-store")
        self.assertEqual(self.request("/wrong", "POST")[0], 501)
        self.assertEqual(self.request("/wrong", "HEAD")[0], 501)
        self.assertEqual(self.connection.writes, [])

    def test_mode_reports_latest_notification_without_sending_commands(self):
        for line, mode in ((None, None),
                           ("MODE mode=endless_chance", "endless_chance"),
                           ("MODE mode=first_only", "first_only"),
                           ("RESET", "first_only"),
                           ("BOOT", None)):
            if line is not None:
                self.bridge._handle_line(self.connection, line)
            status, cache, body = self.request("/mode")
            self.assertEqual(status, 200)
            self.assertEqual(cache, "no-store")
            self.assertEqual(json.loads(body), {"mode": mode})
        self.assertEqual(self.connection.writes, [])

    def test_mode_is_unknown_when_disconnected(self):
        self.bridge._handle_line(self.connection, "MODE mode=first_only")
        self.bridge.available = False
        self.assertEqual(self.request("/mode")[0], 200)
        self.assertEqual(json.loads(self.request("/mode")[2]), {"mode": None})
        self.bridge.available = True
        self.connection.is_open = False
        self.assertEqual(json.loads(self.request("/mode")[2]), {"mode": None})
        self.assertEqual(self.connection.writes, [])

    def test_mode_rejects_other_urls_and_methods(self):
        for path in ("/mode/", "/mode?refresh=1"):
            self.assertEqual(self.request(path)[0], 404)
        self.assertEqual(self.request("/mode", "POST")[0], 501)
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
