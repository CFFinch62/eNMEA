#include "Dashboard.h"

#include <cstdio>
#include <cstring>

namespace {

// Sentence types this tool actively decodes into dashboard values. Anything
// else the source sends still gets checksum-verified and counted (see
// NmeaSource::handleByte) but is only summarized, not given its own row.
// VDM/VDO (AIS) get rows here too even though AIS TGTS is the only box
// fed by them - a "verification tool" should show every sentence type it
// understands is arriving, not just the ones with dashboard real estate.
constexpr const char* KNOWN_IDS[] = {"GGA", "RMC", "VTG", "HDT", "HDG", "MTW",
                                      "DBT", "DPT", "MWV", "MWD", "VDM", "VDO"};
constexpr int KNOWN_ID_COUNT = sizeof(KNOWN_IDS) / sizeof(KNOWN_IDS[0]);

constexpr unsigned long STALE_AFTER_MS = 10000;

// Text scales. The 5x7 font has 1px strokes, which at scale 1 is genuinely
// hard to read across a room on a 792px-wide panel - everything a user reads
// at a glance is scale 2 (10x14, 2px strokes). Scale 1 survives only where the
// content is reference material you walk up to: the source address and the
// footer hints.
constexpr int TEXT_BODY = 2;
constexpr int TEXT_FINE = 1;
constexpr int GLYPH_ADV_2 = 12;  // 5px glyph + 1px gap, doubled

// Vertical bands, top to bottom. Nothing here overlaps: title 6..19,
// status 26..39, divider 46, list header 52..65, rows from 72.
constexpr int TITLE_Y = 6;
constexpr int STATUS_Y = 26;
constexpr int STATUS_CLEAR_Y = 24;
constexpr int STATUS_CLEAR_H = 17;
constexpr int DIVIDER_Y = 46;
constexpr int LIST_LABEL_Y = 52;

// Left column: the sentence-ID checklist. Rows are scale 2, so a row is 14px
// of glyph in a 24px pitch. LIST_W caps a row at 17 characters - see
// drawChecklistRow(), which clamps its counts to stay inside that.
constexpr int LIST_X = 8;
constexpr int LIST_TOP = 72;
constexpr int LIST_ROW_H = 24;
constexpr int LIST_W = 208;
constexpr int LIST_MAX_CHARS = LIST_W / GLYPH_ADV_2;  // 17

// The "OTHER:" block below the checklist: IDs that arrived but aren't decoded.
// Four per line at scale 2. The number of lines is derived from whatever panel
// height is left rather than hardcoded, so the X3's taller screen shows more
// of them (4 lines / 16 IDs) than the X4's (2 lines / 8).
//
// Whatever doesn't fit is reported as "+N MORE" rather than dropped silently.
// For a tool whose purpose is answering "is this data present?", under-showing
// without saying so would let a user conclude a device is not transmitting when
// it is only off the bottom of the list.
constexpr int OTHER_LABEL_GAP = 14;
constexpr int OTHER_IDS_PER_LINE = 4;

// 4 columns x 2 rows = 8 boxes. Both the column width and the row height are
// derived from the canvas's runtime size rather than hardcoded, so the grid
// fits the X3 (792x528) and X4 (800x480) panels without overflowing - and so
// the X3's extra 48px of height goes into taller boxes instead of dead space.
constexpr int GRID_X = 224;
constexpr int GRID_RIGHT_MARGIN = 8;
constexpr int GRID_COLS = 4;
constexpr int GRID_COL_GAP = 8;
constexpr int GRID_TOP = 56;
constexpr int GRID_ROW_GAP = 10;
constexpr int FOOTER_RESERVE = 22;  // space kept clear for the footer hint line

// Box labels are scale 2 too, which caps them at what fits inside the box
// (colW - 12 px, i.e. 10 characters on the X3). Hence "SEA TEMP" over "WATER
// TEMPERATURE" and "AIS TGTS" over "AIS TARGETS" - a legible short label beats
// an exact long one that has to be drawn too small to read.
constexpr const char* BOX_LABELS[8] = {"POSITION", "SPEED", "COURSE",   "HEADING",
                                       "SEA TEMP", "DEPTH", "WIND",     "AIS TGTS"};

const SentenceStatus* findStatus(const SentenceTable& table, const char* id3) {
  for (int i = 0; i < table.count; ++i) {
    if (std::strncmp(table.entries[i].id, id3, 3) == 0) return &table.entries[i];
  }
  return nullptr;
}

bool isKnownId(const char* id3) {
  for (int i = 0; i < KNOWN_ID_COUNT; ++i) {
    if (std::strncmp(KNOWN_IDS[i], id3, 3) == 0) return true;
  }
  return false;
}

}  // namespace

int Dashboard::gridColW() const {
  return (canvas_.width() - GRID_X - GRID_RIGHT_MARGIN - (GRID_COLS - 1) * GRID_COL_GAP) / GRID_COLS;
}

int Dashboard::gridColX(int col) const { return GRID_X + col * (gridColW() + GRID_COL_GAP); }

int Dashboard::gridRowH() const {
  return (canvas_.height() - GRID_TOP - FOOTER_RESERVE - GRID_ROW_GAP) / 2;
}

int Dashboard::gridRowY(int row) const { return GRID_TOP + row * (gridRowH() + GRID_ROW_GAP); }

// How many "OTHER:" lines fit between the checklist and the footer on this
// panel. The X3 is 48px taller than the X4, and that space is worth spending
// here rather than leaving blank.
int Dashboard::otherMaxLines() const {
  const int firstLineY = LIST_TOP + KNOWN_ID_COUNT * LIST_ROW_H + OTHER_LABEL_GAP + LIST_ROW_H;
  const int bottom = canvas_.height() - FOOTER_RESERVE;
  const int lines = (bottom - firstLineY) / LIST_ROW_H;
  return lines < 1 ? 1 : lines;
}

void Dashboard::drawBox(int x, int y, int w, int h, const char* label) {
  canvas_.drawRect(x, y, w, h, true);
  canvas_.drawText(x + 6, y + 6, label, TEXT_BODY, true);
  canvas_.drawHLine(x + 1, y + 24, w - 2, true);
}

void Dashboard::drawChrome(const AppSettings& settings) {
  canvas_.drawText(8, TITLE_Y, "eNMEA - NMEA 0183 WI-FI VERIFIER", TEXT_BODY, true);
  canvas_.drawHLine(0, DIVIDER_Y, canvas_.width(), true);

  // Source address stays fine-print: it's long (an IPv4 host and port), and
  // it's something you read once while setting up, not at a glance.
  char protoLine[80];
  if (settings.protocol == AppSettings::Protocol::UDP) {
    std::snprintf(protoLine, sizeof(protoLine), "UDP LISTEN :%u", settings.port);
  } else {
    std::snprintf(protoLine, sizeof(protoLine), "TCP %s:%u", settings.host, settings.port);
  }
  canvas_.drawText(canvas_.width() - canvas_.textWidth(protoLine, TEXT_FINE) - 8, TITLE_Y + 4, protoLine, TEXT_FINE,
                   true);

  canvas_.drawText(LIST_X, LIST_LABEL_Y, "SENTENCES SEEN", TEXT_BODY, true);

  // Footer: the two things you cannot discover from the screen otherwise -
  // how to switch the device off, and how to get back to the settings page.
  // Drawn once here because none of it changes while the dashboard is up.
  canvas_.drawText(8, canvas_.height() - 16,
                   "HOLD POWER 2S: SHUT DOWN    HOLD BACK 3S: ERASE SETTINGS    SETUP AP: ENMEA-SETUP", TEXT_FINE,
                   true);

  const int colW = gridColW();
  const int rowH = gridRowH();
  for (int i = 0; i < 8; ++i) {
    drawBox(gridColX(i % GRID_COLS), gridRowY(i / GRID_COLS), colW, rowH, BOX_LABELS[i]);
  }
}

void Dashboard::drawBoxValue(int x, int y, int w, int h, const char* line1, const char* line2, bool stale) {
  // Clear the value area (below the label strip drawChrome already drew)
  // before redrawing - text drawing only sets ink pixels, so a shorter new
  // string wouldn't otherwise erase the longer old one.
  canvas_.fillRect(x + 1, y + 25, w - 2, h - 26, /*black=*/false);
  canvas_.drawText(x + 8, y + 36, line1, TEXT_BODY, true);
  if (line2 != nullptr && line2[0] != '\0') {
    canvas_.drawText(x + 8, y + 64, line2, TEXT_BODY, true);
  }
  if (stale) {
    canvas_.drawText(x + 8, y + h - 24, "STALE", TEXT_BODY, true);
  }
}

void Dashboard::drawChecklistRow(int index, const SentenceStatus* status, unsigned long nowMs) {
  const int y = LIST_TOP + index * LIST_ROW_H;
  canvas_.fillRect(LIST_X, y, LIST_W, LIST_ROW_H - 2, false);

  char line[40];
  const char* id = KNOWN_IDS[index];
  if (status == nullptr || status->validCount == 0) {
    std::snprintf(line, sizeof(line), "%s --", id);
  } else {
    // Everything is clamped so the worst case - "GGA 9999 (99) 99+" - is
    // exactly LIST_MAX_CHARS wide. Without this an hour-old feed would render
    // counts and ages long enough to run off into the value grid.
    const unsigned long valid = status->validCount > 9999 ? 9999 : status->validCount;
    const unsigned long ageS = (nowMs - status->lastValidMs) / 1000;
    char age[6];
    if (ageS > 99) {
      std::snprintf(age, sizeof(age), "99+");
    } else {
      std::snprintf(age, sizeof(age), "%lus", ageS);
    }

    if (status->checksumFailCount > 0) {
      const unsigned long bad = status->checksumFailCount > 99 ? 99 : status->checksumFailCount;
      std::snprintf(line, sizeof(line), "%s %lu (%lu) %s", id, valid, bad, age);
    } else {
      std::snprintf(line, sizeof(line), "%s %lu %s", id, valid, age);
    }
  }
  canvas_.drawText(LIST_X, y, line, TEXT_BODY, true);
}

void Dashboard::drawStatusMessage(const char* message) {
  canvas_.fillRect(0, STATUS_CLEAR_Y, canvas_.width(), STATUS_CLEAR_H, false);
  canvas_.drawText(LIST_X, STATUS_Y, message, TEXT_BODY, true);
}

void Dashboard::drawValues(const NmeaData& data, const SentenceTable& table, const char* sourceState,
                            const char* netLine, const char* batteryText, unsigned long nowMs) {
  // Status row, right-to-left: battery at the far right in body size (it is a
  // walk-past glance, like the source state), then the device's own addresses
  // in fine print, then the source state on the left. The addresses matter as
  // much as the state - without them there is no way to find the settings page
  // from the device itself.
  char status[48];
  std::snprintf(status, sizeof(status), "SOURCE: %s", sourceState);
  canvas_.fillRect(0, STATUS_CLEAR_Y, canvas_.width(), STATUS_CLEAR_H, false);
  canvas_.drawText(LIST_X, STATUS_Y, status, TEXT_BODY, true);

  int rightEdge = canvas_.width() - 8;
  if (batteryText != nullptr && batteryText[0] != '\0') {
    const int w = canvas_.textWidth(batteryText, TEXT_BODY);
    canvas_.drawText(rightEdge - w, STATUS_Y, batteryText, TEXT_BODY, true);
    rightEdge -= w + 12;
  }
  if (netLine != nullptr && netLine[0] != '\0') {
    canvas_.drawText(rightEdge - canvas_.textWidth(netLine, TEXT_FINE), STATUS_Y + 4, netLine, TEXT_FINE, true);
  }

  for (int i = 0; i < KNOWN_ID_COUNT; ++i) {
    drawChecklistRow(i, findStatus(table, KNOWN_IDS[i]), nowMs);
  }

  // Anything else the source sent that isn't one of the decoded types. This is
  // the part that matters when the question is "is anything arriving at all?" -
  // a gateway converting NMEA 2000 emits far more types than this tool decodes,
  // and their presence alone proves the wiring and the backbone are good.
  const int otherLabelY = LIST_TOP + KNOWN_ID_COUNT * LIST_ROW_H + OTHER_LABEL_GAP;
  const int maxLines = otherMaxLines();
  canvas_.fillRect(LIST_X, otherLabelY, LIST_W, (maxLines + 1) * LIST_ROW_H, false);
  canvas_.drawText(LIST_X, otherLabelY, "OTHER:", TEXT_BODY, true);

  // Count first, so we know up front whether the list has to be truncated and
  // can reserve the final line to say so.
  int otherTotal = 0;
  for (int i = 0; i < table.count; ++i) {
    if (!isKnownId(table.entries[i].id)) ++otherTotal;
  }
  const int capacity = maxLines * OTHER_IDS_PER_LINE;
  const bool truncated = otherTotal > capacity || table.overflowed;
  // Give up the last line to the "+N MORE" note when one is needed.
  const int showLimit = truncated ? (maxLines - 1) * OTHER_IDS_PER_LINE : otherTotal;

  char lineBuf[LIST_MAX_CHARS + 2];
  int lineLen = 0;
  int lineIndex = 0;
  int onThisLine = 0;
  int shown = 0;
  lineBuf[0] = '\0';
  for (int i = 0; i < table.count && shown < showLimit; ++i) {
    if (isKnownId(table.entries[i].id)) continue;
    if (onThisLine > 0) lineBuf[lineLen++] = ' ';
    std::memcpy(lineBuf + lineLen, table.entries[i].id, 3);
    lineLen += 3;
    lineBuf[lineLen] = '\0';
    ++shown;
    if (++onThisLine == OTHER_IDS_PER_LINE) {
      canvas_.drawText(LIST_X, otherLabelY + (lineIndex + 1) * LIST_ROW_H, lineBuf, TEXT_BODY, true);
      ++lineIndex;
      onThisLine = 0;
      lineLen = 0;
      lineBuf[0] = '\0';
    }
  }
  if (onThisLine > 0) {
    canvas_.drawText(LIST_X, otherLabelY + (lineIndex + 1) * LIST_ROW_H, lineBuf, TEXT_BODY, true);
    ++lineIndex;
  }
  if (truncated) {
    // "FULL" means the tracking table itself ran out, so those IDs have no
    // counts either - a stronger statement than merely not having screen room.
    char more[LIST_MAX_CHARS + 2];
    if (table.overflowed) {
      std::snprintf(more, sizeof(more), "+%d MORE FULL", otherTotal - shown);
    } else {
      std::snprintf(more, sizeof(more), "+%d MORE", otherTotal - shown);
    }
    canvas_.drawText(LIST_X, otherLabelY + (lineIndex + 1) * LIST_ROW_H, more, TEXT_BODY, true);
  }

  char line1[24];
  char line2[24];

  const int colW = gridColW();
  const int rowH = gridRowH();
  const int row1Y = gridRowY(0);
  const int row2Y = gridRowY(1);

  if (data.hasPosition) {
    std::snprintf(line1, sizeof(line1), "%.4f", data.latDeg);
    std::snprintf(line2, sizeof(line2), "%.4f", data.lonDeg);
  } else {
    std::snprintf(line1, sizeof(line1), "--");
    line2[0] = '\0';
  }
  drawBoxValue(gridColX(0), row1Y, colW, rowH, line1, line2,
               data.hasPosition && (nowMs - data.positionUpdateMs) > STALE_AFTER_MS);

  if (data.hasSpeed) {
    std::snprintf(line1, sizeof(line1), "%.1f KT", data.speedKnots);
  } else {
    std::snprintf(line1, sizeof(line1), "--");
  }
  drawBoxValue(gridColX(1), row1Y, colW, rowH, line1, "",
               data.hasSpeed && (nowMs - data.speedUpdateMs) > STALE_AFTER_MS);

  if (data.hasCourse) {
    std::snprintf(line1, sizeof(line1), "%.0f T", data.courseDegTrue);
  } else {
    std::snprintf(line1, sizeof(line1), "--");
  }
  drawBoxValue(gridColX(2), row1Y, colW, rowH, line1, "",
               data.hasCourse && (nowMs - data.courseUpdateMs) > STALE_AFTER_MS);

  if (data.hasHeading) {
    std::snprintf(line1, sizeof(line1), "%.0f %s", data.headingDeg, data.headingIsTrue ? "T" : "M");
  } else {
    std::snprintf(line1, sizeof(line1), "--");
  }
  drawBoxValue(gridColX(3), row1Y, colW, rowH, line1, "",
               data.hasHeading && (nowMs - data.headingUpdateMs) > STALE_AFTER_MS);

  if (data.hasWaterTemp) {
    std::snprintf(line1, sizeof(line1), "%.1f C", data.waterTempC);
  } else {
    std::snprintf(line1, sizeof(line1), "--");
  }
  drawBoxValue(gridColX(0), row2Y, colW, rowH, line1, "",
               data.hasWaterTemp && (nowMs - data.waterTempUpdateMs) > STALE_AFTER_MS);

  if (data.hasDepth) {
    std::snprintf(line1, sizeof(line1), "%.1f M", data.depthMeters);
  } else {
    std::snprintf(line1, sizeof(line1), "--");
  }
  drawBoxValue(gridColX(1), row2Y, colW, rowH, line1, "",
               data.hasDepth && (nowMs - data.depthUpdateMs) > STALE_AFTER_MS);

  if (data.hasWind) {
    std::snprintf(line1, sizeof(line1), "%.1f KT", data.windSpeedKnots);
    std::snprintf(line2, sizeof(line2), "%.0f %s", data.windDirectionDeg, data.windDirectionIsTrue ? "T" : "R");
  } else {
    std::snprintf(line1, sizeof(line1), "--");
    line2[0] = '\0';
  }
  drawBoxValue(gridColX(2), row2Y, colW, rowH, line1, line2,
               data.hasWind && (nowMs - data.windUpdateMs) > STALE_AFTER_MS);

  // "--" until at least one VDM sentence has ever validated - distinct from
  // a live count of 0, which means VDM is arriving but no target is
  // currently within AIS_TARGET_STALE_MS of nowMs. liveCount() already bakes
  // in that freshness window, so this box doesn't need its own STALE tag.
  const SentenceStatus* vdmStatus = findStatus(table, "VDM");
  if (vdmStatus != nullptr && vdmStatus->validCount > 0) {
    std::snprintf(line1, sizeof(line1), "%d", data.aisTargets.liveCount(nowMs));
  } else {
    std::snprintf(line1, sizeof(line1), "--");
  }
  drawBoxValue(gridColX(3), row2Y, colW, rowH, line1, "", false);
}
