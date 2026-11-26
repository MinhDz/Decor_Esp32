#include "manager/DiscoveryManager.h"
#include "XiaoZhiClient.h"
#include "PcStatsManager.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#define DISCOVERY_UDP_PORT 8767
static WiFiUDP s_udpDiscovery;
static bool s_udpStarted = false;
static uint32_t s_lastBeaconMs = 0;

void DiscoveryManager::init() {
  if (s_udpStarted) return;
  if (s_udpDiscovery.begin(DISCOVERY_UDP_PORT)) {
    s_udpStarted = true;
    Serial.printf("📡 [DISCOVERY] UDP Discovery Service sẵn sàng lắng nghe cổng %d\n", DISCOVERY_UDP_PORT);
  } else {
    Serial.println("❌ [DISCOVERY] Không thể mở socket UDP cổng 8767!");
  }
}

void DiscoveryManager::sendBeaconNow() {
  if (!s_udpStarted || WiFi.status() != WL_CONNECTED) return;
  IPAddress bcast(255, 255, 255, 255);
  if (s_udpDiscovery.beginPacket(bcast, DISCOVERY_UDP_PORT)) {
    String msg = "{\"device\":\"tramvutru\",\"name\":\"DecorSpace\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"port\":80,\"ws_port\":8765}";
    s_udpDiscovery.print(msg);
    s_udpDiscovery.endPacket();
  }
}

void DiscoveryManager::loop() {
  if (WiFi.status() != WL_CONNECTED) {
    s_udpStarted = false;
    return;
  }
  if (!s_udpStarted) {
    init();
    return;
  }

  // 1. Kiểm tra gói tin Broadcast đến từ PC Companion
  int packetSize = s_udpDiscovery.parsePacket();
  if (packetSize > 0) {
    char packetBuf[384];
    int len = s_udpDiscovery.read(packetBuf, sizeof(packetBuf) - 1);
    if (len > 0) {
      packetBuf[len] = '\0';
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, packetBuf);
      if (!err) {
        const char* srv = doc["service"] | "";
        if (strcmp(srv, "decor_companion") == 0 || strcmp(srv, "DecorSpaceCompanion") == 0) {
          String pcIp = doc["ip"].is<const char*>() ? doc["ip"].as<String>() : s_udpDiscovery.remoteIP().toString();
          int gwPort = doc["gateway_port"] | 8765;
          if (pcIp.length() > 0 && pcIp != "0.0.0.0" && pcIp != "255.255.255.255") {
            XiaoZhiClient::updatePcGatewayIpDynamic(pcIp, gwPort);
            PcStatsManager::recordPcIp(s_udpDiscovery.remoteIP());
          }
        }
      }
    }
  }

  // 2. Phát Broadcast Heartbeat định kỳ mỗi 8 giây
  uint32_t now = millis();
  if (now - s_lastBeaconMs >= 8000) {
    s_lastBeaconMs = now;
    sendBeaconNow();
  }
}
