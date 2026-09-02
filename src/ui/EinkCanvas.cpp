#include "EinkCanvas.h"

#include <cctype>
#include <cstring>

#include "BoardPins.h"
#include "Font5x7.h"

EinkCanvas::EinkCanvas() : display_(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

void EinkCanvas::begin() {
  // Verified against freeink-sdk's FreeInkDisplay.h: one binary can drive
  // both panels, chosen at runtime via setDisplayX3() before begin() (no
  // call = X4 path). This project picks it at compile time instead of
  // pulling in XteinkDetect's runtime fingerprint probe - see the
  // ENMEA_BOARD_X3 build flag in platformio.ini.
#ifdef ENMEA_BOARD_X3
  display_.setDisplayX3();
#endif
  display_.begin();
}

void EinkCanvas::clear() { display_.clearScreen(0xFF); }

void EinkCanvas::setPixel(int x, int y, bool black) {
  if (x < 0 || y < 0 || x >= width() || y >= height()) return;
  uint8_t* fb = display_.getFrameBuffer();
  const uint16_t stride = width() / 8;
  uint8_t& byte = fb[y * stride + (x / 8)];
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (x % 8));  // MSB-first
  if (black) {
    byte &= static_cast<uint8_t>(~mask);
  } else {
    byte |= mask;
  }
}

void EinkCanvas::fillRect(int x, int y, int w, int h, bool black) {
  for (int row = y; row < y + h; ++row) {
    for (int col = x; col < x + w; ++col) {
      setPixel(col, row, black);
    }
  }
}

void EinkCanvas::drawHLine(int x, int y, int w, bool black) {
  for (int col = x; col < x + w; ++col) setPixel(col, y, black);
}

void EinkCanvas::drawVLine(int x, int y, int h, bool black) {
  for (int row = y; row < y + h; ++row) setPixel(x, row, black);
}

void EinkCanvas::drawRect(int x, int y, int w, int h, bool black) {
  drawHLine(x, y, w, black);
  drawHLine(x, y + h - 1, w, black);
  drawVLine(x, y, h, black);
  drawVLine(x + w - 1, y, h, black);
}

namespace {
constexpr int GLYPH_ADVANCE = font5x7::GLYPH_WIDTH + 1;  // 1px gap between glyphs
}  // namespace

void EinkCanvas::drawText(int x, int y, const char* text, int scale, bool black) {
  int cursorX = x;
  for (const char* p = text; *p != '\0'; ++p) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    const uint8_t* glyph = font5x7::glyphFor(upper);
    if (glyph != nullptr) {
      for (int col = 0; col < font5x7::GLYPH_WIDTH; ++col) {
        const uint8_t bits = glyph[col];
        for (int row = 0; row < font5x7::GLYPH_HEIGHT; ++row) {
          if ((bits & (1 << row)) == 0) continue;
          if (scale == 1) {
            setPixel(cursorX + col, y + row, black);
          } else {
            fillRect(cursorX + col * scale, y + row * scale, scale, scale, black);
          }
        }
      }
    }
    cursorX += GLYPH_ADVANCE * scale;
  }
}

int EinkCanvas::textWidth(const char* text, int scale) const {
  return static_cast<int>(std::strlen(text)) * GLYPH_ADVANCE * scale;
}

int EinkCanvas::textHeight(int scale) const { return font5x7::GLYPH_HEIGHT * scale; }

void EinkCanvas::sleepPanel() { display_.deepSleep(); }

void EinkCanvas::present(EInkDisplay::RefreshMode mode) {
  // Passing turnOffScreen explicitly: CrossInk's HalDisplay::displayBuffer
  // always supplies both args to the underlying EInkDisplay call, so this
  // project does the same rather than assume the SDK method itself defaults
  // the second parameter.
  display_.displayBuffer(mode, /*turnOffScreen=*/false);

  // EInkDisplay is DOUBLE-BUFFERED and displayBuffer() ends with
  // swapBuffers(), so getFrameBuffer() now points at the frame from *two*
  // refreshes ago - not at what the panel is showing. Everything this project
  // draws after the first frame is a partial patch (Dashboard::drawValues
  // repaints value regions but not the box chrome, which drawChrome() draws
  // once), so patching that stale buffer put half the UI on alternate frames:
  // the boxes flickered on and off at the 2s redraw cadence, and the buffer
  // that never received the chrome still held setup()'s "CONNECTING..."
  // splash, which reappeared underneath.
  //
  // Copying the just-displayed frame back into the write buffer restores the
  // invariant every caller here already assumes: the write buffer always
  // matches what is on the panel. Costs one ~52 KB memcpy per refresh, i.e.
  // once every 2 seconds. This is exactly what the SDK documents the call for.
  display_.syncWriteBufferFromActive();
}
