"""Exercise the real command-line tools on loopback, without SSH or a bulb.

Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only.
"""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import sys
import threading
import time
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
NO_WINDOW = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0


def run_tool(name, *args, timeout=5):
    return subprocess.run([sys.executable, str(TOOLS / name), *map(str, args)],
                          capture_output=True, text=True, timeout=timeout,
                          creationflags=NO_WINDOW)


@contextmanager
def echo_service(*args):
    process = subprocess.Popen(
        [sys.executable, str(TOOLS / 'lab_echo.py'), '--port', '0', *map(str, args)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        creationflags=NO_WINDOW)
    output = queue.Queue()
    def first_line():
        output.put(process.stdout.readline())
    reader = threading.Thread(target=first_line, name='lightjack-cli-startup')
    reader.start()
    try:
        line = output.get(timeout=5)
        if not line:
            raise AssertionError('Echo service did not report startup')
        report = json.loads(line)
        if report['listening'][0] != '127.0.0.1' or report['listening'][1] == 0:
            raise AssertionError('Echo service did not bind a loopback temporary port')
        yield process, report['listening'][1]
    finally:
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=5)
        reader.join(timeout=2)
        if reader.is_alive():
            raise AssertionError('Echo service startup reader did not exit')
        process.stdout.close()
        process.stderr.close()


class RemoteTcpCliTests(unittest.TestCase):
    def test_multiple_connections_and_json_report(self):
        with echo_service('--seconds', 10) as (_, port):
            result = run_tool('remote_tcp_check.py', '--port', port, '--cycles', 3,
                              '--size', 1048576, '--chunk', 257)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        report = json.loads(result.stdout)
        self.assertTrue(report['passed'])
        self.assertEqual(len(report['results']), 3)
        for cycle in report['results']:
            self.assertEqual(cycle['bytes_sent'], 1048576)
            self.assertEqual(cycle['bytes_received'], 1048576)
            self.assertTrue(cycle['read_eof'])
            self.assertFalse(cycle['ssh_or_device_identity_verified'])

    def test_rate_limited_echo(self):
        with echo_service('--seconds', 10, '--rate', 16384) as (_, port):
            result = run_tool('remote_tcp_check.py', '--port', port, '--size', 16384,
                              '--read-delay', 0.01)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        report = json.loads(result.stdout)['results'][0]
        self.assertGreaterEqual(report['seconds'], 0.8)
        self.assertTrue(report['write_half_closed'])

    def test_service_expires_and_releases_port(self):
        with echo_service('--seconds', 0.4) as (process, port):
            process.wait(timeout=3)
            self.assertEqual(process.returncode, 0)
            self.assertEqual(json.loads(process.stdout.readline()), {'stopped': True})
            with self.assertRaises(OSError):
                socket.create_connection(('127.0.0.1', port), timeout=0.2)

    def test_lifetime_bounds_a_connected_idle_peer(self):
        with echo_service('--seconds', 0.4) as (process, port):
            with socket.create_connection(('127.0.0.1', port), timeout=1) as peer:
                peer.settimeout(2)
                started = time.monotonic()
                self.assertEqual(peer.recv(1), b'')
                self.assertLess(time.monotonic() - started, 2)
            process.wait(timeout=2)
            self.assertEqual(process.returncode, 0)

    def test_invalid_server_options_fail_before_listening(self):
        for args in (('--host', '0.0.0.0'), ('--seconds', 'nan'), ('--seconds', '0'),
                     ('--rate', '-1')):
            with self.subTest(args=args):
                result = run_tool('lab_echo.py', '--port', 0, *args)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, '')

    def test_invalid_client_endpoint_has_failure_json(self):
        for port in (0, 65536):
            with self.subTest(port=port):
                result = run_tool('remote_tcp_check.py', '--port', port)
                self.assertEqual(result.returncode, 1)
                self.assertFalse(json.loads(result.stdout)['passed'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
