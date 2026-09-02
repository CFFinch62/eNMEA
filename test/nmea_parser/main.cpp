#include <cstdio>

#include "test_support.h"

// Host-side tests for the NMEA framing and parsing layer. No hardware, no
// simulator, no PlatformIO:
//
//   g++ -std=c++20 -Isrc -Itest/stubs test/nmea_parser/*.cpp src/nmea/*.cpp
//       -o /tmp/nmea_parser_test && /tmp/nmea_parser_test
//
// or just: test/run_tests.sh
//
// Every checksum below was computed independently (XOR of the bytes between
// '$'/'!' and '*'), and the expected coordinates were worked out by hand from
// the NMEA ddmm.mmmm form - not read back out of this parser. Tests that only
// confirm the code agrees with itself would not catch the bugs worth catching.
int main() {
  std::printf("eNMEA parser tests\n==================\n");

  runLineReaderTests();
  runParserTests();
  runAisTests();

  std::printf("\n==================\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
