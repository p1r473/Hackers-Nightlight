"""Temporary, bounded echo service for an explicitly chosen laboratory endpoint.

Adapted from playmiel/ESP-Reverse_Tunneling_Libssh2 slow_echo/server.py,
commit 8b3eba99ae696046a5aeb3db6488e1005616b08a, MIT. See
../third_party/lightjack-ssh/reverse-tunnel/LICENSE.
Lightjack modifications, 2026-10-21: loopback default, explicit LAN opt-in,
finite lifetime, one connection at a time, bounded socket calls, no Docker.
"""
from __future__ import annotations
import argparse
import json
import math
import socket
import time
from remote_tcp_check import validate_endpoint


def serve(host: str, port: int, *, seconds: float = 300, rate: int = 0,
          allow_lan: bool = False) -> None:
    validate_endpoint(host, port, allow_lan, allow_ephemeral=True)
    if not math.isfinite(seconds) or not 0 < seconds <= 3600:
        raise ValueError('Service lifetime must be 0 to 3600 seconds')
    if not 0 <= rate <= 1024 * 1024:
        raise ValueError('Rate must be 0 for unlimited, or 1 to 1048576 bytes/second')
    deadline = time.monotonic() + seconds
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind((host, port))
        listener.listen(1)
        listener.settimeout(0.25)
        print(json.dumps({'listening': list(listener.getsockname()), 'lifetime_seconds': seconds,
                          'rate_bytes_per_second': rate}), flush=True)
        while time.monotonic() < deadline:
            try:
                connection, _ = listener.accept()
            except TimeoutError:
                continue
            with connection:
                try:
                    while time.monotonic() < deadline:
                        connection.settimeout(min(2.0, max(0.001, deadline - time.monotonic())))
                        data = connection.recv(4096)
                        if not data:
                            connection.shutdown(socket.SHUT_WR)
                            break
                        for offset in range(0, len(data), 64 if rate else 4096):
                            piece = data[offset:offset + (64 if rate else 4096)]
                            if time.monotonic() >= deadline:
                                break
                            connection.settimeout(min(2.0, max(0.001, deadline - time.monotonic())))
                            connection.sendall(piece)
                            if rate:
                                time.sleep(min(len(piece) / rate, max(0, deadline - time.monotonic())))
                except OSError:
                    # A failed or idle test connection does not leave a worker behind.
                    continue
    print(json.dumps({'stopped': True}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, required=True,
                        help='Listen port, or 0 to allocate a temporary port and print it')
    parser.add_argument('--seconds', type=float, default=300)
    parser.add_argument('--rate', type=int, default=0)
    parser.add_argument('--allow-lan', action='store_true')
    args = parser.parse_args()
    try:
        serve(args.host, args.port, seconds=args.seconds, rate=args.rate, allow_lan=args.allow_lan)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')
