#include "ProvisioningPortal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

namespace {
WebServer server(80);

// Moves the SoftAP off the ESP32 default subnet before it starts - see the
// collision note in ProvisioningPortal.h. Must run before softAP().
void configureApSubnet() {
  IPAddress ip;
  ip.fromString(SETUP_AP_IP);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
}

// Sent in chunks rather than snprintf'd into one buffer: the page outgrew a
// single stack buffer once it gained the status block and the reset form, and
// a silently truncated page is a nasty failure mode for the one screen you
// need when nothing else works.
constexpr char PAGE_HEAD[] =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>eNMEA Setup</title>"
    "<style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}"
    "label{display:block;margin-top:1em;font-weight:bold}"
    "input,select{width:100%;padding:.5em;box-sizing:border-box}"
    "button{margin-top:1.5em;padding:.7em 1.5em}"
    ".status{background:#f4f4f4;border-radius:6px;padding:.8em;font-size:.9em;line-height:1.5}"
    ".hint{color:#555;font-size:.85em;margin-top:.3em}"
    ".danger{margin-top:2.5em;border-top:1px solid #ddd;padding-top:1em}"
    ".danger button{background:#b00;color:#fff;border:0;border-radius:4px}"
    "table{border-collapse:collapse;width:100%;font-size:.92em;margin-top:.5em}"
    "th{text-align:left;font-size:.75em;text-transform:uppercase;color:#666;border-bottom:2px solid #333;padding:.3em .4em .3em 0}"
    "td{padding:.45em .4em;border-bottom:1px solid #ddd;vertical-align:top}"
    "tr.active{background:#eef6ee;font-weight:600}"
    "button.link{background:none;border:0;color:#06c;text-decoration:underline;cursor:pointer;padding:0;margin:0;font-size:1em}"
    "h3{margin:1.6em 0 0}</style></head><body>"
    "<h2>eNMEA Setup</h2>";

constexpr char PAGE_FORM_TAIL[] =
    "<button type='submit' name='save' value='1'>Save</button> "
    "<button type='submit' name='use' value='1'>Save &amp; use now</button>"
    "</form>"
    "<div class='danger'><form method='POST' action='/forget'>"
    "<p><b>Start over.</b> Erases <b>all</b> saved profiles and reboots into setup mode. "
    "To clear just the one in use, hold BACK on the device for 3 seconds.</p>"
    "<button type='submit'>Erase all profiles</button>"
    "</form></div>"
    "</body></html>";

void sendStatusBlock() {
  char buf[512];
  const bool staUp = WiFi.status() == WL_CONNECTED;
  std::snprintf(buf, sizeof(buf),
                "<div class='status'>"
                "<b>Setup AP:</b> %s &rarr; http://%s/<br>"
                "<b>Wi-Fi:</b> %s%s%s<br>"
                "<b>This page is always reachable over the setup AP</b>, whatever the "
                "Wi-Fi settings below say."
                "</div>",
                SETUP_AP_SSID, WiFi.softAPIP().toString().c_str(), staUp ? "connected, device IP " : "not connected",
                staUp ? WiFi.localIP().toString().c_str() : "", staUp ? "" : " (setup mode)");
  server.sendContent(buf);
}

int slotFromArg() {
  const int slot = server.arg("slot").toInt();
  return (slot >= 0 && slot < MAX_PROFILES) ? slot : 0;
}

void sendForm() {
  AppSettings cfg;
  loadAppSettings(cfg);
  const int editing = slotFromArg();

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(PAGE_HEAD);
  sendStatusBlock();

  char buf[900];

  // The slot table first: on a bench the usual job is switching to a stored
  // configuration, not typing a new one.
  server.sendContent("<h3>Saved profiles</h3><table><tr><th>#</th><th>Name</th><th>Source</th><th></th></tr>");
  for (int i = 0; i < MAX_PROFILES; ++i) {
    const NmeaProfile& p = cfg.profiles[i];
    char target[96];
    if (!p.used) {
      std::snprintf(target, sizeof(target), "<i>empty</i>");
    } else if (p.protocol == NmeaProfile::Protocol::UDP) {
      std::snprintf(target, sizeof(target), "UDP :%u &middot; %s", p.port, p.ssid);
    } else {
      std::snprintf(target, sizeof(target), "TCP %s:%u &middot; %s", p.host, p.port, p.ssid);
    }
    const char* label = p.used ? (p.name[0] ? p.name : "(unnamed)") : "";
    if (p.used && i != cfg.activeIndex) {
      std::snprintf(buf, sizeof(buf),
                    "<tr><td>%d</td><td>%s</td><td>%s</td><td>"
                    "<a href='/?slot=%d'>edit</a> "
                    "<form method='POST' action='/use' style='display:inline'>"
                    "<input type='hidden' name='slot' value='%d'>"
                    "<button class='link'>use</button></form></td></tr>",
                    i + 1, label, target, i, i);
    } else {
      std::snprintf(buf, sizeof(buf),
                    "<tr class='%s'><td>%d</td><td>%s</td><td>%s</td><td><a href='/?slot=%d'>edit</a></td></tr>",
                    i == cfg.activeIndex ? "active" : "", i + 1, label, target, i);
    }
    server.sendContent(buf);
  }
  server.sendContent("</table><p class='hint'>The highlighted row is in use. On the device, "
                     "<b>UP</b>/<b>DOWN</b> step through saved profiles and <b>CONFIRM</b> applies one - "
                     "no phone needed once they are stored.</p>");

  const NmeaProfile& e = cfg.profiles[editing];
  std::snprintf(buf, sizeof(buf),
                "<h3>Profile %d</h3><form method='POST' action='/save'>"
                "<input type='hidden' name='slot' value='%d'>"
                "<label>Name</label><input name='name' maxlength='16' value='%s' placeholder='e.g. AIS-100 bench'>"
                "<label>Wi-Fi SSID</label><input name='ssid' maxlength='32' value='%s' required>"
                "<label>Wi-Fi Password</label><input name='pass' type='password' maxlength='64' value='%s'>"
                "<div class='hint'>Leave blank for an open network - most equipment access points are open.</div>",
                editing + 1, editing, e.name, e.ssid, e.password);
  server.sendContent(buf);

  std::snprintf(buf, sizeof(buf),
                "<label>Protocol</label><select name='proto'>"
                "<option value='udp'%s>UDP (listen for broadcasts)</option>"
                "<option value='tcp'%s>TCP (connect out to a server)</option></select>"
                "<div class='hint'>UDP <i>listens</i> on the port and ignores Host. TCP <i>dials out</i> "
                "to Host:Port. Getting it backwards shows no data and no error.</div>"
                "<label>NMEA Source Host (TCP only)</label>"
                "<input name='host' maxlength='63' value='%s' placeholder='e.g. 192.168.1.50'>"
                "<label>Port</label><input name='port' type='number' min='1' max='65535' value='%u'>",
                e.protocol == NmeaProfile::Protocol::UDP ? " selected" : "",
                e.protocol == NmeaProfile::Protocol::TCP ? " selected" : "", e.host, e.port);
  server.sendContent(buf);

  server.sendContent(PAGE_FORM_TAIL);
  server.sendContent("");
}

// Reads the form into `slot`. Shared by "save" and "save & use".
void readProfileFromForm(AppSettings& cfg, int slot) {
  NmeaProfile& p = cfg.profiles[slot];
  std::strncpy(p.name, server.arg("name").c_str(), sizeof(p.name) - 1);
  std::strncpy(p.ssid, server.arg("ssid").c_str(), sizeof(p.ssid) - 1);
  std::strncpy(p.password, server.arg("pass").c_str(), sizeof(p.password) - 1);
  std::strncpy(p.host, server.arg("host").c_str(), sizeof(p.host) - 1);
  const long port = server.arg("port").toInt();
  p.port = (port > 0 && port <= 65535) ? static_cast<uint16_t>(port) : 10110;
  p.protocol = server.arg("proto") == "tcp" ? NmeaProfile::Protocol::TCP : NmeaProfile::Protocol::UDP;
  p.used = p.ssid[0] != '\0';
  if (p.used && p.name[0] == '\0') {
    std::snprintf(p.name, sizeof(p.name), "PROFILE %d", slot + 1);
  }
}

void handleSave() {
  AppSettings cfg;
  loadAppSettings(cfg);
  const int slot = slotFromArg();
  readProfileFromForm(cfg, slot);

  const bool useNow = server.hasArg("use");
  const bool wasActive = cfg.activeIndex == slot;
  if (useNow) cfg.activeIndex = static_cast<int8_t>(slot);
  if (!cfg.hasActive()) cfg.activeIndex = static_cast<int8_t>(cfg.firstUsed());
  saveAppSettings(cfg);

  Serial.printf("[eNMEA] Saved profile %d '%s' ssid='%s' proto=%s host='%s' port=%u%s\n", slot + 1,
                cfg.profiles[slot].name, cfg.profiles[slot].ssid,
                cfg.profiles[slot].protocol == NmeaProfile::Protocol::TCP ? "TCP" : "UDP", cfg.profiles[slot].host,
                cfg.profiles[slot].port, useNow ? " (applying)" : "");

  // Only reboot when the running configuration actually changed. Editing a
  // profile you are not using should never interrupt the feed you are watching.
  if (useNow || wasActive) {
    server.send(200, "text/html",
                "<html><body><h3>Saved. Switching...</h3>"
                "<p>The device is reconnecting; this page returns in a few seconds.</p></body></html>");
    delay(200);
    ESP.restart();
    return;
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleUse() {
  AppSettings cfg;
  loadAppSettings(cfg);
  const int slot = slotFromArg();
  if (!cfg.indexValid(slot)) {
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }
  cfg.activeIndex = static_cast<int8_t>(slot);
  saveAppSettings(cfg);
  Serial.printf("[eNMEA] Switching to profile %d '%s'\n", slot + 1, cfg.profiles[slot].name);
  server.send(200, "text/html",
              "<html><body><h3>Switching...</h3>"
              "<p>The device is reconnecting; this page returns in a few seconds.</p></body></html>");
  delay(200);
  ESP.restart();
}

void handleForget() {
  Serial.println("[eNMEA] All profiles erased from the setup page - rebooting into setup mode");
  clearAppSettings();
  server.send(200, "text/html",
              "<html><body><h3>All profiles erased. Rebooting into setup mode...</h3>"
              "<p>Rejoin the setup network and browse to http://192.168.7.1/</p></body></html>");
  delay(200);
  ESP.restart();
}
}  // namespace

void ProvisioningPortal::setupRoutes() {
  server.on("/", HTTP_GET, sendForm);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/use", HTTP_POST, handleUse);
  server.on("/forget", HTTP_POST, handleForget);
  server.begin();
}

void ProvisioningPortal::beginAsAccessPoint() {
  WiFi.mode(WIFI_AP);
  configureApSubnet();
  WiFi.softAP(SETUP_AP_SSID);
  Serial.printf("[eNMEA] Setup AP '%s' up at http://%s/\n", SETUP_AP_SSID, WiFi.softAPIP().toString().c_str());
  setupRoutes();
}

void ProvisioningPortal::beginOnStation() {
  // AP_STA, not STA: the setup AP stays up for the whole session. Costs a bit
  // of RF airtime (the AP is forced onto the station's channel) and some idle
  // current, which is the right trade for a bench tool that must never become
  // unreachable. Switching modes here does not drop the existing association.
  WiFi.mode(WIFI_AP_STA);
  configureApSubnet();
  WiFi.softAP(SETUP_AP_SSID);
  Serial.printf("[eNMEA] Settings page: http://%s/ (LAN) and http://%s/ via AP '%s'\n",
                WiFi.localIP().toString().c_str(), WiFi.softAPIP().toString().c_str(), SETUP_AP_SSID);
  setupRoutes();
}

void ProvisioningPortal::poll() { server.handleClient(); }
