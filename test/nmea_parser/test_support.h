#pragma once

#include <cstdio>
#include <cstring>

#include "nmea/NmeaLineReader.h"
#include "nmea/NmeaParser.h"
#include "nmea/NmeaTypes.h"

// Tiny check harness. Deliberately not a framework: these tests need to run
// from one g++ invocation with no dependencies, which is the whole reason they
// are cheap enough to run on every parser change.

extern int g_checks;
extern int g_failures;

// What millis() returns (see test/stubs/Arduino.h). Tests set it so field
// timestamps and staleness windows are deterministic rather than wall-clock.
extern unsigned long g_fakeMillis;

void reportCheck(bool ok, const char* expr, const char* file, int line);
void reportNear(double got, double want, double tol, const char* expr, const char* file, int line);
void beginSection(const char* name);

#define CHECK(cond) reportCheck((cond), #cond, __FILE__, __LINE__)
#define CHECK_NEAR(got, want, tol) reportNear((got), (want), (tol), #got " ~= " #want, __FILE__, __LINE__)
#define CHECK_STR(got, want) reportCheck(std::strcmp((got), (want)) == 0, #got " == " #want, __FILE__, __LINE__)

// parseLine() splits fields in place - it writes NULs into its argument - so
// each call needs its own mutable copy. Passing a string literal would be
// undefined behaviour; this is the only sanctioned way to parse in a test.
NmeaParser::Result parseSentence(const char* sentence, NmeaData& data);

// Feeds a raw byte stream through NmeaLineReader, returning how many complete
// sentences came out and copying the last one into `lastLine`.
int feedStream(NmeaLineReader& reader, const char* stream, char* lastLine);

void runParserTests();
void runAisTests();
void runLineReaderTests();

void runProfileTests();
