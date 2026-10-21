/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#ifndef LIGHTJACK_REMOTE_POLICY_H
#define LIGHTJACK_REMOTE_POLICY_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char *ssid;
    const char *wifi_password;
    const char *relay_ipv4;
    uint16_t relay_port;
    const char *username;
    const char *private_key_pem;
    const char *public_key_ssh;
    const char *host_sha256_hex;
    const char *target_ipv4;
    uint16_t target_port;
    uint16_t remote_port;
} lightjack_remote_config;

/* Returns a non-secret reason, or NULL for a structurally valid configuration.
 * This does not validate the cryptographic key pair or authorize the target. */
const char *lightjack_remote_validate(const lightjack_remote_config *config);
const lightjack_remote_config *lightjack_remote_get_config(void);
void lightjack_remote_start(void);
#ifdef __cplusplus
}
#endif
#endif
