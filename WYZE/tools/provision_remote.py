"""Validate supplied per-device credentials and render a private build header.

Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only.
This tool does not generate keys, contact a relay, authorize a key or flash a bulb.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.exceptions import UnsupportedAlgorithm

REPOSITORY = Path(__file__).resolve().parents[2]
HEADER_NAME = 'lightjack_remote_config_private.h'
FIELDS = ('ssid', 'wifi_password', 'relay_ipv4', 'relay_port', 'username',
          'private_key_pem', 'public_key_ssh', 'host_sha256_hex', 'target_ipv4',
          'target_port', 'remote_port')
PORTS = {'relay_port': 1, 'target_port': 1, 'remote_port': 1024}


def text_field(value, name, low, high):
    if not isinstance(value, str) or '\x00' in value:
        raise ValueError(f'{name}: expected text without NUL')
    try:
        length = len(value.encode('utf-8'))
    except UnicodeError:
        raise ValueError(f'{name}: invalid UTF-8 text') from None
    if not low <= length <= high:
        raise ValueError(f'{name}: invalid byte length')
    return value


def fingerprint(value):
    text_field(value, 'host_sha256_hex', 1, 100)
    if re.fullmatch(r'[0-9a-fA-F]{64}', value):
        return value.lower()
    if value.startswith('SHA256:'):
        try:
            body = value[7:]
            raw = base64.b64decode(body + '=' * (-len(body) % 4), validate=True)
            if len(raw) == 32:
                return raw.hex()
        except (ValueError, TypeError):
            pass
    raise ValueError('host_sha256_hex: use a 64-character hex or OpenSSH SHA256 fingerprint')


def public_key(value, name):
    text_field(value, name, 1, 1024)
    if '\n' in value or '\r' in value:
        raise ValueError(f'{name}: expected one OpenSSH public-key line')
    try:
        key = serialization.load_ssh_public_key(value.encode('ascii'))
    except (ValueError, TypeError, UnicodeError, UnsupportedAlgorithm):
        raise ValueError(f'{name}: invalid OpenSSH public key') from None
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError(f'{name}: ECDSA nistp256 is required')
    canonical = key.public_bytes(serialization.Encoding.OpenSSH, serialization.PublicFormat.OpenSSH).decode('ascii')
    blob = base64.b64decode(canonical.split()[1], validate=True)
    return key, canonical, hashlib.sha256(blob).hexdigest()


def validate_config(config):
    if not isinstance(config, dict) or set(config) - set(FIELDS) - {'relay_host_public_key'}:
        raise ValueError('Unsupported configuration fields or root type')
    required = set(FIELDS) - {'host_sha256_hex'}
    if not required <= set(config):
        raise ValueError('Required configuration fields are missing')
    normalized = {}
    for name, low, high in (('ssid', 1, 32), ('wifi_password', 8, 63), ('username', 1, 64)):
        normalized[name] = text_field(config[name], name, low, high)
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', normalized['username']):
        raise ValueError('username: unsupported character')
    for name in ('relay_ipv4', 'target_ipv4'):
        try:
            address = ipaddress.IPv4Address(config[name]) if isinstance(config[name], str) else None
        except ipaddress.AddressValueError:
            address = None
        if address is None or not 0 < int(str(address).split('.')[0]) < 224:
            raise ValueError(f'{name}: expected a literal unicast IPv4 address')
        normalized[name] = str(address)
    for name, minimum in PORTS.items():
        if type(config[name]) is not int or not minimum <= config[name] <= 65535:
            raise ValueError(f'{name}: invalid port')
        normalized[name] = config[name]

    pem = text_field(config['private_key_pem'], 'private_key_pem', 1, 8192).replace('\r\n', '\n')
    if not re.fullmatch(r'-----BEGIN EC PRIVATE KEY-----\n(?:[A-Za-z0-9+/=]+\n)+-----END EC PRIVATE KEY-----\n?', pem):
        raise ValueError('private_key_pem: unencrypted traditional EC PEM is required')
    try:
        private = serialization.load_pem_private_key(pem.encode('ascii'), password=None)
    except (ValueError, TypeError, UnsupportedAlgorithm):
        raise ValueError('private_key_pem: invalid EC private key') from None
    if not isinstance(private, ec.EllipticCurvePrivateKey) or not isinstance(private.curve, ec.SECP256R1):
        raise ValueError('private_key_pem: ECDSA nistp256 is required')
    public, normalized['public_key_ssh'], client_pin = public_key(config['public_key_ssh'], 'public_key_ssh')
    if private.public_key().public_numbers() != public.public_numbers():
        raise ValueError('Client private and public keys do not match')
    normalized['private_key_pem'] = pem.rstrip('\n') + '\n'

    host_pin = fingerprint(config['host_sha256_hex']) if 'host_sha256_hex' in config else None
    if 'relay_host_public_key' in config:
        _, _, derived = public_key(config['relay_host_public_key'], 'relay_host_public_key')
        if host_pin is not None and host_pin != derived:
            raise ValueError('Relay public key and supplied fingerprint do not match')
        host_pin = derived
    if host_pin is None:
        raise ValueError('A trusted relay host fingerprint or public key is required')
    normalized['host_sha256_hex'] = host_pin
    return normalized, client_pin


def render_header(config):
    # Fixed-width octal escapes preserve UTF-8 bytes without C literal injection
    # or the variable-length hexadecimal escape ambiguity.
    def literal(value):
        return '"' + ''.join(f'\\{byte:03o}' for byte in value.encode('utf-8')) + '"'
    rows = [str(config[name]) if name in PORTS else literal(config[name]) for name in FIELDS]
    return ('// PRIVATE: generated Lightjack credentials. Do not publish.\n#pragma once\n'
            'static const lightjack_remote_config lightjack_remote_provisioned = {\n    ' +
            ',\n    '.join(rows) + '\n};\n')


def reject_duplicate_fields(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise ValueError('Duplicate configuration field')
        result[name] = value
    return result


def private_path(path):
    path = Path(path).resolve()
    if path.is_relative_to(REPOSITORY) or path.is_relative_to((Path.home() / '.ssh').resolve()):
        raise ValueError('Use a private path outside the source repository and desktop SSH directory')
    return path


def load_config(path):
    path = private_path(path)
    with path.open('rb') as source:
        data = source.read(65537)
    if len(data) > 65536:
        raise ValueError('Configuration JSON exceeds 64 KiB')
    try:
        return json.loads(data, object_pairs_hook=reject_duplicate_fields)
    except (json.JSONDecodeError, UnicodeError):
        raise ValueError('Malformed configuration JSON') from None


def write_bundle(output, header, public, client_pin, host_pin):
    output = private_path(output)
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    header_bytes = header.encode('utf-8')
    manifest = {'format': 1, 'header_sha256': hashlib.sha256(header_bytes).hexdigest(),
                'client_public_key': public, 'client_sha256_hex': client_pin,
                'relay_host_sha256_hex': host_pin}
    # Write the manifest last. A partial bundle has no valid build marker.
    for name, data in ((HEADER_NAME, header_bytes),
                       ('client-public-key.txt', (public + '\n').encode('ascii')),
                       ('manifest.json', (json.dumps(manifest, indent=2) + '\n').encode())):
        descriptor = os.open(output / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, 'wb') as target:
            target.write(data)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check', action='store_true', help='Validate without writing a bundle')
    args = parser.parse_args()
    if args.check == (args.output is not None):
        parser.error('Choose exactly one of --check or --output')
    try:
        config, client_pin = validate_config(load_config(args.config))
        if not args.check:
            write_bundle(args.output, render_header(config), config['public_key_ssh'],
                         client_pin, config['host_sha256_hex'])
    except (OSError, ValueError) as error:
        print(json.dumps({'valid': False, 'error': str(error)}))
        return 1
    print(json.dumps({'valid': True, 'bundle_written': not args.check,
                      'client_sha256_hex': client_pin,
                      'relay_host_sha256_hex': config['host_sha256_hex']}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
