#pragma once

#include <cstddef>
#include <cstdint>

// NMEA 0183 sentences are at most 82 chars including '$'/'!' and CRLF.
constexpr size_t NMEA_MAX_SENTENCE_LEN = 82;

// Sentence types this project understands well enough to pull dashboard
// fields out of. Add more here (and in NmeaParser.cpp) as needed - the
// checksum/framing layer (NmeaLineReader) already handles any sentence.
enum class NmeaSentenceType : uint8_t {
  Unknown = 0,
  GGA,  // GPS fix: position, fix quality
  RMC,  // Recommended minimum: position, speed, course
  VTG,  // Track made good and speed over ground
  HDT,  // Heading, true
  HDG,  // Heading, magnetic (+ deviation/variation)
  MTW,  // Water temperature
  DBT,  // Depth below transducer (feet/meters/fathoms)
  DPT,  // Depth (meters + transducer offset)
  MWV,  // Wind speed and angle (relative or true, per its reference field)
  MWD,  // Wind direction and speed, true
  VDM,  // AIS message received from another vessel/station (a "target")
  VDO,  // AIS message output by this vessel's own transponder
};

constexpr int MAX_AIS_TARGETS = 64;
// AIS Class B units can go as long as ~6 minutes between position reports
// when static/anchored, and static-data (type 5) reports repeat on a similar
// cycle - a target isn't gone just because it didn't transmit in the last
// few seconds, unlike this tool's other STALE_AFTER_MS-style checks.
constexpr unsigned long AIS_TARGET_STALE_MS = 360000;

struct AisTarget {
  uint32_t mmsi = 0;
  unsigned long lastHeardMs = 0;
};

// Distinct AIS targets (by MMSI) heard via VDM sentences. Own-ship VDO
// sentences don't feed this - VDO is this vessel's own transponder output,
// not another target.
struct AisTargetTable {
  AisTarget entries[MAX_AIS_TARGETS];
  int count = 0;

  // Records/refreshes a sighting of `mmsi` at `nowMs`. No-op if the table is
  // full and `mmsi` is new - already-tracked targets keep updating.
  void recordSighting(uint32_t mmsi, unsigned long nowMs) {
    for (int i = 0; i < count; ++i) {
      if (entries[i].mmsi == mmsi) {
        entries[i].lastHeardMs = nowMs;
        return;
      }
    }
    if (count >= MAX_AIS_TARGETS) return;
    entries[count].mmsi = mmsi;
    entries[count].lastHeardMs = nowMs;
    ++count;
  }

  // Targets heard within AIS_TARGET_STALE_MS of `nowMs` - i.e. currently "in
  // view", not just ever seen since boot.
  int liveCount(unsigned long nowMs) const {
    int n = 0;
    for (int i = 0; i < count; ++i) {
      if (nowMs - entries[i].lastHeardMs <= AIS_TARGET_STALE_MS) ++n;
    }
    return n;
  }
};

// Latest parsed values. Each field's "has*" flag is set the first time a
// sentence supplies it and stays set (stale data is still shown, just
// visually marked stale by Dashboard once lastUpdateMs is too old).
struct NmeaData {
  bool hasPosition = false;
  double latDeg = 0.0;  // + = North
  double lonDeg = 0.0;  // + = East
  unsigned long positionUpdateMs = 0;

  bool hasSpeed = false;
  float speedKnots = 0.0f;
  unsigned long speedUpdateMs = 0;

  bool hasCourse = false;
  float courseDegTrue = 0.0f;
  unsigned long courseUpdateMs = 0;

  bool hasHeading = false;
  float headingDeg = 0.0f;
  bool headingIsTrue = false;  // false = magnetic (from HDG)
  unsigned long headingUpdateMs = 0;

  bool hasWaterTemp = false;
  float waterTempC = 0.0f;
  unsigned long waterTempUpdateMs = 0;

  bool hasDepth = false;
  float depthMeters = 0.0f;
  unsigned long depthUpdateMs = 0;

  bool hasWind = false;
  float windSpeedKnots = 0.0f;
  float windDirectionDeg = 0.0f;
  bool windDirectionIsTrue = false;  // false = relative to bow (MWV with reference 'R')
  unsigned long windUpdateMs = 0;

  AisTargetTable aisTargets;
};

// Per-sentence-ID tracking for the "sentences seen" checklist.
struct SentenceStatus {
  char id[4] = {0};  // e.g. "GGA" (talker prefix stripped)
  uint32_t validCount = 0;
  uint32_t checksumFailCount = 0;
  unsigned long lastValidMs = 0;
};

// 12 decoded IDs (see Dashboard.cpp's KNOWN_IDS) plus generous headroom for
// everything else a real feed carries.
//
// Sized for the job this tool is actually used for: proving that data is
// present on a network, not just the handful of types it draws boxes for. A
// NMEA 2000 gateway converting a healthy backbone to 0183 easily emits more
// than twenty distinct IDs - GGA RMC VTG GLL ZDA GSA GSV HDG HDT HDM MWV MWD
// MTW DBT DPT DBK VHW VLW XDR RSA ROT VDM VDO TXT is twenty-four before engine
// and tank variants. The old limit of 20 meant the table filled and every
// later type became invisible, which is the worst failure a verification tool
// can have: the user concludes a transmitter is silent when it is only
// untracked. At 16 bytes an entry this costs 768 bytes of a 320 KB heap.
constexpr int MAX_TRACKED_SENTENCE_IDS = 48;

struct SentenceTable {
  SentenceStatus entries[MAX_TRACKED_SENTENCE_IDS];
  int count = 0;
  // Set once a new sentence ID had to be turned away. The UI surfaces this, so
  // a full table is never mistaken for a quiet network.
  bool overflowed = false;

  // Finds or creates (if room) the entry for a 3-char sentence id.
  SentenceStatus* findOrAdd(const char* id3) {
    for (int i = 0; i < count; ++i) {
      if (entries[i].id[0] == id3[0] && entries[i].id[1] == id3[1] && entries[i].id[2] == id3[2]) {
        return &entries[i];
      }
    }
    if (count >= MAX_TRACKED_SENTENCE_IDS) {
      overflowed = true;
      return nullptr;
    }
    SentenceStatus* s = &entries[count++];
    s->id[0] = id3[0];
    s->id[1] = id3[1];
    s->id[2] = id3[2];
    s->id[3] = '\0';
    return s;
  }
};
