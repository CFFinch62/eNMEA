#include "test_support.h"

// NmeaLineReader turns a byte stream into framed sentences. The cases that
// matter are the malformed ones: a multiplexer's output is not a tidy sequence
// of well-terminated lines, and the reader must never wedge on one bad
// sentence and swallow every good one behind it.

void runLineReaderTests() {
  beginSection("NmeaLineReader framing");

  const char* GGA = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
  const char* RMC = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";

  {  // A plain CRLF-terminated sentence comes out byte-identical, without the CRLF.
    NmeaLineReader reader;
    char last[NMEA_MAX_SENTENCE_LEN + 1] = {0};
    char stream[256];
    std::snprintf(stream, sizeof(stream), "%s\r\n", GGA);
    CHECK(feedStream(reader, stream, last) == 1);
    CHECK_STR(last, GGA);
  }

  {  // A bare LF terminates too - not every source sends CR.
    NmeaLineReader reader;
    char last[NMEA_MAX_SENTENCE_LEN + 1] = {0};
    char stream[256];
    std::snprintf(stream, sizeof(stream), "%s\n", GGA);
    CHECK(feedStream(reader, stream, last) == 1);
    CHECK_STR(last, GGA);
  }

  {  // Noise before the first '$' is discarded rather than prepended to the
     // first real sentence. This is the normal case when connecting mid-stream.
    NmeaLineReader reader;
    char last[NMEA_MAX_SENTENCE_LEN + 1] = {0};
    char stream[256];
    std::snprintf(stream, sizeof(stream), "8,0.9,545.4,M*47\r\n%s\r\n", GGA);
    CHECK(feedStream(reader, stream, last) == 1);  // the torn fragment is not emitted
    CHECK_STR(last, GGA);
  }

  {  // Two sentences run together with no CR/LF between them - some
     // multiplexers really do this. The '$' resynchronizes framing mid-
     // sentence, so the truncated first one is dropped and the second is
     // emitted intact. Losing the torn sentence is correct; corrupting the
     // one after it would not be.
    NmeaLineReader reader;
    char last[NMEA_MAX_SENTENCE_LEN + 1] = {0};
    char stream[256];
    std::snprintf(stream, sizeof(stream), "%s%s\r\n", GGA, RMC);
    CHECK(feedStream(reader, stream, last) == 1);
    CHECK_STR(last, RMC);
  }

  {  // '!' (AIS) starts a sentence exactly like '$' does.
    NmeaLineReader reader;
    char last[NMEA_MAX_SENTENCE_LEN + 1] = {0};
    const char* vdm = "!AIVDM,1,1,,B,15M67FC000G?ufbE`FepT@3n00Sa,0*5C";
    char stream[256];
    std::snprintf(stream, sizeof(stream), "%s\r\n", vdm);
    CHECK(feedStream(reader, stream, last) == 1);
    CHECK_STR(last, vdm);
  }

  {  // More than NMEA_MAX_SENTENCE_LEN bytes with no terminator: the oversized
     // run is dropped and the *next* real sentence still parses. A reader that
     // kept buffering here would corrupt every sentence that followed.
    NmeaLineReader reader;
    char last[NMEA_MAX_SENTENCE_LEN + 1] = {0};
    char stream[512];
    char oversized[NMEA_MAX_SENTENCE_LEN + 40];
    oversized[0] = '$';
    for (size_t i = 1; i < sizeof(oversized) - 1; ++i) oversized[i] = 'A';
    oversized[sizeof(oversized) - 1] = '\0';
    std::snprintf(stream, sizeof(stream), "%s%s\r\n", oversized, GGA);
    CHECK(feedStream(reader, stream, last) == 1);
    CHECK_STR(last, GGA);
  }

  {  // A lone terminator, and blank lines between sentences, emit nothing.
    NmeaLineReader reader;
    char last[NMEA_MAX_SENTENCE_LEN + 1] = {0};
    char stream[256];
    std::snprintf(stream, sizeof(stream), "\r\n\r\n%s\r\n\r\n", GGA);
    CHECK(feedStream(reader, stream, last) == 1);
    CHECK_STR(last, GGA);
  }

  {  // Back-to-back well-formed sentences all come through.
    NmeaLineReader reader;
    char last[NMEA_MAX_SENTENCE_LEN + 1] = {0};
    char stream[512];
    std::snprintf(stream, sizeof(stream), "%s\r\n%s\r\n%s\r\n", GGA, RMC, GGA);
    CHECK(feedStream(reader, stream, last) == 3);
    CHECK_STR(last, GGA);
  }
}
