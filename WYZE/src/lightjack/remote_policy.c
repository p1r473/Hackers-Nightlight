/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#include "lightjack/remote_policy.h"
#include <stddef.h>
#include <string.h>

static size_t bounded_length(const char *s, size_t cap)
{
    size_t n = 0;
    if (!s) return 0;
    while (n < cap && s[n]) ++n;
    return n;
}

static bool unicast_ipv4(const char *s)
{
    if (!s || !*s) return false;
    unsigned octets[4];
    for (unsigned i = 0; i < 4; ++i) {
        unsigned value = 0, count = 0;
        const char *first = s;
        while (*s >= '0' && *s <= '9') {
            value = value * 10 + (unsigned)(*s++ - '0');
            if (++count > 3 || value > 255) return false;
        }
        if (!count || (count > 1 && *first == '0')) return false;
        octets[i] = value;
        if (i != 3) { if (*s++ != '.') return false; }
        else if (*s) return false;
    }
    return octets[0] != 0 && octets[0] < 224;
}

const char *lightjack_remote_validate(const lightjack_remote_config *c)
{
    if (!c) return "configuration missing";
    size_t n = bounded_length(c->ssid, 33);
    if (n < 1 || n > 32) return "SSID missing or too long";
    n = bounded_length(c->wifi_password, 64);
    if (n < 8 || n > 63) return "WPA2 passphrase must be 8 to 63 bytes";
    if (!unicast_ipv4(c->relay_ipv4)) return "relay must be a literal unicast IPv4 address";
    if (!unicast_ipv4(c->target_ipv4)) return "target must be a literal unicast IPv4 address";
    if (!c->relay_port || !c->target_port || c->remote_port < 1024)
        return "invalid relay, target or unprivileged remote port";
    n = bounded_length(c->username, 65);
    if (n < 1 || n > 64) return "SSH username missing or too long";
    for (size_t i = 0; i < n; ++i) {
        char ch = c->username[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-'))
            return "unsupported SSH username character";
    }
    if (bounded_length(c->host_sha256_hex, 65) != 64)
        return "server SHA256 fingerprint must contain 64 hex characters";
    for (unsigned i = 0; i < 64; ++i) {
        char ch = c->host_sha256_hex[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
              (ch >= 'A' && ch <= 'F'))) return "server fingerprint is not hex";
    }
    n = bounded_length(c->private_key_pem, 8193);
    if (n < 1 || n > 8192 ||
        strncmp(c->private_key_pem, "-----BEGIN EC PRIVATE KEY-----\n", sizeof("-----BEGIN EC PRIVATE KEY-----\n") - 1) != 0 ||
        !strstr(c->private_key_pem, "\n-----END EC PRIVATE KEY-----"))
        return "ECDSA PEM private key missing or unsupported";
    n = bounded_length(c->public_key_ssh, 1025);
    if (n < 21 || n > 1024 || strncmp(c->public_key_ssh, "ecdsa-sha2-nistp256 ", sizeof("ecdsa-sha2-nistp256 ") - 1) != 0)
        return "ECDSA p256 public key missing or unsupported";
    return NULL;
}
