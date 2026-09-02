#include "test_support.h"

// AIS decoding and the live-target table.
//
// The test vector is independently verifiable, which matters more here than
// anywhere else in the parser: the 6-bit ASCII armouring plus a bit-offset
// header read is the easiest thing in this codebase to get subtly wrong, and a
// wrong MMSI still looks like a plausible MMSI.
//
//   !AIVDM,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0*5C
//
// Decoding the payload's six-bit characters gives message type 1 and, from
// bits 8..37, MMSI 366053209. Any AIS decoder will agree - the value does not
// come from this parser.

namespace {

constexpr uint32_t EXPECTED_MMSI = 366053209;
constexpr const char* VDM_FRAGMENT_1 = "!AIVDM,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0*5C";

}  // namespace

void runAisTests() {
  beginSection("AIS - MMSI decode");
  {
    NmeaData data;
    const NmeaParser::Result r = parseSentence(VDM_FRAGMENT_1, data);
    CHECK(r.hasAddress);
    CHECK(r.checksumValid);
    CHECK_STR(r.sentenceId, "VDM");
    CHECK_STR(r.talker, "AI");
    CHECK(data.aisTargets.count == 1);
    CHECK(data.aisTargets.entries[0].mmsi == EXPECTED_MMSI);
  }

  beginSection("AIS - targets are deduplicated by MMSI");
  {
    // A station transmits repeatedly. Each report refreshes the existing entry
    // rather than adding another, or the count would climb forever and the
    // AIS box would show traffic that isn't there.
    NmeaData data;
    g_fakeMillis = 100000;
    parseSentence(VDM_FRAGMENT_1, data);
    g_fakeMillis = 130000;
    parseSentence(VDM_FRAGMENT_1, data);
    parseSentence(VDM_FRAGMENT_1, data);
    CHECK(data.aisTargets.count == 1);
    CHECK(data.aisTargets.entries[0].lastHeardMs == 130000);  // refreshed, not stale
  }

  beginSection("AIS - only fragment 1 of a multi-part message is decoded");
  {
    // Fragment 2 carries a mid-message slice of the payload; the MMSI header is
    // not at its start. Decoding it anyway would invent a target from arbitrary
    // bits - worse than ignoring it, because the invented MMSI looks real.
    NmeaData data;
    const NmeaParser::Result r = parseSentence("!AIVDM,2,2,3,B,15M67FC000G?ufbE`FepT@3n00Sa,0*6F", data);
    CHECK(r.hasAddress);
    CHECK(r.checksumValid);
    CHECK_STR(r.sentenceId, "VDM");
    CHECK(data.aisTargets.count == 0);
  }

  beginSection("AIS - VDO is own-ship, not a target");
  {
    // VDO is this vessel's own transponder output. It must be counted in the
    // sentence checklist (so you can see the transponder is alive) but must
    // never appear as a target, or the vessel would count itself.
    NmeaData data;
    const NmeaParser::Result r = parseSentence("!AIVDO,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0*5E", data);
    CHECK(r.hasAddress);
    CHECK(r.checksumValid);
    CHECK_STR(r.sentenceId, "VDO");
    CHECK(data.aisTargets.count == 0);
  }

  beginSection("AIS - a bad checksum decodes nothing");
  {
    NmeaData data;
    const NmeaParser::Result r = parseSentence("!AIVDM,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0*5D", data);
    CHECK(r.hasAddress);
    CHECK(!r.checksumValid);
    CHECK(data.aisTargets.count == 0);
  }

  beginSection("AIS - liveCount() honours the staleness window");
  {
    // liveCount() is what the dashboard shows: targets heard within
    // AIS_TARGET_STALE_MS, not everything seen since boot. The boundary is
    // inclusive, so check both sides of it rather than a comfortable midpoint.
    AisTargetTable table;
    table.recordSighting(EXPECTED_MMSI, 100000);
    CHECK(table.count == 1);
    CHECK(table.liveCount(100000) == 1);
    CHECK(table.liveCount(100000 + AIS_TARGET_STALE_MS) == 1);
    CHECK(table.liveCount(100000 + AIS_TARGET_STALE_MS + 1) == 0);

    // A second target heard later keeps its own clock: the older one ages out
    // while the newer one stays live.
    table.recordSighting(366999712, 400000);
    CHECK(table.count == 2);
    CHECK(table.liveCount(400000) == 2);
    CHECK(table.liveCount(100000 + AIS_TARGET_STALE_MS + 1) == 1);
  }

  beginSection("AIS - the target table refuses to overflow");
  {
    // A busy waterway can exceed MAX_AIS_TARGETS. New MMSIs are dropped once
    // full, but the ones already tracked must keep updating rather than the
    // table corrupting or writing past its end.
    AisTargetTable table;
    for (int i = 0; i < MAX_AIS_TARGETS + 20; ++i) {
      table.recordSighting(200000000u + static_cast<uint32_t>(i), 100000);
    }
    CHECK(table.count == MAX_AIS_TARGETS);
    table.recordSighting(200000000u, 150000);  // already-tracked target
    CHECK(table.count == MAX_AIS_TARGETS);
    CHECK(table.entries[0].lastHeardMs == 150000);
  }

  beginSection("AIS - a too-short payload is rejected");
  {
    // The MMSI lives in bits 8..37, so a payload shorter than 7 six-bit
    // characters cannot contain one. Reading it anyway would run off the end
    // of the string.
    NmeaData data;
    parseSentence("!AIVDM,1,1,,B,15M6,0*5A", data);  // checksum is valid; the payload is the problem
    CHECK(data.aisTargets.count == 0);
  }
}
