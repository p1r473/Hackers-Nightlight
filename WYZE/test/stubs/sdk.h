/* Host-only SDK doubles for Lightjack tests. GPL-3.0-only, 2026-10-21. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_PARTITION_TYPE_ANY 255
#define ESP_PARTITION_SUBTYPE_ANY 255
#define HTTPD_SOCK_ERR_TIMEOUT -3
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef struct { const char *label; uint32_t address, size; } esp_partition_t;
typedef struct { char project_name[32]; } esp_app_desc_t;
typedef struct {
    size_t content_len;
    const char *body;
    size_t offset, chunk;
    int recv_calls;
    char status[40], response[200];
} httpd_req_t;
const esp_partition_t *esp_partition_find_first(int, int, const char *);
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *, esp_app_desc_t *);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t nvs_flash_init(void);
const char *esp_err_to_name(esp_err_t);
void vTaskSuspend(void *);
int httpd_req_recv(httpd_req_t *, char *, size_t);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, int);
