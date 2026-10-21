/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#ifdef LIGHTJACK_WYZE_POC
#include "lightjack/lightjack_adapter.h"
#include "lightjack/lightjack_levels.h"
#include "lightjack/bulb_write.h"
#include "lightjack/lightjack_page.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static bool ready;
static bool early_ready;
static const char *TAG = "lightjack";

static esp_err_t arm_loader(void)
{
    const struct { const char *label; uint32_t address, size; } expected[] = {
        {"nvs", 0x9000, 0x4000}, {"otadata", 0xd000, 0x2000},
        {"phy_init", 0xf000, 0x1000}, {"ota_0", 0x10000, 0x1e0000},
        {"ota_1", 0x1f0000, 0x1e0000}, {"mac", 0x3d0000, 0x4000},
        {"iot", 0x3d4000, 0x4000}, {"user", 0x3d8000, 0x8000},
        {"factory_nvs", 0x3e0000, 0x6000}, {"reserved", 0x3e6000, 0x12000}
    };
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        const esp_partition_t *p = esp_partition_find_first(
            ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, expected[i].label);
        if (!p || p->address != expected[i].address || p->size != expected[i].size)
            return ESP_ERR_INVALID_STATE;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    esp_app_desc_t description;
    if (!running || !other || running->address == other->address || other->size != 0x1e0000 ||
        esp_ota_get_partition_description(other, &description) != ESP_OK ||
        strncmp(description.project_name, "wyze_loader", sizeof(description.project_name)) != 0)
        return ESP_ERR_NOT_FOUND;
    /* IDF validates the image before selecting it. This identifies its project,
     * not its publisher. The retained loader can select our app again. */
    return esp_ota_set_boot_partition(other);
}

void __real_initArduino(void);
void __wrap_initArduino(void)
{
    early_ready = false;
    esp_err_t error = arm_loader();
    if (error == ESP_OK) error = nvs_flash_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Early startup stopped without formatting NVS: %s", esp_err_to_name(error));
        for (;;) vTaskSuspend(NULL);
    }
    /* In pinned IDF 4.4.7, initialized storage returns ESP_OK immediately.
     * Arduino's following init call therefore cannot enter its autoformat path.
     * This does not prevent normal PHY or other runtime NVS writes. */
    early_ready = true;
    __real_initArduino();
}

static int write_channel(void *unused, uint8_t channel, uint16_t value)
{
    (void)unused;
    return bulb_write(channel, value);
}

esp_err_t lightjack_prepare(void)
{
    ready = false;
    if (!early_ready) return ESP_ERR_INVALID_STATE;
    esp_err_t error = bulb_write_init();
    const lightjack_levels_t off = {{0}};
    int off_error = lightjack_apply_levels(&off, write_channel, NULL);
    if (error == ESP_OK) error = off_error;
    ready = error == ESP_OK;
    ESP_LOGI(TAG, "Loader selected for next boot; lighting ready=%d", ready);
    return error;
}

static int read_body(void *request, char *buffer, size_t length)
{
    return httpd_req_recv((httpd_req_t *)request, buffer, length);
}

static esp_err_t reply(httpd_req_t *request, const char *status, const char *body)
{
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, body);
}

esp_err_t lightjack_color_handler(httpd_req_t *request)
{
    char body[LIGHTJACK_BODY_MAX + 1];
    lightjack_levels_t levels;
    int error = lightjack_receive(read_body, request, request->content_len, body);
    if (error) {
        reply(request, error == HTTPD_SOCK_ERR_TIMEOUT ? "408 Request Timeout" : "400 Bad Request",
            "{\"error\":\"Body must contain 1..384 bytes and arrive completely\"}");
        return ESP_FAIL; /* Close a session with unread body bytes. */
    }
    if (!lightjack_parse_levels(body, request->content_len, &levels))
        return reply(request, "400 Bad Request", "{\"error\":\"Invalid brightness, warm, or rgb\"}");
    if (!ready) {
        const lightjack_levels_t off = {{0}};
        lightjack_apply_levels(&off, write_channel, NULL);
        return reply(request, "503 Service Unavailable", "{\"error\":\"Driver fault; reboot to loader\"}");
    }
    error = lightjack_apply_levels(&levels, write_channel, NULL);
    if (error) {
        ready = false;
        return reply(request, "503 Service Unavailable", "{\"error\":\"I2C write failed; off attempted\"}");
    }
    return reply(request, "200 OK", "{\"ok\":true,\"maximum_channel_level\":128}");
}

esp_err_t lightjack_update_disabled(httpd_req_t *request)
{
    reply(request, "409 Conflict", "{\"error\":\"Updater disabled to preserve wyze_loader; use recovery loader\"}");
    return ESP_FAIL; /* Do not consume or act on credentials or firmware. */
}

esp_err_t lightjack_page_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, LIGHTJACK_PAGE, sizeof(LIGHTJACK_PAGE) - 1);
}
#endif
