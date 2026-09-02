#pragma once

#include <cstdint>

// Persisted in NVS (ESP32 Preferences, namespace "enmea"). Small, fixed-size
// fields only - no dynamic allocation, no std::string, so this struct can be
// copied and reused freely.
struct AppSettings {
  enum class Protocol : uint8_t { UDP = 0, TCP = 1 };

  char ssid[33] = {0};      // 802.11 SSID max is 32 chars + NUL
  char password[65] = {0};  // WPA2 PSK max is 63 chars + NUL
  char host[64] = {0};      // TCP target only; ignored in UDP mode
  uint16_t port = 10110;    // 10110 is the common NMEA-over-IP UDP port
  Protocol protocol = Protocol::UDP;

  bool hasWifiCredentials() const { return ssid[0] != '\0'; }
};

// Returns false (with `out` left default) if nothing has been saved yet.
bool loadAppSettings(AppSettings& out);
bool saveAppSettings(const AppSettings& in);
void clearAppSettings();
