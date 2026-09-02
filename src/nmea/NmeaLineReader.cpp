#include "NmeaLineReader.h"

bool NmeaLineReader::feed(uint8_t c, char outLine[NMEA_MAX_SENTENCE_LEN + 1]) {
  if (c == '$' || c == '!') {
    // A new start character always (re)synchronizes framing, even mid-sentence -
    // a dropped CR/LF or a multiplexer splicing two feeds should not wedge us.
    inSentence_ = true;
    len_ = 0;
    buf_[len_++] = static_cast<char>(c);
    return false;
  }

  if (!inSentence_) return false;  // discard noise before the first '$'/'!'

  if (c == '\r' || c == '\n') {
    if (len_ == 0) return false;  // blank line / lone terminator
    inSentence_ = false;
    buf_[len_] = '\0';
    for (size_t i = 0; i <= len_; ++i) outLine[i] = buf_[i];
    len_ = 0;
    return true;
  }

  if (len_ >= NMEA_MAX_SENTENCE_LEN) {
    // Oversized "sentence" - not real NMEA, drop it and wait for the next start char.
    inSentence_ = false;
    len_ = 0;
    return false;
  }

  buf_[len_++] = static_cast<char>(c);
  return false;
}
