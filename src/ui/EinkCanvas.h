#pragma once

#include <EInkDisplay.h>
#include <cstdint>

// Minimal raw-framebuffer drawing surface on top of freeink-sdk's EInkDisplay.
//
// This intentionally does NOT reuse CrossInk's GfxRenderer/EpdFont stack:
// that stack is built on CrossInk's HalDisplay/HalGPIO and pulls in
// FontCacheManager, FontDecompressor, and compressed font assets meant for
// paginated book text. A 2-second dashboard of short all-caps labels and
// numbers doesn't need any of that - a small built-in bitmap font (see
// Font5x7.h) is enough and keeps this project decoupled from CrossInk.
//
// Framebuffer format, confirmed against freeink-sdk's FreeInkDisplay.cpp
// (blitImage's "1 = white, 0 = black" comment and its 0x80>>(x&7) masking):
// 1 bit per pixel, MSB-first within each byte, row-major, bit=1 white,
// bit=0 black.
class EinkCanvas {
 public:
  EinkCanvas();

  void begin();

  // Runtime accessors, not the static DISPLAY_WIDTH/DISPLAY_HEIGHT constants
  // - those are always the X4's 800x480 regardless of which panel begin()
  // actually initialized. Using them here on an X3 build would compute a
  // wrong framebuffer row stride in setPixel() (800/8=100 bytes vs the SDK's
  // actual 792/8=99-byte X3 rows), corrupting every row after the first.
  uint16_t width() const { return display_.getDisplayWidth(); }
  uint16_t height() const { return display_.getDisplayHeight(); }

  void clear();

  // (x,y) is top-left. black=true draws ink (bit=0), false draws white.
  void setPixel(int x, int y, bool black);
  void fillRect(int x, int y, int w, int h, bool black);
  void drawRect(int x, int y, int w, int h, bool black);
  void drawHLine(int x, int y, int w, bool black);
  void drawVLine(int x, int y, int h, bool black);

  // Text is drawn with the built-in 5x7 font; unsupported characters
  // (anything outside space/-/./:// /0-9/A-Z, case-insensitive) draw blank.
  // scale=1 gives 5x7 px glyphs; scale=2 gives 10x14, etc.
  void drawText(int x, int y, const char* text, int scale = 1, bool black = true);
  int textWidth(const char* text, int scale = 1) const;
  int textHeight(int scale = 1) const;

  // mode: EInkDisplay::FULL_REFRESH / HALF_REFRESH / FAST_REFRESH.
  //
  // Also re-syncs the write buffer from the frame just displayed, so callers
  // can keep patching regions instead of re-rendering whole frames - see the
  // double-buffering note in the .cpp. Without that, anything drawn in an
  // earlier frame and not redrawn in this one vanishes every other refresh.
  void present(EInkDisplay::RefreshMode mode = EInkDisplay::FAST_REFRESH);

  // Puts the panel controller into its own deep sleep. Call after the final
  // present() and before cutting any rails - see PowerControl.cpp. The panel
  // retains whatever was last presented; waking needs a full begin() again,
  // which only happens on the next boot.
  void sleepPanel();

 private:
  EInkDisplay display_;
};
