#pragma once

#include "NmeaTypes.h"

// Stateless checksum validation + field extraction for one NMEA 0183
// sentence at a time. No heap allocation: field splitting happens in place
// on the caller's mutable line buffer.
class NmeaParser {
 public:
  struct Result {
    bool hasAddress = false;   // address field ("GPGGA" etc.) was present and >= 5 chars
    char talker[3] = {0};      // 2 chars + NUL, e.g. "GP"
    char sentenceId[4] = {0};  // 3 chars + NUL, e.g. "GGA"
    bool checksumPresent = false;
    bool checksumValid = false;
  };

  // `line` must be the sentence as produced by NmeaLineReader: starts with
  // '$' or '!', no CR/LF, mutable (this function writes NULs into it while
  // splitting fields - the caller's buffer is scratch, not reused after).
  //
  // `data` is only updated when the checksum is present and valid, and only
  // for sentence types this parser recognizes (see NmeaSentenceType). A
  // recognized-but-checksum-failed sentence still fills `result` so the
  // caller can count it as a checksum failure for that ID.
  Result parseLine(char* line, NmeaData& data);

 private:
  static bool verifyChecksum(const char* line, size_t addressAndFieldsLen);
};
