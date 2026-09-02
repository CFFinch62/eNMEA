#include "ProvisioningPortal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

namespace {
WebServer server(80);

constexpr char AP_SSID[] = "eNMEA-Setup";

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
    ".danger button{background:#b00;color:#fff;border:0;border-radius:4px}</style></head><body>"
    "<h2>eNMEA Setup</h2>";

constexpr char PAGE_FORM_TAIL[] =
    "<button type='submit'>Save &amp; Reboot</button>"
    "</form>"
    "<div class='danger'><form method='POST' action='/forget'>"
    "<p><b>Start over.</b> Erases the saved Wi-Fi and source settings and reboots into "
    "setup mode, so the device stops trying to join a network that isn't here.</p>"
    "<button type='submit'>Forget settings &amp; reboot to setup</button>"
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
                AP_SSID, WiFi.softAPIP().toString().c_str(), staUp ? "connected, device IP " : "not connected",
                staUp ? WiFi.localIP().toString().c_str() : "", staUp ? "" : " (setup mode)");
  server.sendContent(buf);
}

void sendForm() {
  AppSettings current;
  loadAppSettings(current);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent(PAGE_HEAD);
  sendStatusBlock();

  char buf[768];
  std::snprintf(buf, sizeof(buf),
                "<form method='POST' action='/save'>"
                "<label>Wi-Fi SSID</label><input name='ssid' maxlength='32' value='%s' required>"
                "<label>Wi-Fi Password</label><input name='pass' type='password' maxlength='64' value='%s'>"
                "<label>Protocol</label>"
                "<select name='proto'>"
                "<option value='udp'%s>UDP (listen for broadcasts)</option>"
                "<option value='tcp'%s>TCP (connect out to a server)</option>"
                "</select>"
                "<div class='hint'>UDP <i>listens</i> on the port below and ignores Host. "
                "TCP <i>dials out</i> to Host:Port. Pick the one your source actually speaks - "
                "getting it backwards shows no data and no error.</div>",
                current.ssid, current.password, current.protocol == AppSettings::Protocol::UDP ? " selected" : "",
                current.protocol == AppSettings::Protocol::TCP ? " selected" : "");
  server.sendContent(buf);

  std::snprintf(buf, sizeof(buf),
                "<label>NMEA Source Host (TCP only)</label>"
                "<input name='host' maxlength='63' value='%s' placeholder='e.g. 192.168.1.50'>"
                "<div class='hint'>The machine running the NMEA server. A laptop's address changes "
                "from one network to the next, so re-check this after moving the device.</div>"
                "<label>Port</label>"
                "<input name='port' type='number' min='1' max='65535' value='%u'>",
                current.host, current.port);
  server.sendContent(buf);

  server.sendContent(PAGE_FORM_TAIL);
  server.sendContent("");  // terminate the chunked response
}

void handleSave() {
  AppSettings s;
  std::strncpy(s.ssid, server.arg("ssid").c_str(), sizeof(s.ssid) - 1);
  std::strncpy(s.password, server.arg("pass").c_str(), sizeof(s.password) - 1);
  std::strncpy(s.host, server.arg("host").c_str(), sizeof(s.host) - 1);
  const long port = server.arg("port").toInt();
  s.port = (port > 0 && port <= 65535) ? static_cast<uint16_t>(port) : 10110;
  s.protocol = server.arg("proto") == "tcp" ? AppSettings::Protocol::TCP : AppSettings::Protocol::UDP;

  saveAppSettings(s);
  Serial.printf("[eNMEA] Settings saved: ssid='%s' proto=%s host='%s' port=%u - rebooting\n", s.ssid,
                s.protocol == AppSettings::Protocol::TCP ? "TCP" : "UDP", s.host, s.port);
  server.send(200, "text/html", "<html><body><h3>Saved. Rebooting...</h3></body></html>");
  delay(200);  // let the response flush before the socket goes away
  ESP.restart();
}

void handleForget() {
  Serial.println("[eNMEA] Settings erased from the setup page - rebooting into setup mode");
  clearAppSettings();
  server.send(200, "text/html",
              "<html><body><h3>Settings erased. Rebooting into setup mode...</h3>"
              "<p>Rejoin <b>eNMEA-Setup</b> and browse to http://192.168.7.1/</p></body></html>");
  delay(200);
  ESP.restart();
}
}  // namespace

void ProvisioningPortal::setupRoutes() {
  server.on("/", HTTP_GET, sendForm);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/forget", HTTP_POST, handleForget);
  server.begin();
}

void ProvisioningPortal::beginAsAccessPoint() {
  WiFi.mode(WIFI_AP);
  configureApSubnet();
  WiFi.softAP(AP_SSID);
  Serial.printf("[eNMEA] Setup AP '%s' up at http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  setupRoutes();
}

void ProvisioningPortal::beginOnStation() {
  // AP_STA, not STA: the setup AP stays up for the whole session. Costs a bit
  // of RF airtime (the AP is forced onto the station's channel) and some idle
  // current, which is the right trade for a bench tool that must never become
  // unreachable. Switching modes here does not drop the existing association.
  WiFi.mode(WIFI_AP_STA);
  configureApSubnet();
  WiFi.softAP(AP_SSID);
  Serial.printf("[eNMEA] Settings page: http://%s/ (LAN) and http://%s/ via AP '%s'\n",
                WiFi.localIP().toString().c_str(), WiFi.softAPIP().toString().c_str(), AP_SSID);
  setupRoutes();
}

void ProvisioningPortal::poll() { server.handleClient(); }
