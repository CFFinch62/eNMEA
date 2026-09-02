#!/usr/bin/env sh
# Host-side parser tests. No hardware, no PlatformIO, no toolchain beyond g++.
#
#   test/run_tests.sh
#
# NmeaParser.cpp and NmeaLineReader.cpp are pure C++ apart from millis(), which
# test/stubs/Arduino.h supplies - that is what makes this possible. Keep it that
# way: pull String or Serial into src/nmea/ and these tests stop compiling.
set -e
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/nmea_parser_test"
g++ -std=c++20 -Wall -Wextra -Isrc -Itest/stubs \
    test/nmea_parser/*.cpp src/nmea/*.cpp -o "$OUT"
exec "$OUT"
