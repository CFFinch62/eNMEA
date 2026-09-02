#!/usr/bin/env bash
# Builds the browser-based installer in docs/ - the thing that lets someone with
# no toolchain flash an X3 from a web page.
#
#   scripts/build_web_installer.sh
#
# Produces docs/firmware/eNMEA-x3-<version>.bin (a single image containing
# bootloader + partition table + otadata + app, flashable at offset 0) and the
# manifest.json that ESP Web Tools reads. Commit the result and GitHub Pages
# serves it.
#
# Override the tool paths if yours live elsewhere:
#   PIO=/path/to/pio ESPTOOL=/path/to/esptool scripts/build_web_installer.sh
set -euo pipefail
cd "$(dirname "$0")/.."

PIO="${PIO:-$HOME/.venvs/pio/bin/pio}"
ESPTOOL="${ESPTOOL:-$HOME/.venvs/pio/bin/esptool}"
command -v "$PIO" >/dev/null 2>&1 || { echo "error: pio not found at $PIO (set PIO=...)"; exit 1; }
command -v "$ESPTOOL" >/dev/null 2>&1 || { echo "error: esptool not found at $ESPTOOL (set ESPTOOL=...)"; exit 1; }

echo "==> Building firmware (env:x3)"
"$PIO" run -e x3

# boot_app0.bin ships with the Arduino framework package, not with this project.
BOOT_APP0="$(find "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions" \
             -name boot_app0.bin 2>/dev/null | head -1)"
[ -n "$BOOT_APP0" ] || { echo "error: boot_app0.bin not found in the Arduino framework package"; exit 1; }

VERSION="$(git describe --tags --always --dirty 2>/dev/null || date +%Y.%m.%d)"
DATE="$(date +%Y-%m-%d)"
OUT="docs/firmware/eNMEA-x3-${VERSION}.bin"

echo "==> Merging flash image  (version ${VERSION})"
mkdir -p docs/firmware
rm -f docs/firmware/eNMEA-x3-*.bin   # keep exactly one build in the repo

# Offsets are not guesses. 0x0 is what esptool itself reports as the ESP32-C3
# bootloader offset (ESP32C3ROM.BOOTLOADER_FLASH_OFFSET - it is 0x1000 on the
# classic ESP32, which is the usual way to get this wrong). 0xe000 and 0x10000
# are the otadata and app0 partitions as declared in this build's own
# partitions.bin. 0x8000 is the fixed partition-table location.
#
# --flash-mode/freq/size are all "keep" so the merged image behaves exactly like
# what `pio run -t upload` writes, rather than re-deriving the header here.
"$ESPTOOL" --chip esp32c3 merge-bin \
  -o "$OUT" \
  --flash-mode keep --flash-freq keep --flash-size keep \
  0x0     .pio/build/x3/bootloader.bin \
  0x8000  .pio/build/x3/partitions.bin \
  0xe000  "$BOOT_APP0" \
  0x10000 .pio/build/x3/firmware.bin

echo "==> Writing manifest"
cat > docs/manifest.json <<JSON
{
  "name": "eNMEA (Xteink X3)",
  "version": "${VERSION}",
  "home_assistant_domain": null,
  "funding_url": null,
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-C3",
      "parts": [
        { "path": "firmware/eNMEA-x3-${VERSION}.bin", "offset": 0 }
      ]
    }
  ]
}
JSON

# Keep the page's displayed version honest without hand-editing HTML.
if [ -f docs/index.html ]; then
  sed -i "s|<span id=\"version\">[^<]*</span>|<span id=\"version\">${VERSION}</span>|" docs/index.html
  sed -i "s|<span id=\"built\">[^<]*</span>|<span id=\"built\">${DATE}</span>|" docs/index.html
fi

echo
echo "Built ${OUT} ($(du -h "$OUT" | cut -f1))"
echo "Commit docs/ and push; GitHub Pages serves the installer."
