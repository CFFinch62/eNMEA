#include "AppSettings.h"

#include <Preferences.h>

namespace {
constexpr char NAMESPACE[] = "enmea";
constexpr char KEY_SSID[] = "ssid";
constexpr char KEY_PASS[] = "pass";
constexpr char KEY_HOST[] = "host";
constexpr char KEY_PORT[] = "port";
constexpr char KEY_PROTO[] = "proto";
}  // namespace

bool loadAppSettings(AppSettings& out) {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, /*readOnly=*/true)) return false;

  const bool hadSsid = prefs.isKey(KEY_SSID);
  if (hadSsid) {
    prefs.getString(KEY_SSID, out.ssid, sizeof(out.ssid));
    prefs.getString(KEY_PASS, out.password, sizeof(out.password));
    prefs.getString(KEY_HOST, out.host, sizeof(out.host));
    out.port = prefs.getUShort(KEY_PORT, out.port);
    out.protocol = static_cast<AppSettings::Protocol>(prefs.getUChar(KEY_PROTO, static_cast<uint8_t>(out.protocol)));
  }
  prefs.end();
  return hadSsid;
}

bool saveAppSettings(const AppSettings& in) {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, /*readOnly=*/false)) return false;
  prefs.putString(KEY_SSID, in.ssid);
  prefs.putString(KEY_PASS, in.password);
  prefs.putString(KEY_HOST, in.host);
  prefs.putUShort(KEY_PORT, in.port);
  prefs.putUChar(KEY_PROTO, static_cast<uint8_t>(in.protocol));
  prefs.end();
  return true;
}

void clearAppSettings() {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, /*readOnly=*/false)) return;
  prefs.clear();
  prefs.end();
}
