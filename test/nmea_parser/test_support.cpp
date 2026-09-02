#include "test_support.h"

#include <cmath>
#include <cstdio>

int g_checks = 0;
int g_failures = 0;
unsigned long g_fakeMillis = 100000;

unsigned long millis() { return g_fakeMillis; }

void beginSection(const char* name) { std::printf("\n-- %s\n", name); }

void reportCheck(bool ok, const char* expr, const char* file, int line) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::printf("   FAIL  %s:%d\n         %s\n", file, line, expr);
}

void reportNear(double got, double want, double tol, const char* expr, const char* file, int line) {
  ++g_checks;
  if (std::fabs(got - want) <= tol) return;
  ++g_failures;
  std::printf("   FAIL  %s:%d\n         %s\n         got %.6f, want %.6f (tol %.6f)\n", file, line, expr, got, want,
              tol);
}

NmeaParser::Result parseSentence(const char* sentence, NmeaData& data) {
  char buf[NMEA_MAX_SENTENCE_LEN + 1];
  std::snprintf(buf, sizeof(buf), "%s", sentence);
  NmeaParser parser;
  return parser.parseLine(buf, data);
}

int feedStream(NmeaLineReader& reader, const char* stream, char* lastLine) {
  int emitted = 0;
  char line[NMEA_MAX_SENTENCE_LEN + 1];
  for (const char* p = stream; *p != '\0'; ++p) {
    if (reader.feed(static_cast<uint8_t>(*p), line)) {
      ++emitted;
      if (lastLine != nullptr) std::snprintf(lastLine, NMEA_MAX_SENTENCE_LEN + 1, "%s", line);
    }
  }
  return emitted;
}
