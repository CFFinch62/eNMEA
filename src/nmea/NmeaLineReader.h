#pragma once

#include <cstddef>
#include <cstdint>

// NMEA 0183 sentences are at most 82 chars including '$'/'!' and CRLF.
// Lives here rather than with the data model: it is a property of the wire
// format, and keeping it here lets the framing layer be used on its own.
constexpr size_t NMEA_MAX_SENTENCE_LEN = 82;

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
