/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#include "lightjack/lightjack_levels.h"
#include "main/cJSON.h"
#include <string.h>

int lightjack_receive(lightjack_read_fn read, void *context, size_t length,
                      char buffer[LIGHTJACK_BODY_MAX + 1])
{
    size_t offset = 0;
    if (!read || !buffer) return -1;
    buffer[0] = '\0';
    if (!length || length > LIGHTJACK_BODY_MAX) return -1;
    while (offset < length) {
        int count = read(context, buffer + offset, length - offset);
        if (count <= 0) { buffer[offset] = '\0'; return count ? count : -2; }
        if ((size_t)count > length - offset) { buffer[offset] = '\0'; return -1; }
        offset += (size_t)count;
    }
    buffer[offset] = '\0';
    return 0;
}

static bool decimal_string(const cJSON *item, unsigned *value)
{
    if (!cJSON_IsString(item) || !item->valuestring || !item->valuestring[0]) return false;
    const char *text = item->valuestring;
    unsigned result = 0, digits = 0;
    while (*text) {
        if (++digits > 4 || *text < '0' || *text > '9') return false;
        result = result * 10 + (unsigned)(*text++ - '0');
        if (result > 1024) return false;
    }
    *value = result;
    return true;
}

static bool rgb_number(const cJSON *item, unsigned *value)
{
    if (!cJSON_IsNumber(item) || !(item->valuedouble >= 0 && item->valuedouble <= 255) ||
        item->valuedouble != item->valueint) return false;
    *value = (unsigned)item->valueint;
    return true;
}

static bool unique_keys(const cJSON *object, const char *const *keys, unsigned count)
{
    unsigned seen = 0;
    if (!cJSON_IsObject(object)) return false;
    for (const cJSON *item = object->child; item; item = item->next) {
        unsigned index;
        for (index = 0; index < count; ++index) {
            if (item->string && strcmp(item->string, keys[index]) == 0) break;
        }
        if (index == count || (seen & (1U << index))) return false;
        seen |= 1U << index;
    }
    return true;
}

bool lightjack_parse_levels(const char *body, size_t length, lightjack_levels_t *levels)
{
    char buffer[LIGHTJACK_BODY_MAX + 1];
    const char *const keys[] = {"brightness", "warm", "rgb"};
    const char *const rgb_keys[] = {"r", "g", "b"};
    lightjack_levels_t candidate = {{0}};
    unsigned brightness, warm = 0, red = 0, green = 0, blue = 0;
    bool valid = false;
    /* The UI sends plain ASCII field names and decimal strings. Reject escape
     * sequences so decoded NULs cannot truncate a number or a field name. */
    if (!body || !levels || !length || length > LIGHTJACK_BODY_MAX ||
        memchr(body, 0, length) || memchr(body, '\\', length)) return false;
    memcpy(buffer, body, length);
    buffer[length] = '\0';
    cJSON *root = cJSON_ParseWithLengthOpts(buffer, length + 1, NULL, true);
    if (!root) return false;
    cJSON *warm_item = cJSON_GetObjectItemCaseSensitive(root, "warm");
    cJSON *rgb = cJSON_GetObjectItemCaseSensitive(root, "rgb");
    if (!unique_keys(root, keys, 3) ||
        !decimal_string(cJSON_GetObjectItemCaseSensitive(root, "brightness"), &brightness) ||
        (warm_item && !decimal_string(warm_item, &warm))) goto done;
    /* Validate RGB whenever supplied, including in white mode. */
    if (rgb && (!unique_keys(rgb, rgb_keys, 3) ||
        !rgb_number(cJSON_GetObjectItemCaseSensitive(rgb, "r"), &red) ||
        !rgb_number(cJSON_GetObjectItemCaseSensitive(rgb, "g"), &green) ||
        !rgb_number(cJSON_GetObjectItemCaseSensitive(rgb, "b"), &blue))) goto done;
    if (warm) {
        /* Preserve the upstream payload's independent white intensities.
         * Physical warm/cold assignments await measurement. */
        candidate.channel[3] = (uint16_t)(warm * LIGHTJACK_LEVEL_MAX / 1024);
        candidate.channel[4] = (uint16_t)(brightness * LIGHTJACK_LEVEL_MAX / 1024);
    } else {
        if (!rgb) goto done;
        candidate.channel[0] = (uint16_t)(blue * brightness * LIGHTJACK_LEVEL_MAX / (255U * 1024U));
        candidate.channel[1] = (uint16_t)(green * brightness * LIGHTJACK_LEVEL_MAX / (255U * 1024U));
        candidate.channel[2] = (uint16_t)(red * brightness * LIGHTJACK_LEVEL_MAX / (255U * 1024U));
    }
    *levels = candidate;
    valid = true;
done:
    cJSON_Delete(root);
    return valid;
}

static int all_off(lightjack_write_fn write, void *context)
{
    int first_error = 0;
    for (uint8_t channel = 0; channel < LIGHTJACK_CHANNELS; ++channel) {
        int error = write(context, channel, 0);
        if (error && !first_error) first_error = error;
    }
    return first_error;
}

int lightjack_apply_levels(const lightjack_levels_t *levels, lightjack_write_fn write, void *context)
{
    if (!levels || !write) return -1;
    for (unsigned channel = 0; channel < LIGHTJACK_CHANNELS; ++channel) {
        if (levels->channel[channel] > LIGHTJACK_LEVEL_MAX) return -1;
    }
    int error = all_off(write, context);
    if (error) return error;
    for (uint8_t channel = 0; channel < LIGHTJACK_CHANNELS; ++channel) {
        if (!levels->channel[channel]) continue;
        error = write(context, channel, levels->channel[channel]);
        if (error) { all_off(write, context); return error; }
    }
    return 0;
}
