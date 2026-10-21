"""Explicit opt-in to an external private configuration, never a source-tree key.

Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only.
Uses only stdlib so the ordinary PlatformIO environment needs no crypto package.
"""
import hashlib
import json
import os
from pathlib import Path

HEADER_NAME = 'lightjack_remote_config_private.h'


def configure_private(env, value):
    if value is None:
        return False
    if not value.strip():
        raise ValueError('LIGHTJACK_REMOTE_CONFIG_DIR is empty; unset it for an unconfigured build')
    if env['PIOENV'] != 'lightjack_wyze_ssh_poc':
        raise ValueError('Private configuration requires the SSH POC build target')
    directory = Path(value).resolve()
    repository = Path(env['PROJECT_DIR']).resolve().parent
    if directory.is_relative_to(repository) or directory.is_relative_to((Path.home() / '.ssh').resolve()):
        raise ValueError('Private configuration must be outside the repository and desktop SSH directory')
    header = directory / HEADER_NAME
    marker = directory / 'manifest.json'
    if header.resolve().parent != directory or marker.resolve().parent != directory:
        raise ValueError('Configuration bundle files must be in the selected directory')
    if not header.is_file() or not marker.is_file() or header.stat().st_size > 131072 or marker.stat().st_size > 8192:
        raise ValueError('Private configuration bundle is missing or oversized')
    try:
        manifest = json.loads(marker.read_text(encoding='utf-8'))
    except (ValueError, UnicodeError):
        raise ValueError('Invalid private configuration manifest') from None
    if not isinstance(manifest, dict) or type(manifest.get('format')) is not int or manifest['format'] != 1:
        raise ValueError('Unsupported private configuration format')
    digest = hashlib.sha256(header.read_bytes()).hexdigest()
    if digest != manifest.get('header_sha256'):
        raise ValueError('Private configuration header hash mismatch')
    env.Append(CPPDEFINES=[('LIGHTJACK_REMOTE_CONFIGURED', 1)], CPPPATH=[str(directory)])
    return True


if 'Import' in globals():
    Import('env')
    configure_private(env, os.environ.get('LIGHTJACK_REMOTE_CONFIG_DIR'))
