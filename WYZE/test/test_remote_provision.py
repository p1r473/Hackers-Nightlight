"""Credential validation and build selection tests with no operational keys.

SPDX-License-Identifier: GPL-3.0-only
Publicly known scalar 1/2 test keys exist only in memory. Files contain dummy
strings, never serialized private keys or desktop SSH configuration.
"""
import base64
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
import uuid

from cryptography.exceptions import UnsupportedAlgorithm
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec, ed25519

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import provision_remote as provision
import lightjack_remote_build as build


def key_material(scalar=1, curve=None):
    key = ec.derive_private_key(scalar, curve or ec.SECP256R1())
    pem = key.private_bytes(serialization.Encoding.PEM,
                            serialization.PrivateFormat.TraditionalOpenSSL,
                            serialization.NoEncryption()).decode('ascii')
    public = key.public_key().public_bytes(serialization.Encoding.OpenSSH,
                                           serialization.PublicFormat.OpenSSH).decode('ascii')
    return pem, public


def valid_config():
    pem, public = key_material()
    return dict(ssid='test-bulb', wifi_password='test-only-password',
                relay_ipv4='192.0.2.10', relay_port=22, username='lightjack_test',
                private_key_pem=pem, public_key_ssh=public,
                host_sha256_hex='ab' * 32, target_ipv4='192.0.2.20',
                target_port=8000, remote_port=18080)


class ValidationTests(unittest.TestCase):
    def test_matching_pair_and_blob_fingerprint(self):
        source = valid_config()
        source['private_key_pem'] = source['private_key_pem'].replace('\n', '\r\n')
        source['public_key_ssh'] += ' ignored-comment'
        normalized, pin = provision.validate_config(source)
        self.assertEqual(normalized, valid_config())
        blob = base64.b64decode(normalized['public_key_ssh'].split()[1])
        self.assertEqual(pin, hashlib.sha256(blob).hexdigest())
        self.assertNotEqual(pin, hashlib.sha256(normalized['public_key_ssh'].encode()).hexdigest())

    def test_mismatched_pair(self):
        config = valid_config()
        config['public_key_ssh'] = key_material(2)[1]
        with self.assertRaisesRegex(ValueError, 'do not match'):
            provision.validate_config(config)

    def test_host_public_key_derives_pin_and_checks_agreement(self):
        config = valid_config()
        config['relay_host_public_key'] = key_material(2)[1]
        config.pop('host_sha256_hex')
        normalized, _ = provision.validate_config(config)
        blob = base64.b64decode(config['relay_host_public_key'].split()[1])
        expected = hashlib.sha256(blob).digest()
        self.assertEqual(normalized['host_sha256_hex'], expected.hex())
        config['host_sha256_hex'] = 'SHA256:' + base64.b64encode(expected).decode().rstrip('=')
        self.assertEqual(provision.validate_config(config)[0], normalized)
        config['host_sha256_hex'] = '00' * 32
        with self.assertRaisesRegex(ValueError, 'do not match'):
            provision.validate_config(config)

    def test_missing_host_trust(self):
        config = valid_config()
        config.pop('host_sha256_hex')
        with self.assertRaisesRegex(ValueError, 'trusted relay'):
            provision.validate_config(config)

    def test_fingerprint_formats(self):
        self.assertEqual(provision.fingerprint('AB' * 32), 'ab' * 32)
        for bad in ('', 'a' * 63, 'g' * 64, 'SHA256:!!!!', 'SHA256:AAAA', None, 42):
            with self.subTest(value=bad), self.assertRaises(ValueError):
                provision.fingerprint(bad)

    def test_rejects_other_curves_and_public_key_types(self):
        config = valid_config()
        config['private_key_pem'], config['public_key_ssh'] = key_material(curve=ec.SECP384R1())
        with self.assertRaisesRegex(ValueError, 'nistp256'):
            provision.validate_config(config)
        public = ed25519.Ed25519PrivateKey.from_private_bytes(bytes(32)).public_key()
        line = public.public_bytes(serialization.Encoding.OpenSSH, serialization.PublicFormat.OpenSSH).decode()
        with self.assertRaisesRegex(ValueError, 'nistp256'):
            provision.public_key(line, 'public_key_ssh')

    def test_rejects_bad_private_key_encodings(self):
        config = valid_config()
        pem = config['private_key_pem']
        for bad in (pem + pem, pem.replace('EC PRIVATE', 'PRIVATE'),
                    pem.replace('EC PRIVATE', 'ENCRYPTED PRIVATE'),
                    '-----BEGIN EC PRIVATE KEY-----\nQUJD\n-----END EC PRIVATE KEY-----\n',
                    pem + 'private trailing data', pem.replace('\n', '\r')):
            with self.subTest(length=len(bad)), self.assertRaises(ValueError):
                provision.validate_config(dict(config, private_key_pem=bad))

    def test_crypto_errors_are_generic_and_do_not_echo_input(self):
        for function, target in ((provision.public_key, 'load_ssh_public_key'),):
            with patch.object(serialization, target, side_effect=UnsupportedAlgorithm('secret-value')):
                with self.assertRaisesRegex(ValueError, '^public_key_ssh: invalid OpenSSH public key$'):
                    function(valid_config()['public_key_ssh'], 'public_key_ssh')
        with patch.object(serialization, 'load_pem_private_key', side_effect=UnsupportedAlgorithm('secret-value')):
            with self.assertRaisesRegex(ValueError, '^private_key_pem: invalid EC private key$'):
                provision.validate_config(valid_config())

    def test_text_byte_boundaries_nul_and_surrogates(self):
        config = valid_config()
        for name, good, bad in (('ssid', 'a' * 32, 'a' * 33),
                                 ('ssid', '\u00e9' * 16, '\u00e9' * 17),
                                 ('wifi_password', 'a' * 63, 'a' * 64),
                                 ('username', 'a' * 64, 'a' * 65)):
            with self.subTest(name=name, kind='length'):
                provision.validate_config(dict(config, **{name: good}))
                with self.assertRaises(ValueError):
                    provision.validate_config(dict(config, **{name: bad}))
        for name in ('ssid', 'wifi_password', 'username', 'private_key_pem', 'public_key_ssh'):
            for bad in ('a\x00b', '\ud800', None, 123):
                with self.subTest(name=name, kind=type(bad).__name__), self.assertRaises(ValueError):
                    provision.validate_config(dict(config, **{name: bad}))
        for name, bad in (('ssid', ''), ('wifi_password', 'short'), ('username', 'has space')):
            with self.subTest(name=name), self.assertRaises(ValueError):
                provision.validate_config(dict(config, **{name: bad}))

    def test_ports_are_integer_and_in_range(self):
        config = valid_config()
        for name, minimum in provision.PORTS.items():
            for good in (minimum, 65535):
                provision.validate_config(dict(config, **{name: good}))
            for bad in (minimum - 1, 65536, True, 22.0, '22', None):
                with self.subTest(name=name, value=bad), self.assertRaises(ValueError):
                    provision.validate_config(dict(config, **{name: bad}))

    def test_ipv4_and_schema_rejection(self):
        config = valid_config()
        for name in ('relay_ipv4', 'target_ipv4'):
            for bad in ('relay.example', '::1', '0.1.2.3', '224.1.2.3', '255.255.255.255',
                        '192.168.001.2', '1.2.3.256', 2130706433, None):
                with self.subTest(name=name, value=bad), self.assertRaises(ValueError):
                    provision.validate_config(dict(config, **{name: bad}))
        for bad in ([], None, dict(config, unexpected='field'), {k: v for k, v in config.items() if k != 'ssid'}):
            with self.assertRaises(ValueError):
                provision.validate_config(bad)

    def test_render_is_byte_exact_and_has_no_literal_injection(self):
        # Rendering is tested with non-key strings. No private material is written.
        config = valid_config()
        config['private_key_pem'] = 'NOT-A-KEY'
        config['ssid'] = 'a";\n#error injected\n\\\u00e9'
        header = provision.render_header(config)
        self.assertNotIn('#error injected', header)
        literals = re.findall(r'"((?:\\[0-7]{3})*)"', header)
        strings = [bytes(int(part, 8) for part in re.findall(r'\\([0-7]{3})', value)).decode('utf-8')
                   for value in literals]
        self.assertEqual(strings, [config[name] for name in provision.FIELDS if name not in provision.PORTS])


class FakeEnv(dict):
    def __init__(self):
        super().__init__(PIOENV='lightjack_wyze_ssh_poc', PROJECT_DIR=str(provision.REPOSITORY / 'WYZE'))
        self.appended = []

    def Append(self, **kwargs):
        self.appended.append(kwargs)


class BundleTests(unittest.TestCase):
    def setUp(self):
        # Ordinary temporary parent, not TemporaryDirectory's platform ACL mode.
        self.root = Path(tempfile.gettempdir()) / ('lightjack-public-fixtures-' + uuid.uuid4().hex)
        self.root.mkdir()
        self.bundle = self.root / 'bundle'
        self.env = FakeEnv()

    def tearDown(self):
        # Exact unique path created above, containing only public dummy fixtures.
        self.assertEqual(self.root.parent, Path(tempfile.gettempdir()))
        self.assertTrue(self.root.name.startswith('lightjack-public-fixtures-'))
        shutil.rmtree(self.root)

    def bundle_fixture(self):
        return provision.write_bundle(self.bundle, '// NOT-A-KEY\n', 'PUBLIC-TEST-FIXTURE', 'ab' * 32, 'cd' * 32)

    def test_write_and_select_bundle_without_secrets_in_flags(self):
        manifest = self.bundle_fixture()
        self.assertEqual(manifest['header_sha256'], hashlib.sha256(b'// NOT-A-KEY\n').hexdigest())
        self.assertEqual((self.bundle / 'client-public-key.txt').read_text(), 'PUBLIC-TEST-FIXTURE\n')
        self.assertTrue(build.configure_private(self.env, str(self.bundle)))
        self.assertEqual(self.env.appended, [dict(CPPDEFINES=[('LIGHTJACK_REMOTE_CONFIGURED', 1)],
                                                 CPPPATH=[str(self.bundle.resolve())])])
        with self.assertRaises(FileExistsError):
            self.bundle_fixture()

    def test_unset_empty_and_wrong_target(self):
        self.assertFalse(build.configure_private(self.env, None))
        with self.assertRaises(ValueError):
            build.configure_private(self.env, '')
        self.env['PIOENV'] = 'lightjack_wyze_poc'
        with self.assertRaises(ValueError):
            build.configure_private(self.env, str(self.bundle))
        self.assertEqual(self.env.appended, [])

    def test_repository_and_desktop_ssh_paths_rejected_before_read(self):
        for directory in (provision.REPOSITORY / 'private', Path.home() / '.ssh' / 'private'):
            with self.assertRaises(ValueError):
                provision.load_config(directory / 'config.json')
            with self.assertRaises(ValueError):
                build.configure_private(self.env, str(directory))
            with self.assertRaises(ValueError):
                provision.write_bundle(directory, 'unused', 'unused', 'unused', 'unused')
        self.assertEqual(self.env.appended, [])

    def test_missing_partial_tampered_and_oversized_bundles(self):
        with self.assertRaises(ValueError):
            build.configure_private(self.env, str(self.bundle))
        self.bundle_fixture()
        marker = self.bundle / 'manifest.json'
        original = marker.read_bytes()
        for content in (b'{', b'[]', b'{"format":true}', b'{"format":2}', b'{}', b'x' * 8193):
            marker.write_bytes(content)
            with self.subTest(length=len(content)), self.assertRaises(ValueError):
                build.configure_private(self.env, str(self.bundle))
        marker.write_bytes(original)
        (self.bundle / provision.HEADER_NAME).write_text('changed header')
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            build.configure_private(self.env, str(self.bundle))
        (self.bundle / provision.HEADER_NAME).write_bytes(b'x' * 131073)
        with self.assertRaisesRegex(ValueError, 'oversized'):
            build.configure_private(self.env, str(self.bundle))
        self.assertEqual(self.env.appended, [])

    def test_manifest_is_absent_after_interrupted_write(self):
        real_open = provision.os.open
        def fail_public(path, flags, mode):
            if Path(path).name == 'client-public-key.txt':
                raise OSError('simulated write failure')
            return real_open(path, flags, mode)
        with patch.object(provision.os, 'open', side_effect=fail_public):
            with self.assertRaises(OSError):
                self.bundle_fixture()
        self.assertFalse((self.bundle / 'manifest.json').exists())
        with self.assertRaises(ValueError):
            build.configure_private(self.env, str(self.bundle))
        self.assertEqual(self.env.appended, [])

    def test_json_duplicate_and_size_limits(self):
        config = self.root / 'public-fixture.json'
        for content in ('{"ssid":"a","ssid":"b"}', '{invalid', 'x' * 65537):
            config.write_text(content)
            with self.assertRaises(ValueError):
                provision.load_config(config)
        config.write_text('{"fixture":true}')
        self.assertEqual(provision.load_config(config), {'fixture': True})

    def test_check_cli_uses_no_writer_and_returns_only_public_fingerprints(self):
        output = io.StringIO()
        with patch.object(sys, 'argv', ['provision_remote.py', '--config', str(self.root / 'unused.json'), '--check']), \
             patch.object(provision, 'load_config', return_value=valid_config()), \
             patch.object(provision, 'write_bundle') as writer, contextlib.redirect_stdout(output):
            self.assertEqual(provision.main(), 0)
        writer.assert_not_called()
        result = json.loads(output.getvalue())
        self.assertEqual(set(result), {'valid', 'bundle_written', 'client_sha256_hex', 'relay_host_sha256_hex'})
        self.assertTrue(result['valid'])
        self.assertFalse(result['bundle_written'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
