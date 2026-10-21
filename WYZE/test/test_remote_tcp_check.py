"""Real loopback socket tests for the test runner, not the bulb or SSH tunnel.

Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only.
"""
from contextlib import contextmanager
from pathlib import Path
import selectors
import socket
import sys
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from remote_tcp_check import check_echo, validate_endpoint
from vendor.ssh_pattern import make_stream


@contextmanager
def endpoint(mode='echo'):
    stop = threading.Event()
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0))
    port = listener.getsockname()[1]
    listener.listen(1)
    listener.settimeout(0.5)
    peers = []
    errors = []

    def serve():
        try:
            peer, _ = listener.accept()
            peers.append(peer)
            with peer:
                peer.settimeout(1)
                if mode == 'early-close':
                    return
                if mode == 'silent':
                    stop.wait(2)
                    return
                data = bytearray()
                while not stop.is_set():
                    block = peer.recv(4096)
                    if not block:
                        break
                    if mode in ('echo', 'slow'):
                        peer.sendall(block)
                        if mode == 'slow':
                            time.sleep(0.002)
                    else:
                        data.extend(block)
                    if len(data) > 2 * 1024 * 1024:
                        raise AssertionError('Fixture exceeded its input bound')
                if mode == 'mismatch':
                    data[0] ^= 1
                if mode == 'truncated':
                    del data[-1:]
                if mode == 'extra':
                    data.extend(b'extra')
                if data:
                    peer.sendall(data)
                if mode == 'no-eof':
                    stop.wait(2)
                else:
                    peer.shutdown(socket.SHUT_WR)
        except OSError:
            pass  # Fault tests deliberately abort their peer.
        except BaseException as error:
            errors.append(error)

    worker = threading.Thread(target=serve, name='lightjack-loopback-fixture')
    worker.start()
    try:
        yield port
    finally:
        stop.set()
        for peer in peers:
            try:
                peer.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            peer.close()
        listener.close()
        worker.join(timeout=2)
        if worker.is_alive():
            raise AssertionError('Loopback fixture leaked a worker')
        if errors:
            raise errors[0]


class RemoteTcpCheckTests(unittest.TestCase):
    def test_binary_fragmented_echo(self):
        for chunk in (1, 257, 1024, 65536):
            with self.subTest(chunk=chunk), endpoint() as port:
                report = check_echo('127.0.0.1', port, size=65536, chunk=chunk, timeout=5)
                self.assertEqual(report['bytes_received'], 65536)
                self.assertTrue(report['read_eof'])
                self.assertTrue(report['write_half_closed'])
                self.assertFalse(report['ssh_or_device_identity_verified'])

    def test_reply_only_after_half_close(self):
        with endpoint('buffered') as port:
            report = check_echo('127.0.0.1', port, size=1024 * 1024, chunk=1024, timeout=5)
            self.assertEqual(report['bytes_received'], 1024 * 1024)

    def test_empty_transfer(self):
        with endpoint() as port:
            self.assertEqual(check_echo('127.0.0.1', port, size=0)['bytes_received'], 0)

    def test_slow_read_and_echo(self):
        with endpoint('slow') as port:
            self.assertEqual(check_echo('127.0.0.1', port, size=262144, chunk=257,
                                       read_delay=0.005, timeout=5)['bytes_received'], 262144)

    def test_corruption_rejected(self):
        with endpoint('mismatch') as port, self.assertRaisesRegex(ValueError, 'mismatch'):
            check_echo('127.0.0.1', port)

    def test_truncation_rejected(self):
        with endpoint('truncated') as port, self.assertRaisesRegex(ValueError, 'Incomplete'):
            check_echo('127.0.0.1', port)

    def test_extra_bytes_rejected(self):
        with endpoint('extra') as port, self.assertRaisesRegex(ValueError, 'extra bytes'):
            check_echo('127.0.0.1', port)

    def test_early_close_rejected(self):
        with endpoint('early-close') as port, self.assertRaises((OSError, ValueError)):
            check_echo('127.0.0.1', port)

    def test_complete_payload_without_eof_does_not_pass(self):
        with endpoint('no-eof') as port, self.assertRaises(TimeoutError):
            check_echo('127.0.0.1', port, timeout=0.2)

    def test_silent_peer_deadline(self):
        started = time.monotonic()
        with endpoint('silent') as port, self.assertRaises(TimeoutError):
            check_echo('127.0.0.1', port, timeout=0.2)
        self.assertLess(time.monotonic() - started, 2)

    def test_late_eof_after_scheduler_delay_does_not_pass(self):
        class DelayedSelector(selectors.DefaultSelector):
            def select(self, timeout=None):
                ready = super().select(timeout)
                if ready:
                    time.sleep(0.2)
                return ready
        with endpoint() as port, patch('remote_tcp_check.selectors.DefaultSelector', DelayedSelector):
            with self.assertRaises(TimeoutError):
                check_echo('127.0.0.1', port, size=0, timeout=0.1)

    def test_no_hostname_or_public_address(self):
        for host in ('localhost', 'example.com', '8.8.8.8', '0.0.0.0', '224.0.0.1', '::1', '192.168.1.1'):
            with self.subTest(host=host), self.assertRaises(ValueError):
                validate_endpoint(host, 8000)

    def test_explicit_lan_only(self):
        for host in ('192.168.1.1', '10.0.0.1', '172.16.0.1'):
            validate_endpoint(host, 8000, True)
        with self.assertRaises(ValueError):
            validate_endpoint('8.8.8.8', 8000, True)

    def test_invalid_sizes_and_times_rejected_before_connect(self):
        for settings in ({'size': -1}, {'size': 16777217}, {'chunk': 0}, {'chunk': 65537},
                         {'timeout': float('nan')}, {'timeout': float('inf')}, {'timeout': 0},
                         {'read_delay': -1}, {'read_delay': float('nan')}):
            with self.subTest(settings=settings), self.assertRaises(ValueError):
                check_echo('127.0.0.1', 1, **settings)

    def test_pattern_fixture(self):
        self.assertEqual(make_stream(1, 8).hex(), '2120040001060804')


if __name__ == '__main__':
    unittest.main(verbosity=2)
