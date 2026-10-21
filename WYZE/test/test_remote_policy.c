/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#include "lightjack/remote_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static void check(bool condition) { ++checks; assert(condition); }
/* Structurally valid fixture, deliberately not a cryptographic key pair. */
static lightjack_remote_config valid(void)
{
    lightjack_remote_config c = {
        "isolated-lab", "test-passphrase", "192.0.2.1", 22, "lightjack_lab",
        "-----BEGIN EC PRIVATE KEY-----\nNOT-A-KEY\n-----END EC PRIVATE KEY-----\n",
        "ecdsa-sha2-nistp256 NOT-A-KEY",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "192.0.2.2", 8000, 18080
    };
    return c;
}

int main(void)
{
    lightjack_remote_config c = valid();
    check(lightjack_remote_validate(&c) == NULL);
    check(lightjack_remote_validate(NULL) != NULL);
    lightjack_remote_config empty = {0};
    check(lightjack_remote_validate(&empty) != NULL);

    const char *bad_ips[] = {NULL, "", "0.0.0.0", "224.1.2.3", "255.255.255.255",
        "localhost", "192.0.2.1.example", "192.0.2.1 ", " 192.0.2.1", "192.0.2.1:80",
        "192.0.2", "192.0.2.1.2", "192.0.2.256", "192.00.2.1", "0x7f.0.0.1",
        "127.1", "::1", "192.-1.2.3", "192.0.2.", ".192.0.2.1", "1..2.3", "1.2.3.4\n"};
    for (unsigned i = 0; i < sizeof(bad_ips) / sizeof(bad_ips[0]); ++i) {
        c = valid(); c.relay_ipv4 = bad_ips[i];
        check(lightjack_remote_validate(&c) != NULL);
        c = valid(); c.target_ipv4 = bad_ips[i];
        check(lightjack_remote_validate(&c) != NULL);
    }
    const char *good_ips[] = {"127.0.0.1", "10.1.2.3", "172.16.0.1", "192.168.1.1", "223.1.2.3"};
    for (unsigned i = 0; i < sizeof(good_ips) / sizeof(good_ips[0]); ++i) {
        c = valid(); c.target_ipv4 = good_ips[i];
        check(lightjack_remote_validate(&c) == NULL);
    }
    c = valid(); c.relay_port = 0; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.target_port = 0; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.remote_port = 1023; check(lightjack_remote_validate(&c) != NULL);
    c.remote_port = 1024; check(lightjack_remote_validate(&c) == NULL);
    c.remote_port = 65535; check(lightjack_remote_validate(&c) == NULL);

    const char *bad_pins[] = {NULL, "", "SHA256:abcdef", "abcdef", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdefg",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef ",
        "g123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    for (unsigned i = 0; i < sizeof(bad_pins) / sizeof(bad_pins[0]); ++i) {
        c = valid(); c.host_sha256_hex = bad_pins[i];
        check(lightjack_remote_validate(&c) != NULL);
    }
    char pin[65]; strcpy(pin, valid().host_sha256_hex);
    for (unsigned i = 0; i < 64; ++i) {
        char old = pin[i]; pin[i] = 'x'; c = valid(); c.host_sha256_hex = pin;
        check(lightjack_remote_validate(&c) != NULL); pin[i] = old;
    }
    c = valid(); c.host_sha256_hex = "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789";
    check(lightjack_remote_validate(&c) == NULL);
    c = valid(); c.private_key_pem = NULL; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.private_key_pem = "-----BEGIN OPENSSH PRIVATE KEY-----\nexample";
    check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.private_key_pem = "-----BEGIN EC PRIVATE KEY-----\nwithout-end";
    check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.public_key_ssh = NULL; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.public_key_ssh = "ssh-ed25519 example"; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.public_key_ssh = "ecdsa-sha2-nistp256Xexample"; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.username = "user\ncommand"; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.username = ""; check(lightjack_remote_validate(&c) != NULL);

    char text[8194]; memset(text, 'a', sizeof(text)); text[sizeof(text)-1] = 0;
    c = valid(); c.private_key_pem = text; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.public_key_ssh = text; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.username = text; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.ssid = text; check(lightjack_remote_validate(&c) != NULL);
    text[32] = 0; c.ssid = text; check(lightjack_remote_validate(&c) == NULL);
    text[32] = 'a'; text[33] = 0; check(lightjack_remote_validate(&c) != NULL);
    c = valid(); c.wifi_password = "1234567"; check(lightjack_remote_validate(&c) != NULL);
    c.wifi_password = "12345678"; check(lightjack_remote_validate(&c) == NULL);
    memset(text, 'a', 64); text[64] = 0; c.wifi_password = text;
    check(lightjack_remote_validate(&c) != NULL);
    text[63] = 0; check(lightjack_remote_validate(&c) == NULL);
    printf("%u remote configuration policy checks passed\n", checks);
    return 0;
}
