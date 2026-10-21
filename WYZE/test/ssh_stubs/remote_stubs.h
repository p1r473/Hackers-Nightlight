/* Lightjack host-test boundary, 2026-10-21. GPL-3.0-only. No sockets or radios. */
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using String = std::string;
enum { LOG_ERROR, WIFI_AUTH_WPA2_PSK, WIFI_STORAGE_RAM, WIFI_IF_STA, MALLOC_CAP_8BIT };
enum TunnelState { TUNNEL_DISCONNECTED, TUNNEL_CONNECTING, TUNNEL_CONNECTED, TUNNEL_ERROR };
inline unsigned long now_ms;
inline unsigned station_connects, tasks_created;
inline bool sta_exists = true, associated, ip_ready, config_failure, task_failure, setter_failure;
inline unsigned configured_storage, configured_station;
inline unsigned task_stack;
inline void (*task_entry)(void *);
inline unsigned long millis() { return now_ms; }
struct TestSerial {
    void println(const char *) {}
    template<typename... Args> void printf(const char *, Args...) {}
};
inline TestSerial Serial;
constexpr int ESP_OK = 0, pdPASS = 1;
using esp_netif_t = int;
struct wifi_ap_record_t {};
struct esp_netif_ip_info_t { struct { uint32_t addr; } ip; };
struct wifi_config_t { struct { uint8_t ssid[32], password[64];
    struct { int authmode; } threshold; struct { bool capable; } pmf_cfg; } sta; };
inline wifi_config_t recorded_station;
inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    static int netif;
    return sta_exists && std::strcmp(key, "WIFI_STA_DEF") == 0 ? &netif : nullptr;
}
inline int esp_wifi_sta_get_ap_info(wifi_ap_record_t *) { return associated ? ESP_OK : -1; }
inline int esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *info) {
    info->ip.addr = ip_ready ? 1 : 0; return ESP_OK;
}
inline int esp_wifi_set_storage(int) { ++configured_storage; return config_failure ? -1 : ESP_OK; }
inline int esp_wifi_set_config(int, const wifi_config_t *config) {
    ++configured_station; recorded_station = *config; return config_failure ? -1 : ESP_OK;
}
inline int esp_wifi_connect() { ++station_connects; return ESP_OK; }
inline unsigned esp_get_free_heap_size() { return 100000; }
inline unsigned heap_caps_get_largest_free_block(int) { return 50000; }
inline unsigned uxTaskGetStackHighWaterMark(void *) { return 16000; }
inline void vTaskDelay(unsigned) {}
inline void vTaskDelete(void *) {}
inline unsigned pdMS_TO_TICKS(unsigned value) { return value; }
inline int xTaskCreate(void (*entry)(void *), const char *, unsigned stack, void *, int, void *) {
    task_entry = entry; task_stack = stack; ++tasks_created; return task_failure ? 0 : pdPASS;
}
struct TestString : std::string {
    using std::string::string;
    using std::string::operator=;
    bool isEmpty() const { return empty(); }
};
struct SSHServerConfig {
    TestString host, username, privateKeyData, publicKeyData, password, expectedHostKeyFingerprint, hostKeyType;
    int port = 22; bool useSSHKey = false, verifyHostKey = false;
};
struct TunnelConfig { TestString remoteBindHost, localHost; int remoteBindPort, localPort; };
struct ConnectionConfig {
    int maxChannels = 10, maxReverseListeners = 1, bufferSize = 8192, connectionTimeoutSec = 30;
    size_t tunnelRingBufferSize = 65536;
};
struct SSHConfiguration {
    SSHServerConfig server;
    std::vector<TunnelConfig> mappings;
    ConnectionConfig buffers;
    void setDebugConfig(bool, int) {}
    void setLogLevel(int) {}
    void setSSHKeyAuthFromMemory(const char *host, int port, const char *user, const char *priv, const char *pub) {
        if (setter_failure) return;
        server.host = host; server.port = port; server.username = user;
        server.privateKeyData = priv; server.publicKeyData = pub; server.password = ""; server.useSSHKey = true;
    }
    void setHostKeyVerification(const char *pin, const char *type, bool enabled) {
        if (setter_failure) return;
        server.expectedHostKeyFingerprint = pin; server.hostKeyType = type; server.verifyHostKey = enabled;
    }
    void clearTunnelMappings() { mappings.clear(); }
    void setTunnelConfig(const char *bind, int port, const char *target, int target_port) {
        TunnelConfig mapping; mapping.remoteBindHost = bind; mapping.remoteBindPort = port;
        mapping.localHost = target; mapping.localPort = target_port; mappings.push_back(mapping);
    }
    void setConnectionConfig(int, int, int, int timeout) { buffers.connectionTimeoutSec = timeout; }
    void setBufferConfig(int pump, int channels, int, size_t rings) {
        buffers.bufferSize = pump; buffers.maxChannels = channels; buffers.tunnelRingBufferSize = rings;
    }
    void setMaxReverseListeners(int listeners) { buffers.maxReverseListeners = listeners; }
    void setKeepAliveOptions(bool, int) {}
    const SSHServerConfig &getSSHConfig() const { return server; }
    const TunnelConfig &getTunnelConfig() const { return mappings.at(0); }
    const ConnectionConfig &getConnectionConfig() const { return buffers; }
    size_t getTunnelMappingCount() const { return mappings.size(); }
    bool validateConfiguration() const { return true; }
};
inline SSHConfiguration globalSSHConfig;
struct SSHTunnel {
    TunnelState state = TUNNEL_DISCONNECTED;
    unsigned connects = 0, loops = 0, disconnects = 0;
    bool init() { return true; }
    TunnelState getState() const { return state; }
    bool connectSSH() { ++connects; state = TUNNEL_CONNECTED; return true; }
    void loop() { ++loops; }
    void disconnect() { ++disconnects; state = TUNNEL_DISCONNECTED; }
    int getActiveChannels() const { return 0; }
    unsigned long getBytesReceived() const { return 0; }
    unsigned long getBytesSent() const { return 0; }
};
