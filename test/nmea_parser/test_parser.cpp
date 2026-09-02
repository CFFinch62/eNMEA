#include "test_support.h"

// Field extraction, one sentence type at a time.
//
// Expected values here were derived by hand, not read back from the parser:
// - Checksums are the XOR of every byte between '$'/'!' and '*'.
// - 4807.038,N is ddmm.mmmm, so 48 degrees + 7.038/60 = 48.1173 exactly.
// - 01131.000,E is dddmm.mmmm, so 11 + 31.0/60 = 11.5166667.
//
// The point of that is to catch a parser that is confidently wrong. A tool
// whose entire job is verification fails worst when it displays a plausible
// number rather than no number.

namespace {

constexpr double LAT_4807_038_N = 48.1173;
constexpr double LON_01131_000_E = 11.51666667;

// A GGA with a valid fix, used as the baseline "good sentence" throughout.
constexpr const char* GGA_VALID =
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

}  // namespace

void runParserTests() {
  beginSection("GGA - position and fix quality");
  {
    NmeaData data;
    const NmeaParser::Result r = parseSentence(GGA_VALID, data);
    CHECK(r.hasAddress);
    CHECK(r.checksumPresent);
    CHECK(r.checksumValid);
    CHECK_STR(r.talker, "GP");
    CHECK_STR(r.sentenceId, "GGA");
    CHECK(data.hasPosition);
    CHECK_NEAR(data.latDeg, LAT_4807_038_N, 1e-6);
    CHECK_NEAR(data.lonDeg, LON_01131_000_E, 1e-6);
    CHECK(data.positionUpdateMs == g_fakeMillis);
  }
  {
    // Southern/western hemispheres must negate. Getting this backwards puts
    // the vessel in the wrong hemisphere while looking entirely plausible.
    NmeaData data;
    parseSentence("$GPGGA,123519,4807.038,S,01131.000,W,1,08,0.9,545.4,M,46.9,M,,*48", data);
    CHECK(data.hasPosition);
    CHECK_NEAR(data.latDeg, -LAT_4807_038_N, 1e-6);
    CHECK_NEAR(data.lonDeg, -LON_01131_000_E, 1e-6);
  }
  {
    // Fix quality 0 means "no fix" - the coordinates in the sentence are not
    // trustworthy and must not reach the dashboard.
    NmeaData data;
    const NmeaParser::Result r =
        parseSentence("$GPGGA,123519,4807.038,N,01131.000,E,0,00,99.9,545.4,M,46.9,M,,*7E", data);
    CHECK(r.checksumValid);
    CHECK(!data.hasPosition);
  }

  beginSection("Checksum failure leaves data untouched");
  {
    // Corrupt only the checksum, keeping the body byte-identical to GGA_VALID
    // (*47 -> *46). Everything already in `data` must survive untouched, while
    // the result still carries the address so NmeaSource can count the failure
    // against the right sentence ID rather than losing it.
    NmeaData data;
    parseSentence(GGA_VALID, data);
    const double latBefore = data.latDeg;
    const double lonBefore = data.lonDeg;
    const unsigned long stampBefore = data.positionUpdateMs;

    g_fakeMillis += 5000;
    const NmeaParser::Result r =
        parseSentence("$GPGGA,999999,1234.567,S,12345.678,W,1,08,0.9,545.4,M,46.9,M,,*46", data);

    CHECK(r.hasAddress);
    CHECK(r.checksumPresent);
    CHECK(!r.checksumValid);
    CHECK_STR(r.sentenceId, "GGA");
    CHECK_NEAR(data.latDeg, latBefore, 1e-12);
    CHECK_NEAR(data.lonDeg, lonBefore, 1e-12);
    CHECK(data.positionUpdateMs == stampBefore);
  }
  {
    // No checksum at all is not "valid by default".
    NmeaData data;
    const NmeaParser::Result r = parseSentence("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", data);
    CHECK(r.hasAddress);
    CHECK(!r.checksumPresent);
    CHECK(!r.checksumValid);
    CHECK(!data.hasPosition);
  }

  beginSection("RMC - status gate, position, speed, course");
  {
    NmeaData data;
    parseSentence("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", data);
    CHECK(data.hasPosition);
    CHECK_NEAR(data.latDeg, LAT_4807_038_N, 1e-6);
    CHECK(data.hasSpeed);
    CHECK_NEAR(data.speedKnots, 22.4, 1e-4);
    CHECK(data.hasCourse);
    CHECK_NEAR(data.courseDegTrue, 84.4, 1e-4);
  }
  {
    // Status 'V' = navigation receiver warning. The sentence is well-formed and
    // its checksum is valid, but the values are not to be believed.
    NmeaData data;
    const NmeaParser::Result r =
        parseSentence("$GPRMC,123519,V,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*7D", data);
    CHECK(r.checksumValid);
    CHECK(!data.hasPosition);
    CHECK(!data.hasSpeed);
    CHECK(!data.hasCourse);
  }

  beginSection("VTG / HDT / HDG");
  {
    NmeaData data;
    parseSentence("$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48", data);
    CHECK(data.hasCourse);
    CHECK_NEAR(data.courseDegTrue, 54.7, 1e-4);
    CHECK(data.hasSpeed);
    // Field 5 is knots; field 7 is km/h. Reading the wrong one gives 10.2 -
    // a believable speed, silently wrong by a factor of 1.852.
    CHECK_NEAR(data.speedKnots, 5.5, 1e-4);
  }
  {
    NmeaData data;
    parseSentence("$HEHDT,274.07,T*19", data);
    CHECK(data.hasHeading);
    CHECK_NEAR(data.headingDeg, 274.07, 1e-4);
    CHECK(data.headingIsTrue);
  }
  {
    // HDG is magnetic and this parser deliberately does not apply the
    // deviation/variation fields - the flag must say so.
    NmeaData data;
    parseSentence("$HCHDG,98.3,0.0,E,12.6,W*57", data);
    CHECK(data.hasHeading);
    CHECK_NEAR(data.headingDeg, 98.3, 1e-4);
    CHECK(!data.headingIsTrue);
  }

  beginSection("MTW / DBT / DPT");
  {
    NmeaData data;
    parseSentence("$YXMTW,17.9,C*1D", data);
    CHECK(data.hasWaterTemp);
    CHECK_NEAR(data.waterTempC, 17.9, 1e-4);
  }
  {
    // DBT carries the same depth three times: feet, metres, fathoms. Field 3 is
    // metres. An off-by-one here reads 17.5 feet as 17.5 metres - a three-fold
    // error, in the one reading where being wrong runs you aground.
    NmeaData data;
    parseSentence("$SDDBT,17.5,f,5.3,M,2.9,F*38", data);
    CHECK(data.hasDepth);
    CHECK_NEAR(data.depthMeters, 5.3, 1e-4);
  }
  {
    // DPT field 2 is the transducer offset, deliberately ignored (its sign
    // convention is instrument-specific). Field 1 alone is what we report.
    NmeaData data;
    parseSentence("$SDDPT,5.3,0.4,*79", data);
    CHECK(data.hasDepth);
    CHECK_NEAR(data.depthMeters, 5.3, 1e-4);
  }

  beginSection("MWV / MWD - wind, units and reference");
  {
    NmeaData data;
    parseSentence("$WIMWV,214.8,R,10.0,N,A*1D", data);
    CHECK(data.hasWind);
    CHECK_NEAR(data.windDirectionDeg, 214.8, 1e-4);
    CHECK_NEAR(data.windSpeedKnots, 10.0, 1e-3);
    CHECK(!data.windDirectionIsTrue);  // 'R' = relative to bow
  }
  {
    // Reference 'T' means the angle is true, not relative.
    NmeaData data;
    parseSentence("$WIMWV,120.5,T,10.0,N,A*12", data);
    CHECK(data.hasWind);
    CHECK(data.windDirectionIsTrue);
  }
  {
    // All three speed units describe the same 10 knots: 18.52 km/h and
    // 5.14444 m/s both convert to it. Any of the three landing on a different
    // number means a broken conversion.
    NmeaData knots, kmh, ms;
    parseSentence("$WIMWV,214.8,R,10.0,N,A*1D", knots);
    parseSentence("$WIMWV,214.8,R,18.52,K,A*27", kmh);
    parseSentence("$WIMWV,214.8,R,5.14444,M,A*2B", ms);
    CHECK_NEAR(knots.windSpeedKnots, 10.0, 1e-3);
    CHECK_NEAR(kmh.windSpeedKnots, 10.0, 1e-3);
    CHECK_NEAR(ms.windSpeedKnots, 10.0, 1e-3);
  }
  {
    // Status 'V' invalidates the reading, exactly like RMC's status field.
    NmeaData data;
    const NmeaParser::Result r = parseSentence("$WIMWV,214.8,R,10.0,N,V*0A", data);
    CHECK(r.checksumValid);
    CHECK(!data.hasWind);
  }
  {
    // MWD's direction is always true, and field 5 is its knots value.
    NmeaData data;
    parseSentence("$WIMWD,285.0,T,275.0,M,12.3,N,6.3,M*60", data);
    CHECK(data.hasWind);
    CHECK_NEAR(data.windDirectionDeg, 285.0, 1e-4);
    CHECK_NEAR(data.windSpeedKnots, 12.3, 1e-4);
    CHECK(data.windDirectionIsTrue);
  }

  beginSection("Unrecognized sentences are still identified");
  {
    // GSA is not decoded into any dashboard box, but it must still be
    // checksum-verified and identified so it can be counted on the "OTHER:"
    // line. "Seen but not decoded" is information, not a parse failure.
    NmeaData data;
    const NmeaParser::Result r = parseSentence("$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39", data);
    CHECK(r.hasAddress);
    CHECK(r.checksumValid);
    CHECK_STR(r.sentenceId, "GSA");
    CHECK_STR(r.talker, "GP");
    CHECK(!data.hasPosition);
    CHECK(!data.hasSpeed);
    CHECK(!data.hasDepth);
  }
  {
    // Garbage that is not a sentence at all must not be reported as one.
    NmeaData data;
    const NmeaParser::Result r = parseSentence("not a sentence", data);
    CHECK(!r.hasAddress);
    CHECK(!r.checksumValid);
  }

  beginSection("Sentence table - capacity and honest overflow");
  {
    // The table has to hold everything a gateway converting NMEA 2000 emits,
    // because "this ID arrived at all" is the answer the tool exists to give.
    SentenceTable table;
    CHECK(MAX_TRACKED_SENTENCE_IDS >= 40);
    CHECK(!table.overflowed);

    char id[4] = {'A', 'A', 'A', '\0'};
    for (int i = 0; i < MAX_TRACKED_SENTENCE_IDS; ++i) {
      id[1] = static_cast<char>('A' + i / 26);
      id[2] = static_cast<char>('A' + i % 26);
      CHECK(table.findOrAdd(id) != nullptr);
    }
    CHECK(table.count == MAX_TRACKED_SENTENCE_IDS);
    CHECK(!table.overflowed);  // exactly full is not overflowed

    // One more distinct ID must be refused *and* recorded as refused. Silently
    // dropping it would let the dashboard imply a transmitter is quiet when it
    // is merely untracked - the one failure a verification tool must not have.
    char extra[4] = {'Z', 'Z', 'Z', '\0'};
    CHECK(table.findOrAdd(extra) == nullptr);
    CHECK(table.overflowed);
    CHECK(table.count == MAX_TRACKED_SENTENCE_IDS);

    // Already-tracked IDs keep working after overflow.
    id[1] = 'A';
    id[2] = 'A';
    SentenceStatus* first = table.findOrAdd(id);
    CHECK(first != nullptr);
    first->validCount = 7;
    CHECK(table.findOrAdd(id)->validCount == 7);
  }
  {
    // A feed within capacity never sets the flag.
    SentenceTable table;
    char id[4] = {'X', 'X', 'A', '\0'};
    for (int i = 0; i < 20; ++i) {
      id[2] = static_cast<char>('A' + i);
      table.findOrAdd(id);
    }
    CHECK(table.count == 20);
    CHECK(!table.overflowed);
  }

  beginSection("Field timestamps track millis()");
  {
    NmeaData data;
    g_fakeMillis = 250000;
    parseSentence(GGA_VALID, data);
    CHECK(data.positionUpdateMs == 250000);
    g_fakeMillis = 262000;
    parseSentence("$YXMTW,17.9,C*1D", data);
    CHECK(data.waterTempUpdateMs == 262000);
    // The untouched field keeps its older stamp - that is what drives the
    // per-box STALE marker on the dashboard.
    CHECK(data.positionUpdateMs == 250000);
  }
}
