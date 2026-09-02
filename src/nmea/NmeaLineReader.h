#pragma once

#include <cstddef>
#include <cstdint>

#include "NmeaTypes.h"

// Turns a raw byte stream (from TCP or UDP, doesn't matter which) into
// framed NMEA sentences. Static buffer, no heap allocation, safe to feed
// one byte at a time from a socket read loop.
class NmeaLineReader {
 public:
  // Feed one byte. Returns true when `outLine` now holds a complete sentence
  // (from '$' or '!' up to but not including the CR/LF), null-terminated.
  // Malformed/oversized input is dropped silently and framing resyncs on the
  // next '$' or '!' - a torn sentence at connect time is expected and not an
  // error worth surfacing.
  bool feed(uint8_t c, char outLine[NMEA_MAX_SENTENCE_LEN + 1]);

 private:
  char buf_[NMEA_MAX_SENTENCE_LEN + 1] = {0};
  size_t len_ = 0;
  bool inSentence_ = false;
};
