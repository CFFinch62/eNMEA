#include "settings/AppSettings.h"
#include "test_support.h"

// Profile selection logic. Pure data handling, so it is testable on the host
// even though everything around it is Arduino-bound.
//
// This drives the UP/DOWN buttons on a bench, where the failure modes are all
// at the edges: wrapping past the ends, skipping empty slots, and what happens
// when the active profile is deleted out from under the selection.

namespace {

void fill(AppSettings& cfg, int slot, const char* name) {
  NmeaProfile& p = cfg.profiles[slot];
  std::snprintf(p.name, sizeof(p.name), "%s", name);
  std::snprintf(p.ssid, sizeof(p.ssid), "net-%s", name);
  p.used = true;
}

}  // namespace

void runProfileTests() {
  beginSection("Profiles - empty settings");
  {
    AppSettings cfg;
    CHECK(cfg.usedCount() == 0);
    CHECK(!cfg.hasActive());
    CHECK(cfg.active() == nullptr);
    CHECK(cfg.firstUsed() == -1);
    // Cycling with nothing stored must not wander off the array.
    CHECK(cfg.nextUsed(-1, 1) == -1);
    CHECK(cfg.nextUsed(0, -1) == -1);
  }

  beginSection("Profiles - cycling skips empty slots and wraps");
  {
    AppSettings cfg;
    fill(cfg, 0, "A");
    fill(cfg, 3, "B");
    fill(cfg, 7, "C");  // deliberately the last slot, to exercise the wrap
    CHECK(cfg.usedCount() == 3);
    CHECK(cfg.firstUsed() == 0);

    CHECK(cfg.nextUsed(0, 1) == 3);
    CHECK(cfg.nextUsed(3, 1) == 7);
    CHECK(cfg.nextUsed(7, 1) == 0);  // wraps forward past the end

    CHECK(cfg.nextUsed(0, -1) == 7);  // wraps backward past the start
    CHECK(cfg.nextUsed(7, -1) == 3);
    CHECK(cfg.nextUsed(3, -1) == 0);
  }

  beginSection("Profiles - a single profile always selects itself");
  {
    AppSettings cfg;
    fill(cfg, 5, "ONLY");
    CHECK(cfg.nextUsed(5, 1) == 5);
    CHECK(cfg.nextUsed(5, -1) == 5);
    // Starting from an empty slot still finds the only real one.
    CHECK(cfg.nextUsed(0, 1) == 5);
  }

  beginSection("Profiles - active index validity");
  {
    AppSettings cfg;
    fill(cfg, 2, "X");
    cfg.activeIndex = 2;
    CHECK(cfg.hasActive());
    CHECK_STR(cfg.active()->name, "X");

    // Pointing at an empty slot is not "active" - this is the state left
    // behind when BACK forgets the profile in use.
    cfg.activeIndex = 4;
    CHECK(!cfg.hasActive());
    CHECK(cfg.active() == nullptr);

    // Out-of-range indices must be rejected rather than read.
    cfg.activeIndex = -1;
    CHECK(!cfg.hasActive());
    cfg.activeIndex = MAX_PROFILES;
    CHECK(!cfg.hasActive());
    cfg.activeIndex = 99;
    CHECK(!cfg.hasActive());
  }

  beginSection("Profiles - forgetting the active one falls back");
  {
    AppSettings cfg;
    fill(cfg, 1, "FIRST");
    fill(cfg, 6, "SECOND");
    cfg.activeIndex = 6;
    CHECK(cfg.hasActive());

    cfg.profiles[6] = NmeaProfile{};  // what forgetActiveProfile() does
    CHECK(!cfg.hasActive());
    CHECK(cfg.usedCount() == 1);
    CHECK(cfg.firstUsed() == 1);

    cfg.activeIndex = static_cast<int8_t>(cfg.firstUsed());
    CHECK(cfg.hasActive());
    CHECK_STR(cfg.active()->name, "FIRST");

    // Forget the last one and there is nothing to fall back to.
    cfg.profiles[1] = NmeaProfile{};
    CHECK(cfg.firstUsed() == -1);
    CHECK(!cfg.hasActive());
  }

  beginSection("Profiles - all eight slots usable");
  {
    AppSettings cfg;
    for (int i = 0; i < MAX_PROFILES; ++i) fill(cfg, i, "P");
    CHECK(cfg.usedCount() == MAX_PROFILES);
    // A full ring visits every slot exactly once before repeating.
    int at = 0;
    for (int i = 0; i < MAX_PROFILES; ++i) at = cfg.nextUsed(at, 1);
    CHECK(at == 0);
  }
}
