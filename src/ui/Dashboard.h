#pragma once

#include "EinkCanvas.h"
#include "nmea/NmeaTypes.h"
#include "settings/AppSettings.h"

// Draws the whole "is the NMEA feed alive and sane" view: a checklist of
// sentence IDs seen (with per-ID valid/checksum-fail counts) and a small
// grid of the values the user asked for - Position, Speed, Course, Heading,
// Water Temp, Water Depth, Wind, AIS Targets.
class Dashboard {
 public:
  explicit Dashboard(EinkCanvas& canvas) : canvas_(canvas) {}

  // Full layout redraw: call once after canvas_.clear(), before the first
  // present(). Draws box borders/labels that don't change call to call.
  void drawChrome(const AppSettings& settings);

  // Redraws only the value regions inside the boxes already drawn by
  // drawChrome(). Caller is responsible for calling canvas_.present() with
  // whatever refresh mode it decides on (see main.cpp's redraw policy).
  //
  // `sourceState` is NmeaSource::stateText(); `netLine` is the device's own
  // addresses; `batteryText` is e.g. "BATT 87%" (empty to omit). All passed as
  // text rather than as object references so the UI layer stays independent of
  // the networking and power layers.
  void drawValues(const NmeaData& data, const SentenceTable& table, const char* sourceState, const char* netLine,
                  const char* batteryText, unsigned long nowMs);

  // Overwrites the status row with a single message (used for the
  // "keep holding" button feedback). Cleared by the next drawValues().
  void drawStatusMessage(const char* message);

 private:
  EinkCanvas& canvas_;

  void drawBox(int x, int y, int w, int h, const char* label);
  void drawBoxValue(int x, int y, int w, int h, const char* line1, const char* line2, bool stale);
  void drawChecklistRow(int index, const SentenceStatus* status, unsigned long nowMs);

  // Grid geometry, derived from canvas_.width()/height() so the 4x2 grid fits
  // both panels without overflowing (they differ in both dimensions - see
  // EinkCanvas.h) and so the X3's extra height becomes taller boxes rather
  // than dead space at the bottom.
  int gridColW() const;
  int gridColX(int col) const;
  int gridRowH() const;
  int gridRowY(int row) const;
  int otherMaxLines() const;
};
