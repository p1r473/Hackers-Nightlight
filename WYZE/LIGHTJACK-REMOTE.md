# Lightjack remote TCP prototype

Added 2026-10-21. This is an optional source prototype, not a working remote-access product or an installation release. Read [the bulb integration notes](LIGHTJACK.md) first.

## What is implemented

The `lightjack_wyze_ssh_poc` environment extends the existing bulb target. It reuses Playmiel's reverse-SSH library and libssh2 instead of introducing a new transport protocol or custom relay daemon. A configured bulb would make an outbound SSH connection to an OpenSSH relay. A TCP listener on that relay would forward to one explicitly configured IPv4 address and port reachable by the bulb.

The source requests a loopback-only relay listener at `127.0.0.1`. Remote clients would reach it through separately authenticated access to the relay. The relay must also enforce loopback binding and forwarding restrictions. No relay has been configured or deployed.

Configuration requires a WPA2 station network, literal relay and target IPv4 addresses, client keys, and a trusted 64-character SHA256 hex fingerprint of the relay's ECDSA p256 host key. Host-key verification occurs before client authentication. Empty pins and disabled verification are rejected. Password authentication and accepting an unknown server key are unavailable in this target. Algorithm selection is ECDH p256, ECDSA p256 host key, AES-128-CTR and HMAC-SHA256.

The application uses the existing IDF APSTA interfaces, keeps station credentials in RAM, and runs the tunnel in one worker. It attempts the initial SSH connection explicitly, delegates reconnect backoff to the library after connection failures, and closes the session when station connectivity is lost. If tunnel initialization fails, remote access stays off until reboot. The existing light controls and recovery startup guard remain present.

## Current configuration boundary

Without `LIGHTJACK_REMOTE_CONFIG_DIR`, the weak `lightjack_remote_get_config()` provider returns empty fields. The default build stops before changing station configuration, allocating the worker or opening a connection. The actual SSH implementation remains linked in the image.

The [private configuration tool](tools/provision_remote.py) validates supplied per-device credentials and creates a build header outside the source repository. It does not generate keys, contact a relay, install authorized keys, configure a service or flash a bulb. No operational SSH credentials have been provisioned for this prototype. Do not embed a desktop administration key in a bulb. Device credential issuance, relay authorization, storage and revocation still need a controlled trial.

The host tool verifies the P256 client key pair cryptographically and checks its agreement with any supplied relay public key and fingerprint. The firmware validator checks structure only; libssh2 must still parse the configured keys and the relay must authorize them. Python validation uses publicly known mathematical test keys in memory only. Firmware build-selection and adapter fixtures use invalid `NOT-A-KEY` strings and cannot authenticate.

## Private configuration workflow

Use Python 3.12 or newer with the pinned dependency in [tools/requirements-remote.txt](tools/requirements-remote.txt). The ordinary unconfigured PlatformIO build does not need this Python dependency.

The input is a private UTF-8 JSON object with these fields. Do not put it in this repository or a desktop `.ssh` directory. Obtain the relay fingerprint through a trusted channel; this tool does not discover or establish trust in an unknown relay.

| Field | Required value |
| --- | --- |
| `ssid` | Station network name, 1 to 32 UTF-8 bytes |
| `wifi_password` | WPA2 passphrase, 8 to 63 UTF-8 bytes |
| `relay_ipv4`, `target_ipv4` | Literal unicast IPv4 addresses; hostnames and IPv6 are unsupported |
| `relay_port`, `target_port` | Integer ports from 1 through 65535 |
| `remote_port` | Relay listener port from 1024 through 65535 |
| `username` | Relay account, 1 to 64 ASCII letters, digits, underscores or hyphens |
| `private_key_pem` | Dedicated client key as unencrypted traditional `EC PRIVATE KEY` PEM, curve nistp256, with JSON-escaped newlines |
| `public_key_ssh` | Matching single-line OpenSSH ECDSA nistp256 public key |
| `host_sha256_hex` | Trusted relay host-key SHA256, either 64 hexadecimal characters or OpenSSH `SHA256:` form |
| `relay_host_public_key` | Optional trusted ECDSA nistp256 host public key. May replace the fingerprint field; if both are supplied they must agree |

NUL characters, unknown or duplicate JSON fields, wrong curves, mismatched keys, encrypted or malformed PEM, invalid ports and oversized fields are rejected. Host fingerprints are calculated from the SSH binary public-key blob, not its text representation.

From `WYZE`, validate a private configuration without writing a bundle using `python tools/provision_remote.py --config PATH --check`, replacing `PATH` with the existing private JSON file. Create the bundle with `python tools/provision_remote.py --config PATH --output DIRECTORY`, choosing a new private directory outside the repository. These commands print only validation results and public-key fingerprints. The tool refuses to overwrite an existing directory. The input file and generated header contain secrets; restrict access and do not publish them. POSIX file permissions and Windows directory protection depend on the operating system and calling account; verify access permissions before using operational credentials.

Set `LIGHTJACK_REMOTE_CONFIG_DIR` to that bundle directory for the build process, then run `pio run -e lightjack_wyze_ssh_poc`. The build script verifies the header hash before enabling the configured provider. It passes only a directory and feature flag to the compiler, never credential values. The manifest is a completeness and corruption check, not a signature or authorization mechanism. Keep the directory trusted and unchanged during the build.

Configured firmware and build artifacts contain the supplied credentials in plaintext. Keep all of `.pio` private and never distribute that image as a shared kit image. Unset `LIGHTJACK_REMOTE_CONFIG_DIR` and rebuild to return to the credential-free configuration. Selecting a configuration does not upload an image or change any relay permissions.

Run `python test/test_remote_provision.py` to exercise key matching, host pins, bounds, JSON rejection, byte-exact C escaping, interrupted bundle writes and build selection. Tests do not read desktop keys, write real private keys, connect to a relay or demonstrate device operation.

## Build and host checks

Use the pinned PlatformIO Core and packages described in [LIGHTJACK.md](LIGHTJACK.md). Compile from `WYZE` with:

```sh
pio run -e lightjack_wyze_ssh_poc
```

This target uses C++17. Vendor sources and licenses are included under `third_party/lightjack-ssh`; no floating SSH dependency is downloaded. Arduino supplies the mbedTLS implementation. The same upload/erase guard applies. Do not flash generated bootloader or partition images.

With a C and C++17 compiler, the application policy and actual adapter can be exercised without a network or device:

```sh
cc -Iinclude src/lightjack/remote_policy.c test/test_remote_policy.c -o test-remote-policy
./test-remote-policy
cc -Iinclude -c src/lightjack/remote_policy.c -o remote-policy.o
c++ -std=c++17 -Itest/ssh_stubs -Iinclude test/test_remote_adapter.cpp remote-policy.o -o test-remote-adapter
./test-remote-adapter
python test/test_upload_guard.py
```

The adapter tests compile the real adapter against mocked SDK and tunnel boundaries. They check fail-closed configuration, setter failure, station/task failures, initial connection, reconnect delegation, Wi-Fi loss, and duplicate startup. They do not perform SSH or emulate a radio.

The included upstream helper tests can be run with:

```sh
python tools/test_remote_upstream.py --cc cc --cxx c++
```

Recorded evidence is in [verification/remote-results.json](verification/remote-results.json). The current image is 1,006,432 bytes in a 1,966,080-byte application slot. Reported static RAM is 40,324 bytes, which excludes heap, worker stack, packet queues and cryptographic runtime allocations.

## Transfer acceptance tools

The stdlib-only Python tools reuse the upstream deterministic binary pattern and echo-service design. They do not start Docker, configure SSH, create keys, or change a firewall. Start a temporary test service in one terminal:

```sh
python tools/lab_echo.py --port 8000 --seconds 300
```

Check that direct loopback endpoint from another terminal:

```sh
python tools/remote_tcp_check.py --port 8000 --size 1048576 --chunk 257 --cycles 3
```

This direct loopback command tests the tools only. During a later bulb trial, run the echo service at the configured target and run the checker against the actual relay listener port instead. An RFC1918 laboratory address requires `--host` and `--allow-lan` explicitly; public addresses, hostnames and all-interface binding are rejected. The server accepts `--port 0` to allocate and print a temporary port. The checker requires the actual printed port, never zero.

The checker sends and receives concurrently, handles partial sends, compares every byte and SHA256, sends a write half-close after the payload, and requires both the exact response length and receive EOF within one deadline. Corruption, truncation, extra bytes, early close, missing EOF and late completion fail with a nonzero exit code and a JSON error. `--read-delay` slows the receiver; server `--rate` limits echo bytes per second. `--timeout` bounds each checker cycle, up to 300 seconds. The echo service handles one connection at a time, closes an idle or blocked connection after a two-second socket timeout, and exits after its configured lifetime, up to one hour.

Run the fixture and command-line tests with:

```sh
python -m unittest discover -s test -p "test_remote_tcp*.py" -v
```

Results in [verification/remote-tcp-results.json](verification/remote-tcp-results.json) cover the acceptance tools against real local sockets, including deliberately faulty peers. They do not prove SSH authentication, the forwarding path, radio behavior or bulb performance. An echo alone cannot identify which device forwarded the bytes, so reports explicitly set `ssh_or_device_identity_verified` to false. Correlate a future relay test with the bulb and relay session logs, then disconnect the bulb session and verify that the relay port stops forwarding. Test wrong host pins and unauthorized keys separately.

## Desktop SSH reference test

The acceptance tools have also passed a real desktop OpenSSH reference test using an existing Windows SSH service and existing credentials. This was a same-host test, separate from the ESP32 firmware. Two authenticated SSH sessions completed five transfer cases, totaling 3,211,264 echoed payload bytes. Cases included three 1 MiB transfers, a slow receiver, an empty stream, and a 64 KiB transfer after establishing a second session. Binary equality, SHA256, write half-close and receive EOF were checked.

The SSH reverse listener and echo service were confirmed bound to `127.0.0.1`. Closing each owned SSH process removed its exact forwarding port and made connection attempts fail, while the echo endpoint still worked directly. The echo process was then stopped and its listener removal verified. SSH configuration, private key and known-hosts files were unchanged. Separate checks rejected an untrusted host identity and a connection with authentication methods disabled. The mechanism follows OpenSSH's documented [remote forwarding](https://man.openbsd.org/ssh#R) and [strict host checking](https://man.openbsd.org/ssh_config#StrictHostKeyChecking).

The reference negotiated Curve25519, an Ed25519 host key and ChaCha20-Poly1305, using an existing Ed25519 client identity. A separate attempt to select the bulb's P256 host algorithm failed because this server offers P384, RSA and Ed25519 host keys. The server was left unchanged. These results validate the desktop reference path and acceptance tools, not the bulb's selected algorithms, libssh2 implementation, authentication, throughput or recovery. A dedicated compatible relay and approved per-device credential remain necessary. See [the scoped reference evidence](verification/openssh-reference-results.json).

## Memory profile and upstream changes

- One active channel, one pending channel, one deferred-close slot and one reverse listener.
- Two 8 KiB directional rings, two 1 KiB prepend buffers and two 1 KiB transport buffers. The worker requests a 32 KiB stack.
- Advertised SSH channel window 16 KiB and maximum channel packet 4 KiB. Default socket receive/send buffer requests are 8 KiB.
- Fifteen-second TCP connect deadline and libssh2 blocking-operation timeout. These are per-operation limits, not a total session-setup deadline.
- Explicit host pin and algorithm requirements, and a thin compatibility header for the selected ESP-IDF port.
- Local package manifests replace an upstream reference to a nonexistent `libssh2_esp` tag `1.1.1`.
- Source line endings and comment/log punctuation normalized. Original notices retained.

These limits make a hardware trial plausible; they do not prove that peak memory fits. Inspect heap, largest free block and worker stack high-water readings under sustained and interrupted traffic. The upstream dropped-byte accessor is a constant zero, so it is deliberately not presented as a measured statistic.

## Work still required before claiming the POC

1. Provision approved per-device credentials with the configuration tool and validate the actual key format and host pin against a controlled relay. Host validation and build selection alone do not prove authentication.
2. Test a complete SSH session, wrong host key, rejected client key, unavailable relay and reconnect backoff.
3. Transfer known binary data in both directions and compare hashes. Exercise half-close, slow consumers, interrupted transfers, idle expiry and excess connections.
4. Verify the relay listener's actual binding and access restrictions. No external network exposure is implied by this source.
5. On a backed-up bulb, verify physical recovery, normal lighting, peak heap, stack margin, useful throughput and sustained stability. The bulbs have not yet been tested.
6. Harden the inherited shared-password, unauthenticated HTTP management before any product release. The SSH transport does not protect that separate interface.

## Sources and licenses

- Reverse tunnel: [playmiel/ESP-Reverse_Tunneling_Libssh2](https://github.com/playmiel/ESP-Reverse_Tunneling_Libssh2), commit `8b3eba99ae696046a5aeb3db6488e1005616b08a`, MIT. [Notice](third_party/lightjack-ssh/reverse-tunnel/LICENSE).
- ESP port: [playmiel/libssh2_esp](https://github.com/playmiel/libssh2_esp), commit `378f0bd47900bffacbf29cac328c6e9b5391c886`, BSD-3-Clause. [Notice](third_party/lightjack-ssh/libssh2/LICENSE-port).
- libssh2 submodule: [libssh2/libssh2](https://github.com/libssh2/libssh2), commit `455f062236fdd5deba9abb909dbe3b86463a23d8`, version `1.11.2_DEV`. This is a development snapshot, not a claim that release 1.11.2 was audited. [Notice](third_party/lightjack-ssh/libssh2/COPYING).
- Unity helper-test framework: [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity), tag `v2.6.1`, commit `cbcd08fa7de711053a3deec6339ee89cad5d2697`, MIT. [Notice](test/vendor/unity/LICENSE.txt).
- Lightjack glue and integration: GPL-3.0-only, as part of this existing GPL application. Preserve all third-party notices in a distribution.

The exact source selections and original checkout hashes are recorded in [UPSTREAM.json](third_party/lightjack-ssh/UPSTREAM.json); current vendor-file hashes are in [VENDORED.json](third_party/lightjack-ssh/VENDORED.json).
