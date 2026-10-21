/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t lightjack_prepare(void);
esp_err_t lightjack_color_handler(httpd_req_t *request);
esp_err_t lightjack_update_disabled(httpd_req_t *request);
esp_err_t lightjack_page_handler(httpd_req_t *request);
#ifdef __cplusplus
}
#endif
