#include <Arduino.h>
#include <components/system_manager.h>
#ifdef LIGHTJACK_REMOTE_SSH_POC
#include "lightjack/remote_policy.h"
#endif

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
  return 0;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Welcome, Booting Up...");
  SystemManager::getInstance().SetupSystem();
#ifdef LIGHTJACK_REMOTE_SSH_POC
  lightjack_remote_start();
#endif
}

void loop() {
  delay(100);
}
