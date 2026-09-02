#pragma once

#include "AppSettings.h"

// A tiny HTTP form (served over WiFiServer/WebServer, both stock
// Arduino-ESP32 classes - not a freeink-sdk or CrossInk dependency) for
// entering the WiFi SSID/password and the NMEA source host/port/protocol.
//
// v1 deliberately does not build an on-device text-entry keyboard: typing an
// SSID and WPA2 password via 5-7 buttons is bad UX even with a working input
// component. The device-side escape hatch is a button gesture that erases the
// saved settings (see main.cpp), not on-device editing.
class ProvisioningPortal {
 public:
  // Starts a SoftAP ("eNMEA-Setup", open network) and serves the form there.
  // Call when there is no saved WiFi config yet, or the saved one failed.
  void beginAsAccessPoint();

  // Serves the same form on the station connection AND keeps the setup AP up
  // alongside it (WIFI_AP_STA). The AP is the part that matters: it's what
  // makes the settings reachable when the saved config is wrong for wherever
  // the device is now - the case where a station-only portal is unreachable
  // precisely when you need it. Call after a successful WiFi.begin().
  void beginOnStation();

  // Must be called every loop() iteration while a portal is active.
  void poll();

 private:
  void setupRoutes();
};
