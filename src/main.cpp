// eNMEA - NMEA 0183 over Wi-Fi verification tool for Xteink X3/X4 hardware.
// See README.md for what's verified vs. assumed about the freeink-sdk API,
// and for the bring-up checklist to run through on first flash.

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <InputManager.h>
#include <WiFi.h>

#include <cstdio>

#include "PowerControl.h"
#include "net/NmeaSource.h"
#include "nmea/NmeaTypes.h"
#include "settings/AppSettings.h"
#include "settings/ProvisioningPortal.h"
#include "ui/Dashboard.h"
#include "ui/EinkCanvas.h"

namespace {

constexpr unsigned long DASHBOARD_REFRESH_MS = 2000;
// Every Nth partial (FAST_REFRESH) redraw is instead a HALF_REFRESH repaint,
// to clear the faint ghosting fast-refresh partial updates leave behind on
// e-ink over time. ~60s at the 2s cadence above.
constexpr int FULL_REPAINT_EVERY_N_UPDATES = 30;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

// Button gestures. Both are holds, not taps: a tap is too easy to trigger by
// accident on a device whose only feedback is a 2-second e-ink redraw.
constexpr unsigned long POWER_OFF_HOLD_MS = 2000;
constexpr unsigned long SETTINGS_ERASE_HOLD_MS = 3000;
// Show "keep holding" this far into a hold, so neither gesture is a silent
// trap and neither fires without warning.
constexpr unsigned long HOLD_FEEDBACK_MS = 500;

EinkCanvas canvas;
Dashboard dashboard(canvas);
NmeaSource nmeaSource;
ProvisioningPortal portal;
InputManager input;
BatteryMonitor battery;
AppSettings settings;
NmeaData nmeaData;
SentenceTable sentenceTable;
// True while only the setup portal is running (no saved config yet, or the
// saved config's Wi-Fi failed to connect). Set once in setup() rather than
// re-reading NVS every loop() tick.
bool provisioningOnly = true;

// Last good battery reading. The X3's BQ27220 is on I2C and a read can fail
// transiently, which must not blank the indicator - readStatus()'s per-field
// "known" flags are what distinguish a real 0% from a failed read.
uint16_t batteryPct = 0;
bool batteryCharging = false;
bool batteryKnown = false;

unsigned long backHoldStartMs = 0;
bool powerHintShown = false;
bool backHintShown = false;
// The power button is how the device is woken, and wake is a full chip reset.
// If the user is still holding it when loop() first runs, the shutdown gesture
// would fire on the press that just turned the device on. Arm the gesture only
// after the button has been seen released once.
bool powerGestureArmed = false;

// Formats the battery indicator, or an empty string when this board has no
// battery telemetry at all. Polled once per dashboard redraw - one I2C
// transaction every 2 seconds, which the gauge is entirely happy with.
void formatBattery(char* out, size_t outLen) {
  const BatteryMonitor::Status s = battery.readStatus();
  if (s.supported && s.percentageKnown) {
    batteryPct = s.percentage;
    batteryKnown = true;
    if (s.chargingKnown) batteryCharging = s.charging;
  }
  if (!batteryKnown) {
    out[0] = '\0';
    return;
  }
  std::snprintf(out, outLen, "%s %u%%", batteryCharging ? "CHRG" : "BATT", static_cast<unsigned>(batteryPct));
}

void showHoldBanner(const char* message) {
  dashboard.drawStatusMessage(message);
  canvas.present(EInkDisplay::FAST_REFRESH);
}

void eraseSettingsAndReboot() {
  Serial.println("[eNMEA] BACK held - erasing saved settings and rebooting into setup mode");
  clearAppSettings();
  canvas.clear();
  canvas.drawText(24, 60, "SETTINGS ERASED", 3, true);
  canvas.drawText(24, 120, "REBOOTING INTO SETUP MODE", 1, true);
  canvas.drawText(24, 140, "JOIN WIFI: ENMEA-SETUP", 1, true);
  canvas.present(EInkDisplay::HALF_REFRESH);
  delay(500);
  ESP.restart();
}

// Runs first in loop(), in every mode - the device has to be switchable off
// and resettable whether it reached the dashboard or is sitting in setup mode.
void handleGestures() {
  input.update();

  // Power button held -> shut down. getPowerButtonHeldTime() counts up live
  // while the button is down (verified in the SDK's InputManager.cpp), so this
  // fires mid-hold rather than on release.
  if (!input.isPowerButtonPressed()) {
    powerGestureArmed = true;  // released at least once - the gesture is live now
    powerHintShown = false;
  } else if (powerGestureArmed) {
    const unsigned long held = input.getPowerButtonHeldTime();
    if (held >= POWER_OFF_HOLD_MS) {
      powerOff(canvas);  // does not return
    }
    if (held >= HOLD_FEEDBACK_MS && !powerHintShown) {
      powerHintShown = true;
      showHoldBanner("KEEP HOLDING TO SHUT DOWN");
    }
  }

  // BACK held -> erase settings, reboot to the setup AP. Timed here rather
  // than with getHeldTime(), whose documented span is first-press to
  // final-release - not the "still held right now" this gesture needs.
  if (input.isPressed(InputManager::BTN_BACK)) {
    if (backHoldStartMs == 0) backHoldStartMs = millis();
    const unsigned long held = millis() - backHoldStartMs;
    if (held >= SETTINGS_ERASE_HOLD_MS) {
      eraseSettingsAndReboot();  // does not return
    }
    if (held >= HOLD_FEEDBACK_MS && !backHintShown) {
      backHintShown = true;
      showHoldBanner("KEEP HOLDING TO ERASE SETTINGS");
    }
  } else {
    backHoldStartMs = 0;
    backHintShown = false;
  }
}

void drawSetupScreen(const char* headline) {
  canvas.clear();
  canvas.drawText(20, 40, headline, 3, true);
  canvas.drawText(20, 90, "CONNECT TO WIFI:", 1, true);
  canvas.drawText(20, 105, "eNMEA-Setup", 2, true);
  canvas.drawText(20, 135, "THEN BROWSE TO 192.168.4.1", 1, true);
  canvas.drawText(20, canvas.height() - 14, "HOLD POWER 2S: SHUT DOWN", 1, true);
  canvas.present(EInkDisplay::HALF_REFRESH);
}

bool connectToWifi() {
  // WiFi.status() alone only says "not connected", not why - hook the
  // ESP-IDF disconnect event for the actual 802.11 reason code (wrong
  // password vs. AP not found vs. something else). Diagnostic only, not
  // acted on programmatically.
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        Serial.printf("[eNMEA] WiFi disconnect reason=%u\n", info.wifi_sta_disconnected.reason);
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // This board previously ran CrossInk as an e-reader, which explicitly
  // calls WiFi.persistent(false) to "suppress SDK NVS auto-connect" (see
  // /home/chuck/CrossInk/src/activities/network/WifiSelectionActivity.cpp) -
  // it manages its own credential store rather than trusting the ESP-IDF
  // WiFi driver's own internal NVS blob (a separate partition from this
  // project's own AppSettings/Preferences storage). A prior boot's Wi-Fi
  // config sitting in that blob is a plausible reason WiFi.begin() here
  // could behave differently than the exact same radio/AP under CrossInk.
  // Mirror the same defensive sequence: don't persist, explicitly clear any
  // pending connection state before (re)connecting, and scan every channel
  // rather than stopping at the first SSID match.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false, 1000);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  Serial.println("[eNMEA] Scanning for nearby APs...");
  const int found = WiFi.scanNetworks();
  if (found < 0) {
    Serial.printf("[eNMEA] WiFi.scanNetworks() failed, code=%d\n", found);
  } else {
    Serial.printf("[eNMEA] Scan found %d network(s):\n", found);
    bool sawExactMatch = false;
    for (int i = 0; i < found; ++i) {
      const String ssid = WiFi.SSID(i);
      const bool exact = ssid == settings.ssid;
      sawExactMatch = sawExactMatch || exact;
      // SSID wrapped in brackets and length printed alongside it so a
      // trailing space or other invisible character is visible in the log
      // instead of silently looking identical to the configured SSID.
      Serial.printf("  [%2d] ssid='%s' (len=%u) rssi=%d ch=%d enc=%d%s\n", i, ssid.c_str(),
                    static_cast<unsigned>(ssid.length()), WiFi.RSSI(i), WiFi.channel(i),
                    static_cast<int>(WiFi.encryptionType(i)), exact ? "  <-- MATCHES configured SSID" : "");
    }
    if (!sawExactMatch) {
      Serial.printf("[eNMEA] WARNING: configured SSID '%s' not present in the scan above.\n", settings.ssid);
    }
  }
  WiFi.scanDelete();

  WiFi.begin(settings.ssid, settings.password);
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(200);
  }
  const wl_status_t status = WiFi.status();
  // Numeric status disambiguates *why* it failed - e.g. WL_NO_SSID_AVAIL (1)
  // means the SSID was never seen (often a 2.4GHz-vs-5GHz mismatch, since
  // the ESP32-C3 radio is 2.4GHz-only), WL_CONNECT_FAILED (4) usually means
  // a wrong password, vs. WL_IDLE_STATUS/WL_DISCONNECTED just meaning it
  // never got far enough to know.
  Serial.printf("[eNMEA] WiFi.status() = %d\n", static_cast<int>(status));
  return status == WL_CONNECTED;
}

}  // namespace

void setup() {
  Serial.begin(115200);

  // Before anything else touches a pin. Two separate hazards:
  //  - On X4 revisions that don't self-latch, the board powers off the moment
  //    the power button is released unless holdPowerRails() asserts the latch.
  //  - A previous power-off (this firmware's or CrossInk's) leaves GPIO holds
  //    that survive a reset and silently defeat later writes to those pins.
  //    Both helpers call gpio_hold_dis() first, which is the only way out.
  BoardConfig::holdPowerRails();
  // The X3's SD rail (GPIO13) shares the display SPI bus: left held off, the
  // unpowered card clamps SCLK/MOSI and the panel never hears a command. This
  // project has no SD support of its own, so nothing else would release it.
  BoardConfig::releaseSdRail();

  canvas.begin();
  canvas.clear();
  input.begin();

  Serial.printf("[eNMEA] Board: %s, panel %ux%u\n", BoardConfig::ACTIVE.name,
                static_cast<unsigned>(canvas.width()), static_cast<unsigned>(canvas.height()));

  const BatteryMonitor::Status batt = battery.readStatus();
  if (batt.supported && batt.percentageKnown) {
    Serial.printf("[eNMEA] Battery: %u%%", static_cast<unsigned>(batt.percentage));
    if (batt.millivoltsKnown) Serial.printf("  %u mV", static_cast<unsigned>(batt.millivolts));
    if (batt.chargingKnown && batt.charging) Serial.print("  charging");
    Serial.println();
  } else {
    Serial.println("[eNMEA] Battery: no telemetry from this board profile");
  }

  const bool hasSettings = loadAppSettings(settings);

  if (!hasSettings) {
    Serial.println("[eNMEA] No saved config - starting setup AP 'eNMEA-Setup'");
    drawSetupScreen("SETUP MODE");
    portal.beginAsAccessPoint();
    return;  // loop() services the portal and the buttons until the user saves
  }

  Serial.printf("[eNMEA] Connecting to '%s'...\n", settings.ssid);
  canvas.drawText(20, 40, "CONNECTING...", 2, true);
  canvas.present(EInkDisplay::HALF_REFRESH);

  if (!connectToWifi()) {
    Serial.println("[eNMEA] Wi-Fi connect failed - falling back to setup AP");
    drawSetupScreen("WIFI FAILED");
    portal.beginAsAccessPoint();
    return;
  }

  Serial.printf("[eNMEA] Connected. IP %s  gw %s  mask %s  rssi %d dBm  ch %d\n",
                WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
                WiFi.subnetMask().toString().c_str(), WiFi.RSSI(), WiFi.channel());

  nmeaSource.begin(settings);
  // The settings page stays up for the whole session - on this IP, and on the
  // setup AP alongside it, so it is reachable even when the saved source
  // address is wrong for wherever the device now is.
  portal.beginOnStation();

  canvas.clear();
  dashboard.drawChrome(settings);
  canvas.present(EInkDisplay::FULL_REFRESH);

  provisioningOnly = false;
}

void loop() {
  handleGestures();
  portal.poll();

  if (provisioningOnly) {
    delay(2);  // nothing else to drive; yield so WiFi/AP housekeeping runs
    return;
  }

  static unsigned long lastDashboardUpdate = 0;
  static int updateCount = 0;

  nmeaSource.poll(nmeaData, sentenceTable);

  const unsigned long now = millis();
  if (now - lastDashboardUpdate < DASHBOARD_REFRESH_MS) {
    delay(2);  // yield between socket-poll ticks instead of busy-spinning
    return;
  }
  lastDashboardUpdate = now;

  // Where to reach this device, shown on the panel because it is otherwise
  // undiscoverable from the device itself.
  char netLine[56];
  if (WiFi.status() == WL_CONNECTED) {
    std::snprintf(netLine, sizeof(netLine), "SETUP: %s OR AP", WiFi.localIP().toString().c_str());
  } else {
    std::snprintf(netLine, sizeof(netLine), "WIFI DOWN - SETUP AP 192.168.4.1");
  }

  char battLine[16];
  formatBattery(battLine, sizeof(battLine));

  dashboard.drawValues(nmeaData, sentenceTable, nmeaSource.stateText(), netLine, battLine, now);

  ++updateCount;
  if (updateCount % FULL_REPAINT_EVERY_N_UPDATES == 0) {
    canvas.present(EInkDisplay::HALF_REFRESH);
  } else {
    canvas.present(EInkDisplay::FAST_REFRESH);
  }
}
