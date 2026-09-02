#pragma once

#include "AppSettings.h"
#include "Product.h"

// The setup access point's name and address.
//
// Deliberately NOT the ESP32 default of 192.168.4.1. Marine Wi-Fi gateways are
// overwhelmingly ESP32-based and sit on exactly that address in their own AP
// mode - the ONWA KC-2W, for one. If eNMEA joins such a gateway's access point
// while hosting its own on the same subnet, the device ends up with two
// interfaces on 192.168.4.0/24 and the address it must reach the gateway on is
// its own AP address: it would connect to itself and report a source failure
// with everything apparently configured correctly. Sitting on a different
// subnet makes eNMEA a good citizen with any gateway, not just that one.
// Derived from the product name, so setting SETUP_PRODUCT_NAME is enough for a
// project to name both its access point and its setup page. Override
// SETUP_AP_NAME directly only if the two need to differ.
#ifndef SETUP_AP_NAME
#define SETUP_AP_NAME SETUP_PRODUCT_NAME "-Setup"
#endif
inline constexpr const char* SETUP_AP_SSID = SETUP_AP_NAME;
inline constexpr const char* SETUP_AP_IP = "192.168.7.1";

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
  // Starts a SoftAP (SETUP_AP_SSID, open network) and serves the form there.
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
