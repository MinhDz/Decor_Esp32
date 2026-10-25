#include "PcStatsManager.h"

static PcMetrics currentMetrics;

void PcStatsManager::init() {
  currentMetrics.lastUpdateMillis = 0;
}

bool PcStatsManager::updateMetrics(const String& jsonString) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonString);
  if (err) {
    return false;
  }

  // CPU
  if (doc["cpu"].is<JsonObject>()) {
    if (doc["cpu"]["name"].is<const char*>()) {
      currentMetrics.cpu.name = doc["cpu"]["name"].as<String>();
    }
    if (doc["cpu"]["short_name"].is<const char*>()) {
      currentMetrics.cpu.shortName = doc["cpu"]["short_name"].as<String>();
    }
    currentMetrics.cpu.usage = doc["cpu"]["usage"] | currentMetrics.cpu.usage;
    currentMetrics.cpu.temp  = doc["cpu"]["temp"]  | currentMetrics.cpu.temp;
    currentMetrics.cpu.freq  = doc["cpu"]["freq"]  | currentMetrics.cpu.freq;
    currentMetrics.cpu.cores = doc["cpu"]["cores"] | currentMetrics.cpu.cores;
  }

  // GPU
  if (doc["gpu"].is<JsonObject>()) {
    if (doc["gpu"]["name"].is<const char*>()) {
      currentMetrics.gpu.name = doc["gpu"]["name"].as<String>();
    }
    if (doc["gpu"]["short_name"].is<const char*>()) {
      currentMetrics.gpu.shortName = doc["gpu"]["short_name"].as<String>();
    }
    currentMetrics.gpu.usage     = doc["gpu"]["usage"]      | currentMetrics.gpu.usage;
    currentMetrics.gpu.temp      = doc["gpu"]["temp"]       | currentMetrics.gpu.temp;
    currentMetrics.gpu.vramUsed  = doc["gpu"]["vram_used"]  | currentMetrics.gpu.vramUsed;
    currentMetrics.gpu.vramTotal = doc["gpu"]["vram_total"] | currentMetrics.gpu.vramTotal;
    currentMetrics.gpu.power     = doc["gpu"]["power"]      | currentMetrics.gpu.power;
  }

  // RAM
  if (doc["ram"].is<JsonObject>()) {
    currentMetrics.ram.usage   = doc["ram"]["usage"]    | currentMetrics.ram.usage;
    currentMetrics.ram.usedGb  = doc["ram"]["used_gb"]  | currentMetrics.ram.usedGb;
    currentMetrics.ram.totalGb = doc["ram"]["total_gb"] | currentMetrics.ram.totalGb;
  }

  // DISK
  if (doc["disk"].is<JsonObject>()) {
    currentMetrics.disk.usage      = doc["disk"]["usage"]      | currentMetrics.disk.usage;
    currentMetrics.disk.readSpeed  = doc["disk"]["read_speed"]  | currentMetrics.disk.readSpeed;
    currentMetrics.disk.writeSpeed = doc["disk"]["write_speed"] | currentMetrics.disk.writeSpeed;
  }

  // NET
  if (doc["net"].is<JsonObject>()) {
    currentMetrics.net.dlSpeedKb = doc["net"]["dl_speed"] | currentMetrics.net.dlSpeedKb;
    currentMetrics.net.ulSpeedKb = doc["net"]["ul_speed"] | currentMetrics.net.ulSpeedKb;
  }

  // FAN & VOLTAGE
  if (doc["fan"].is<JsonObject>()) {
    currentMetrics.fanRpm = doc["fan"]["rpm"] | currentMetrics.fanRpm;
  } else if (doc["fan_rpm"].is<int>()) {
    currentMetrics.fanRpm = doc["fan_rpm"].as<int>();
  }

  if (doc["voltage"].is<float>()) {
    currentMetrics.voltage = doc["voltage"].as<float>();
  }

  currentMetrics.lastUpdateMillis = millis();
  return true;
}

void PcStatsManager::serializeMetrics(JsonDocument& doc) {
  doc["is_live"] = isLive();
  doc["age_ms"] = currentMetrics.lastUpdateMillis > 0 ? (millis() - currentMetrics.lastUpdateMillis) : 999999;

  JsonObject cpuObj = doc["cpu"].to<JsonObject>();
  cpuObj["name"]       = currentMetrics.cpu.name;
  cpuObj["short_name"] = currentMetrics.cpu.shortName;
  cpuObj["usage"]      = currentMetrics.cpu.usage;
  cpuObj["temp"]       = currentMetrics.cpu.temp;
  cpuObj["freq"]       = currentMetrics.cpu.freq;
  cpuObj["cores"]      = currentMetrics.cpu.cores;

  JsonObject gpuObj = doc["gpu"].to<JsonObject>();
  gpuObj["name"]       = currentMetrics.gpu.name;
  gpuObj["short_name"] = currentMetrics.gpu.shortName;
  gpuObj["usage"]      = currentMetrics.gpu.usage;
  gpuObj["temp"]       = currentMetrics.gpu.temp;
  gpuObj["vram_used"]  = currentMetrics.gpu.vramUsed;
  gpuObj["vram_total"] = currentMetrics.gpu.vramTotal;
  gpuObj["power"]      = currentMetrics.gpu.power;

  JsonObject ramObj = doc["ram"].to<JsonObject>();
  ramObj["usage"]    = currentMetrics.ram.usage;
  ramObj["used_gb"]  = currentMetrics.ram.usedGb;
  ramObj["total_gb"] = currentMetrics.ram.totalGb;

  JsonObject diskObj = doc["disk"].to<JsonObject>();
  diskObj["usage"]       = currentMetrics.disk.usage;
  diskObj["read_speed"]  = currentMetrics.disk.readSpeed;
  diskObj["write_speed"] = currentMetrics.disk.writeSpeed;

  JsonObject netObj = doc["net"].to<JsonObject>();
  netObj["dl_speed"] = currentMetrics.net.dlSpeedKb;
  netObj["ul_speed"] = currentMetrics.net.ulSpeedKb;

  doc["fan_rpm"] = currentMetrics.fanRpm;
  doc["voltage"] = currentMetrics.voltage;
}

bool PcStatsManager::isLive() {
  if (currentMetrics.lastUpdateMillis == 0) return false;
  return (millis() - currentMetrics.lastUpdateMillis) < 6000; // Coi là trực tuyến nếu nhận dữ liệu trong 6 giây gần nhất
}

const PcMetrics& PcStatsManager::getMetrics() {
  return currentMetrics;
}

