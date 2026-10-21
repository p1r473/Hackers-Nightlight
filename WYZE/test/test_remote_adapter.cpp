/* Lightjack additions, 2026-10-21. SPDX-License-Identifier: GPL-3.0-only */
#define LIGHTJACK_REMOTE_SSH_POC
#include "../src/lightjack/remote_ssh.cpp"
#include <cassert>

static unsigned checks;
static lightjack_remote_config current;
extern "C" const lightjack_remote_config *lightjack_remote_get_config() { return &current; }
static void check(bool condition) { ++checks; assert(condition); }
static void reset()
{
    current = {"isolated-lab", "test-passphrase", "192.0.2.1", 22, "lightjack_lab",
        "-----BEGIN EC PRIVATE KEY-----\nNOT-A-KEY\n-----END EC PRIVATE KEY-----\n",
        "ecdsa-sha2-nistp256 NOT-A-KEY",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "192.0.2.2", 8000, 18080};
    started = false; tunnel = SSHTunnel(); globalSSHConfig = SSHConfiguration();
    now_ms = 0; station_connects = tasks_created = configured_storage = configured_station = 0;
    sta_exists = true; associated = ip_ready = config_failure = task_failure = setter_failure = false;
}
int main()
{
    reset(); current = {}; lightjack_remote_start();
    check(!started && tasks_created == 0 && configured_station == 0 && station_connects == 0);
    reset(); current.host_sha256_hex = ""; lightjack_remote_start();
    check(tasks_created == 0 && configured_storage == 0);
    reset(); current.private_key_pem = ""; lightjack_remote_start(); check(tasks_created == 0);
    reset(); current.target_ipv4 = "example.com"; lightjack_remote_start(); check(tasks_created == 0);
    reset(); setter_failure = true; lightjack_remote_start();
    check(tasks_created == 0 && configured_station == 0);
    reset(); config_failure = true; lightjack_remote_start(); check(!started && tasks_created == 0);
    reset(); task_failure = true; lightjack_remote_start(); check(!started && station_connects == 0);
    reset(); lightjack_remote_start(); check(started && tasks_created == 1 && task_stack == 32768);
    check(recorded_station.sta.threshold.authmode == WIFI_AUTH_WPA2_PSK);
    check(std::memcmp(recorded_station.sta.ssid, "isolated-lab", 12) == 0);
    check(std::memcmp(recorded_station.sta.password, "test-passphrase", 15) == 0);
    check(globalSSHConfig.getTunnelConfig().remoteBindHost == "127.0.0.1");
    check(globalSSHConfig.getTunnelConfig().localHost == "192.0.2.2");
    check(globalSSHConfig.getSSHConfig().verifyHostKey && globalSSHConfig.getSSHConfig().useSSHKey);
    lightjack_remote_start(); check(tasks_created == 1);

    unsigned long last_connect = 0;
    now_ms = 9999; remote_tick(last_connect); check(station_connects == 0 && tunnel.connects == 0);
    now_ms = 10000; remote_tick(last_connect); check(station_connects == 1 && tunnel.connects == 0);
    now_ms = 10001; remote_tick(last_connect); check(station_connects == 1);
    associated = true; remote_tick(last_connect); check(tunnel.connects == 0);
    ip_ready = true; remote_tick(last_connect); check(tunnel.connects == 1 && tunnel.loops == 0);
    remote_tick(last_connect); check(tunnel.connects == 1 && tunnel.loops == 1);
    tunnel.state = TUNNEL_ERROR; remote_tick(last_connect); check(tunnel.loops == 2 && tunnel.connects == 1);
    associated = false; remote_tick(last_connect); check(tunnel.disconnects == 1);
    remote_tick(last_connect); check(tunnel.disconnects == 1);
    associated = true; remote_tick(last_connect); check(tunnel.connects == 2);
    sta_exists = false; remote_tick(last_connect); check(tunnel.disconnects == 2);
    std::printf("%u actual-adapter checks passed with SDK/tunnel mocks\n", checks);
    return 0;
}
