"""Check a specified echo endpoint, including half-close, content and EOF.

This does not create an SSH session or prove which device forwarded the bytes.
Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only.
"""
from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import math
import selectors
import socket
import time

from vendor.ssh_pattern import make_stream


def validate_endpoint(host: str, port: int, allow_lan: bool = False, *,
                      allow_ephemeral: bool = False) -> None:
    ip = ipaddress.IPv4Address(host)
    lab_ranges = tuple(ipaddress.IPv4Network(n) for n in
                       ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16'))
    if not ip.is_loopback and not (allow_lan and any(ip in n for n in lab_ranges)):
        raise ValueError('Use loopback, or explicitly allow a private laboratory IPv4 address')
    minimum = 0 if allow_ephemeral else 1
    if not minimum <= port <= 65535:
        raise ValueError(f'Port must be {minimum} to 65535')


def check_echo(host: str, port: int, *, size: int = 65536, chunk: int = 1024,
               timeout: float = 15.0, seed: int = 0x4C4A,
               read_delay: float = 0.0, allow_lan: bool = False) -> dict:
    validate_endpoint(host, port, allow_lan)
    if not 0 <= size <= 16 * 1024 * 1024 or not 1 <= chunk <= 65536:
        raise ValueError('Size must be 0 to 16 MiB and chunk 1 to 65536 bytes')
    if not math.isfinite(timeout) or not 0 < timeout <= 300:
        raise ValueError('Timeout must be finite and 0 to 300 seconds')
    if not math.isfinite(read_delay) or not 0 <= read_delay <= 5:
        raise ValueError('Read delay must be finite and 0 to 5 seconds')
    payload = make_stream(seed, size)
    expected_hash = hashlib.sha256(payload).hexdigest()
    started = time.perf_counter()
    deadline = started + timeout
    sent = received = 0
    write_closed = eof = False
    read_due = started
    digest = hashlib.sha256()
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.setblocking(False)
        with selectors.DefaultSelector() as selector:
            registered = False
            while not eof:
                now = time.perf_counter()
                if now >= deadline:
                    raise TimeoutError(f'Deadline exceeded: sent={sent}, received={received}, expected={size}, EOF={eof}')
                if sent == size and not write_closed:
                    sock.shutdown(socket.SHUT_WR)
                    write_closed = True
                events = (selectors.EVENT_WRITE if sent < size else 0)
                if now >= read_due:
                    events |= selectors.EVENT_READ
                if not events:
                    # Windows select cannot wait on an empty descriptor set.
                    time.sleep(min(read_due - now, deadline - now))
                    continue
                if registered:
                    selector.modify(sock, events)
                else:
                    selector.register(sock, events)
                    registered = True
                wait = deadline - now
                if read_due > now:
                    wait = min(wait, read_due - now)
                for _, ready in selector.select(wait):
                    if ready & selectors.EVENT_WRITE:
                        try:
                            count = sock.send(memoryview(payload)[sent:min(sent + chunk, size)])
                        except BlockingIOError:
                            count = None
                        if count == 0:
                            raise ConnectionError('Peer stopped accepting bytes')
                        if count is not None:
                            sent += count
                    if ready & selectors.EVENT_READ:
                        try:
                            data = sock.recv(65536)
                        except BlockingIOError:
                            continue
                        if not data:
                            eof = True
                            break
                        if received + len(data) > size:
                            raise ValueError('Peer returned extra bytes')
                        expected = payload[received:received + len(data)]
                        if data != expected:
                            offset = next(i for i, (a, b) in enumerate(zip(data, expected)) if a != b)
                            raise ValueError(f'Byte mismatch at offset {received + offset}')
                        digest.update(data)
                        received += len(data)
                        read_due = time.perf_counter() + read_delay
    if sent != size or received != size or not write_closed:
        raise ValueError(f'Incomplete transfer: sent={sent}, received={received}, expected={size}')
    actual_hash = digest.hexdigest()
    if actual_hash != expected_hash:
        raise ValueError('SHA256 mismatch')
    elapsed = time.perf_counter() - started
    if elapsed > timeout:
        raise TimeoutError('Transfer completed after the deadline')
    return {'scope': 'specified_tcp_echo_endpoint', 'host': host, 'port': port,
            'bytes_sent': sent, 'bytes_received': received, 'chunk': chunk,
            'sha256': actual_hash, 'write_half_closed': write_closed, 'read_eof': eof,
            'seconds': round(elapsed, 6),
            'echo_bytes_per_second': round(size / elapsed, 1) if elapsed > 0 else None,
            'read_delay_seconds': read_delay, 'ssh_or_device_identity_verified': False}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--allow-lan', action='store_true')
    parser.add_argument('--size', type=int, default=65536)
    parser.add_argument('--chunk', type=int, default=1024)
    parser.add_argument('--timeout', type=float, default=15)
    parser.add_argument('--seed', type=lambda value: int(value, 0), default=0x4C4A)
    parser.add_argument('--read-delay', type=float, default=0)
    parser.add_argument('--cycles', type=int, default=1)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 100:
        parser.error('cycles must be 1 to 100')
    results = []
    try:
        for _ in range(args.cycles):
            results.append(check_echo(args.host, args.port, size=args.size, chunk=args.chunk,
                timeout=args.timeout, seed=args.seed, read_delay=args.read_delay, allow_lan=args.allow_lan))
    except (OSError, ValueError) as error:
        print(json.dumps({'passed': False, 'completed_cycles': len(results), 'error': str(error),
                          'results': results}))
        return 1
    print(json.dumps({'passed': True, 'results': results}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
