#include "NmeaParser.h"

#include <Arduino.h>  // millis()

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int MAX_FIELDS = 20;

int splitFields(char* body, char* fields[MAX_FIELDS]) {
  int count = 0;
  char* p = body;
  fields[count++] = p;
  while (*p != '\0' && count < MAX_FIELDS) {
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    }
    ++p;
  }
  return count;
}

bool hexNibble(char c, uint8_t& outNibble) {
  if (c >= '0' && c <= '9') {
    outNibble = static_cast<uint8_t>(c - '0');
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    outNibble = static_cast<uint8_t>(c - 'A' + 10);
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    outNibble = static_cast<uint8_t>(c - 'a' + 10);
    return true;
  }
  return false;
}

float toFloat(const char* field) {
  if (field == nullptr || *field == '\0') return 0.0f;
  return strtof(field, nullptr);
}

bool fieldPresent(const char* field) { return field != nullptr && *field != '\0'; }

// MWV speed is tagged with its own unit field (K=km/h, M=m/s, N=knots) -
// normalize to knots so the dashboard always shows one unit.
float windSpeedToKnots(float speed, char unit) {
  switch (unit) {
    case 'K':
      return speed / 1.852f;
    case 'M':
      return speed * 1.943844f;
    default:  // 'N' or unrecognized - assume already knots
      return speed;
  }
}

// Decodes the MMSI (bits 8-37 of the AIS binary payload, present in every
// message type's common navigation block) out of an AIVDM/AIVDO 6-bit
// ASCII-armored payload. Only fragment 1 of a multi-part message needs
// decoding for this - the header always falls within the first fragment
// even for long messages (e.g. type 5 static/voyage data spans many
// fragments, but its header doesn't move).
bool decodeAisMmsi(const char* payload, uint32_t& outMmsi) {
  const size_t len = std::strlen(payload);
  if (len < 7) return false;  // need >=42 bits to safely read the 38-bit header

  auto sixBitValue = [](char c) -> uint8_t {
    uint8_t v = static_cast<uint8_t>(c) - 48;
    if (v > 40) v -= 8;
    return static_cast<uint8_t>(v & 0x3F);
  };
  auto bitAt = [&](int bitPos) -> uint32_t {
    const int charIdx = bitPos / 6;
    const int bitInChar = bitPos % 6;  // 0 = MSB of that character's 6 bits
    return (sixBitValue(payload[charIdx]) >> (5 - bitInChar)) & 0x1;
  };

  uint32_t mmsi = 0;
  for (int bit = 8; bit < 38; ++bit) {
    mmsi = (mmsi << 1) | bitAt(bit);
  }
  outMmsi = mmsi;
  return true;
}

// NMEA lat/lon fields are "d..dmm.mmmm": whatever precedes the last two
// whole-number digits before the decimal point is degrees (2 digits for
// lat, 3 for lon), the rest is minutes. Dividing by 100 and flooring
// isolates the minutes' 2 digits regardless of how many degree digits
// precede them, so one implementation covers both lat and lon.
double parseCoordinate(const char* field) {
  if (!fieldPresent(field)) return 0.0;
  const double raw = strtod(field, nullptr);
  const double degrees = std::floor(raw / 100.0);
  const double minutes = raw - degrees * 100.0;
  return degrees + minutes / 60.0;
}

}  // namespace

bool NmeaParser::verifyChecksum(const char* line, size_t addressAndFieldsLen) {
  uint8_t computed = 0;
  for (size_t i = 0; i < addressAndFieldsLen; ++i) {
    computed ^= static_cast<uint8_t>(line[i]);
  }
  const char* star = line + addressAndFieldsLen;
  if (star[0] != '*' || star[1] == '\0' || star[2] == '\0') return false;
  uint8_t hi = 0, lo = 0;
  if (!hexNibble(star[1], hi) || !hexNibble(star[2], lo)) return false;
  const uint8_t expected = static_cast<uint8_t>((hi << 4) | lo);
  return computed == expected;
}

NmeaParser::Result NmeaParser::parseLine(char* line, NmeaData& data) {
  Result result;

  if (line[0] != '$' && line[0] != '!') return result;

  char* body = line + 1;  // skip '$'/'!'
  char* star = std::strchr(body, '*');
  const size_t bodyLen = star != nullptr ? static_cast<size_t>(star - body) : std::strlen(body);

  if (star != nullptr) {
    result.checksumPresent = true;
    result.checksumValid = verifyChecksum(body, bodyLen);
    *star = '\0';  // terminate body at '*' so field splitting doesn't see the checksum
  }

  char* fields[MAX_FIELDS];
  const int fieldCount = splitFields(body, fields);
  if (fieldCount < 1) return result;

  const char* address = fields[0];
  const size_t addressLen = std::strlen(address);
  if (addressLen >= 5) {
    result.hasAddress = true;
    result.talker[0] = address[0];
    result.talker[1] = address[1];
    result.sentenceId[0] = address[addressLen - 3];
    result.sentenceId[1] = address[addressLen - 2];
    result.sentenceId[2] = address[addressLen - 1];
  }

  // Only trust field values when the checksum actually validated - that's
  // the entire point of a "verification" tool.
  if (!result.checksumValid || !result.hasAddress) return result;

  const unsigned long now = millis();
  const char* id = result.sentenceId;

  if (std::strncmp(id, "GGA", 3) == 0 && fieldCount >= 7) {
    // fields: 1=time 2=lat 3=N/S 4=lon 5=E/W 6=fixQuality
    if (fieldPresent(fields[6]) && std::atoi(fields[6]) > 0 && fieldPresent(fields[2]) && fieldPresent(fields[4])) {
      double lat = parseCoordinate(fields[2]);
      if (fields[3][0] == 'S') lat = -lat;
      double lon = parseCoordinate(fields[4]);
      if (fields[5][0] == 'W') lon = -lon;
      data.latDeg = lat;
      data.lonDeg = lon;
      data.hasPosition = true;
      data.positionUpdateMs = now;
    }
  } else if (std::strncmp(id, "RMC", 3) == 0 && fieldCount >= 9) {
    // fields: 2=status 3=lat 4=N/S 5=lon 6=E/W 7=speedKn 8=courseT
    if (fieldPresent(fields[2]) && fields[2][0] == 'A') {
      if (fieldPresent(fields[3]) && fieldPresent(fields[5])) {
        double lat = parseCoordinate(fields[3]);
        if (fields[4][0] == 'S') lat = -lat;
        double lon = parseCoordinate(fields[5]);
        if (fields[6][0] == 'W') lon = -lon;
        data.latDeg = lat;
        data.lonDeg = lon;
        data.hasPosition = true;
        data.positionUpdateMs = now;
      }
      if (fieldPresent(fields[7])) {
        data.speedKnots = toFloat(fields[7]);
        data.hasSpeed = true;
        data.speedUpdateMs = now;
      }
      if (fieldPresent(fields[8])) {
        data.courseDegTrue = toFloat(fields[8]);
        data.hasCourse = true;
        data.courseUpdateMs = now;
      }
    }
  } else if (std::strncmp(id, "VTG", 3) == 0 && fieldCount >= 6) {
    // fields: 1=courseT 2='T' 3=courseM 4='M' 5=speedKn 6='N'
    if (fieldPresent(fields[1])) {
      data.courseDegTrue = toFloat(fields[1]);
      data.hasCourse = true;
      data.courseUpdateMs = now;
    }
    if (fieldCount >= 6 && fieldPresent(fields[5])) {
      data.speedKnots = toFloat(fields[5]);
      data.hasSpeed = true;
      data.speedUpdateMs = now;
    }
  } else if (std::strncmp(id, "HDT", 3) == 0 && fieldCount >= 2) {
    if (fieldPresent(fields[1])) {
      data.headingDeg = toFloat(fields[1]);
      data.headingIsTrue = true;
      data.hasHeading = true;
      data.headingUpdateMs = now;
    }
  } else if (std::strncmp(id, "HDG", 3) == 0 && fieldCount >= 2) {
    // Raw magnetic sensor heading - not corrected for deviation/variation here.
    if (fieldPresent(fields[1])) {
      data.headingDeg = toFloat(fields[1]);
      data.headingIsTrue = false;
      data.hasHeading = true;
      data.headingUpdateMs = now;
    }
  } else if (std::strncmp(id, "MTW", 3) == 0 && fieldCount >= 2) {
    if (fieldPresent(fields[1])) {
      data.waterTempC = toFloat(fields[1]);
      data.hasWaterTemp = true;
      data.waterTempUpdateMs = now;
    }
  } else if (std::strncmp(id, "DBT", 3) == 0 && fieldCount >= 4) {
    // fields: 1=feet 2='f' 3=meters 4='M' 5=fathoms 6='F'
    if (fieldPresent(fields[3])) {
      data.depthMeters = toFloat(fields[3]);
      data.hasDepth = true;
      data.depthUpdateMs = now;
    }
  } else if (std::strncmp(id, "DPT", 3) == 0 && fieldCount >= 2) {
    // Reports raw depth below transducer only (field 1). Not applying the
    // field-2 offset here: its sign convention (to waterline vs. to keel)
    // is instrument-configured and guessing it wrong would defeat the
    // point of a verification tool.
    if (fieldPresent(fields[1])) {
      data.depthMeters = toFloat(fields[1]);
      data.hasDepth = true;
      data.depthUpdateMs = now;
    }
  } else if (std::strncmp(id, "MWV", 3) == 0 && fieldCount >= 6) {
    // fields: 1=angle 2=reference(R/T) 3=speed 4=speedUnit(K/M/N) 5=status(A=valid)
    if (fieldPresent(fields[5]) && fields[5][0] == 'A' && fieldPresent(fields[1]) && fieldPresent(fields[3]) &&
        fieldPresent(fields[4])) {
      data.windDirectionDeg = toFloat(fields[1]);
      data.windDirectionIsTrue = fields[2][0] == 'T';
      data.windSpeedKnots = windSpeedToKnots(toFloat(fields[3]), fields[4][0]);
      data.hasWind = true;
      data.windUpdateMs = now;
    }
  } else if (std::strncmp(id, "MWD", 3) == 0 && fieldCount >= 6) {
    // fields: 1=trueDir 2='T' 3=magDir 4='M' 5=speedKn 6='N' 7=speedMps 8='M'
    if (fieldPresent(fields[1]) && fieldPresent(fields[5])) {
      data.windDirectionDeg = toFloat(fields[1]);
      data.windDirectionIsTrue = true;
      data.windSpeedKnots = toFloat(fields[5]);
      data.hasWind = true;
      data.windUpdateMs = now;
    }
  } else if (std::strncmp(id, "VDM", 3) == 0 && fieldCount >= 6) {
    // fields: 1=totalFragments 2=fragmentNumber 3=sequentialMsgId 4=channel
    //         5=payload 6=fillBits
    // Only VDM (a message received from another station) counts as a
    // "target" - VDO is this vessel's own transponder output.
    if (fieldPresent(fields[2]) && fields[2][0] == '1' && fieldPresent(fields[5])) {
      uint32_t mmsi = 0;
      if (decodeAisMmsi(fields[5], mmsi)) {
        data.aisTargets.recordSighting(mmsi, now);
      }
    }
  }

  return result;
}
