/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define LIGHTJACK_BODY_MAX 384
#define LIGHTJACK_LEVEL_MAX 128
#define LIGHTJACK_CHANNELS 5

#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint16_t channel[LIGHTJACK_CHANNELS]; } lightjack_levels_t;
typedef int (*lightjack_read_fn)(void *context, char *buffer, size_t length);
typedef int (*lightjack_write_fn)(void *context, uint8_t channel, uint16_t value);

/* Receive exactly length bytes. Returns zero on success, -1 on invalid size,
 * -2 on early EOF, or the receiver's negative error code. Always terminates. */
int lightjack_receive(lightjack_read_fn read, void *context, size_t length,
                      char buffer[LIGHTJACK_BODY_MAX + 1]);
/* Invalid input never changes levels. String brightness/warm use 0..1024;
 * integer RGB uses 0..255. Output is B,G,R,W1,W2 with a test cap of 128. */
bool lightjack_parse_levels(const char *body, size_t length, lightjack_levels_t *levels);
/* Turn all channels off before applying. Any write failure triggers best-effort
 * all-off. Returns the first driver error, never success after an I/O failure. */
int lightjack_apply_levels(const lightjack_levels_t *levels, lightjack_write_fn write, void *context);
#ifdef __cplusplus
}
#endif
