/* Lightjack packaging, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
/* The selected ESP-IDF port exposes libssh2.h directly. The tunnel's Arduino
 * include name expects this thin compatibility header. No second TLS backend. */
#ifndef LIGHTJACK_LIBSSH2_ESP_H
#define LIGHTJACK_LIBSSH2_ESP_H
#include "libssh2.h"
#endif
