/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#ifdef LIGHTJACK_REMOTE_SSH_POC
#include "lightjack/remote_policy.h"
#include <ESP-Reverse_Tunneling_Libssh2.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/task.h>
#include <cstring>

static constexpr unsigned stack_bytes = 32768;
static SSHTunnel tunnel;
static bool started;

static bool configure(const lightjack_remote_config &c)
{
    globalSSHConfig.setDebugConfig(false, 115200);
    globalSSHConfig.setLogLevel(LOG_ERROR);
    globalSSHConfig.setSSHKeyAuthFromMemory(c.relay_ipv4, c.relay_port,
        c.username, c.private_key_pem, c.public_key_ssh);
    globalSSHConfig.setHostKeyVerification(c.host_sha256_hex, "ecdsa-sha2-nistp256", true);
    globalSSHConfig.clearTunnelMappings();
    globalSSHConfig.setTunnelConfig("127.0.0.1", c.remote_port, c.target_ipv4, c.target_port);
    globalSSHConfig.setConnectionConfig(30, 10000, 5, 15);
    globalSSHConfig.setBufferConfig(1024, 1, 120000, 8192);
    globalSSHConfig.setMaxReverseListeners(1);
    globalSSHConfig.setKeepAliveOptions(true, 30);

    // Setters can fail silently if a mutex or String allocation fails. Verify
    // every field that controls authentication, destination or allocation.
    const auto &s = globalSSHConfig.getSSHConfig();
    const auto &t = globalSSHConfig.getTunnelConfig();
    const auto &b = globalSSHConfig.getConnectionConfig();
    return s.useSSHKey && s.verifyHostKey && s.host == c.relay_ipv4 &&
        s.port == c.relay_port && s.username == c.username &&
        s.privateKeyData == c.private_key_pem && s.publicKeyData == c.public_key_ssh &&
        s.password.isEmpty() && s.expectedHostKeyFingerprint == c.host_sha256_hex &&
        s.hostKeyType == "ecdsa-sha2-nistp256" && globalSSHConfig.getTunnelMappingCount() == 1 &&
        t.remoteBindHost == "127.0.0.1" && t.remoteBindPort == c.remote_port &&
        t.localHost == c.target_ipv4 && t.localPort == c.target_port &&
        b.maxChannels == 1 && b.maxReverseListeners == 1 && b.bufferSize == 1024 &&
        b.tunnelRingBufferSize == 8192 && b.connectionTimeoutSec == 15 &&
        globalSSHConfig.validateConfiguration();
}

static bool station_ready()
{
    wifi_ap_record_t ap;
    esp_netif_ip_info_t info;
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    return sta && esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
        esp_netif_get_ip_info(sta, &info) == ESP_OK && info.ip.addr != 0;
}

static void remote_tick(unsigned long &last_connect)
{
    if (station_ready()) {
        if (tunnel.getState() == TUNNEL_DISCONNECTED) tunnel.connectSSH();
        else tunnel.loop();
    } else {
        if (tunnel.getState() != TUNNEL_DISCONNECTED) tunnel.disconnect();
        if ((unsigned long)(millis() - last_connect) >= 10000) {
            last_connect = millis();
            esp_wifi_connect();
        }
    }
}

static void remote_task(void *)
{
    if (!tunnel.init()) {
        Serial.println("Lightjack SSH initialization failed; remote access remains off.");
        vTaskDelete(nullptr);
        return;
    }
    unsigned long last_connect = millis() - 10000;
    unsigned long last_report = millis();
    for (;;) {
        remote_tick(last_connect);
        if ((unsigned long)(millis() - last_report) >= 30000) {
            last_report = millis();
            Serial.printf("Lightjack SSH state=%d channels=%d rx=%lu tx=%lu heap=%u largest=%u stack-free=%u\n",
                (int)tunnel.getState(), tunnel.getActiveChannels(), tunnel.getBytesReceived(),
                tunnel.getBytesSent(),
                (unsigned)esp_get_free_heap_size(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" void lightjack_remote_start(void)
{
    if (started) return;
    const lightjack_remote_config *c = lightjack_remote_get_config();
    const char *error = lightjack_remote_validate(c);
    if (error) {
        Serial.printf("Lightjack SSH disabled: %s.\n", error);
        return;
    }
    if (!configure(*c)) {
        Serial.println("Lightjack SSH configuration rejected; no connection attempted.");
        return;
    }
    // The existing Nightlight driver owns Wi-Fi/APSTA and its interfaces.
    // Do not initialize Arduino WiFi again or overwrite the management AP.
    wifi_config_t station = {};
    memcpy(station.sta.ssid, c->ssid, strlen(c->ssid));
    memcpy(station.sta.password, c->wifi_password, strlen(c->wifi_password));
    station.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    station.sta.pmf_cfg.capable = true;
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &station) != ESP_OK) {
        Serial.println("Lightjack SSH station setup failed.");
        return;
    }
    if (xTaskCreate(remote_task, "lightjack-ssh", stack_bytes, nullptr, 1, nullptr) != pdPASS) {
        Serial.println("Lightjack SSH worker allocation failed.");
        return;
    }
    started = true;
}
#endif
