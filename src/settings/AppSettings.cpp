#include "AppSettings.h"

#include <Preferences.h>

#include <cstring>

namespace {
constexpr char NAMESPACE[] = "enmea";
constexpr char KEY_BLOB[] = "cfg";

// Stored as one blob rather than a key per field: eight profiles would
// otherwise be ~48 NVS keys to keep in step, and a partial write would leave a
// half-configured profile behind. The header lets a build reject data it does
// not understand instead of reinterpreting someone else's bytes as an SSID.
constexpr uint32_t MAGIC = 0x654E4D31;  // "eNM1"
constexpr uint16_t VERSION = 1;

struct StoredSettings {
  uint32_t magic;
  uint16_t version;
  int8_t activeIndex;
  NmeaProfile profiles[MAX_PROFILES];
};
}  // namespace

bool loadAppSettings(AppSettings& out) {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, /*readOnly=*/true)) return false;

  StoredSettings stored{};
  const size_t got = prefs.getBytes(KEY_BLOB, &stored, sizeof(stored));
  prefs.end();

  // A short read, a different layout, or an older format: all mean "nothing we
  // can use". Coming up unconfigured is the safe outcome - the alternative is
  // trying to join a network named from arbitrary bytes.
  if (got != sizeof(stored) || stored.magic != MAGIC || stored.version != VERSION) return false;

  for (int i = 0; i < MAX_PROFILES; ++i) out.profiles[i] = stored.profiles[i];
  out.activeIndex = stored.activeIndex;
  // Guard against a stored index pointing at a slot that was later emptied.
  if (!out.indexValid(out.activeIndex)) out.activeIndex = static_cast<int8_t>(out.firstUsed());
  return out.usedCount() > 0;
}

bool saveAppSettings(const AppSettings& in) {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, /*readOnly=*/false)) return false;

  StoredSettings stored{};
  stored.magic = MAGIC;
  stored.version = VERSION;
  stored.activeIndex = in.activeIndex;
  for (int i = 0; i < MAX_PROFILES; ++i) stored.profiles[i] = in.profiles[i];

  const size_t written = prefs.putBytes(KEY_BLOB, &stored, sizeof(stored));
  prefs.end();
  return written == sizeof(stored);
}

void clearAppSettings() {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, /*readOnly=*/false)) return;
  prefs.clear();
  prefs.end();
}
