# eNMEA

NMEA 0183 over Wi-Fi verification tool, running on Xteink X3/X4 hardware
(ESP32-C3; 800x480 e-ink on the X4, 792x528 on the X3) as a bare dev-kit
target.

This is **not** a CrossInk/CrossPoint fork or plugin. It shares no code with
that firmware's app layer, only two low-level hardware libraries from the
open-source `freeink-sdk` that CrossInk itself is built on (the e-ink panel
driver and board detection). If you never plan to use this board as an
e-reader again, you don't need CrossInk at all - just this project and
`freeink-sdk`.

## What it does

- Connects to a Wi-Fi network you configure.
- Listens for NMEA 0183 sentences over UDP (broadcast) or connects out to a
  TCP NMEA server, whichever you configure.
- Validates each sentence's checksum and tracks, per sentence ID, how many
  valid/invalid sentences have been seen and how recently. This applies to
  every sentence that starts with `$` (NAV data) or `!` (AIS data), whether or
  not this tool knows how to decode its contents.
- Creates a list of valid sentence IDs (GGA, RMC, VTG, VDO, VDM, etc) received.
- Decodes the twelve of those it has dashboard boxes for (below); everything
  else is counted and listed on the `OTHER:` line but not interpreted.
- Uses data from GGA/RMC (position, fix), RMC/VTG (speed, course), HDT/HDG
  (heading), MTW (water temp), DBT/DPT (water depth), MWV/MWD (wind speed
  and direction), and VDM (AIS targets - a live count of distinct MMSIs
  heard in the last 6 minutes, not a cumulative since-boot total) to display
  into an 8-box dashboard grid (POSITION / SPEED / COURSE / HEADING /
  SEA TEMP / DEPTH / WIND / AIS TGTS - labels are kept short so they can be
  drawn at a legible size).
- Shows battery percentage in the status row, read from the X3's BQ27220 fuel
  gauge over I2C. Worth watching: a constant Wi-Fi station, an always-on SoftAP
  and a 2-second refresh draw far more than the e-reader firmware did.
- Redraws the dashboard every 2 seconds using the e-ink panel's fast partial
  refresh, with a `HALF_REFRESH` repaint roughly every 60s to clear ghosting.
  (`FULL_REFRESH` is used only for the first paint and the power-off screen -
  it's slower and flashes the panel.)
- Shuts down on a 2-second hold of the power button, and erases its settings
  on a 3-second hold of BACK - see "Powering off" and "Getting back to setup".
- All settings (Wi-Fi SSID/password, protocol, host, port) are entered
  through a small web form the device serves, not on-device text entry -
  see "Why no on-device buttons yet" below.

## Getting freeink-sdk

This repo does not vendor `freeink-sdk` (it's git-ignored). Clone it
alongside `platformio.ini` **and check out the pinned commit**:

```sh
git clone https://github.com/Free-Ink/freeink-sdk.git
git -C freeink-sdk checkout fad70f28a982c978737410e535a4f7276ce28c19
```

You should end up with `./freeink-sdk/libs/...` next to this README.

The checkout line is not optional ceremony. This project builds against
upstream `main`, and that has already broken it once: a commit added a hard
requirement in `BoardConfig.h` for a `-DFREEINK_DEVICE_<NAME>` build flag and
`pio run` started failing with an `#error` before anything compiled. Cloning
`main` gets you whatever it happens to be today; the pin above is the commit
this project was last verified working against (2026-09-01, hardware-tested
on an X3).

To move to a newer SDK deliberately, check out the newer commit, build both
envs, flash, and update the hash here. If a fresh clone fails with a
`BoardConfig.h` `#error` about no `FREEINK_DEVICE_<NAME>` selected, that's
the same drift recurring - `platformio.ini` already passes
`-DFREEINK_DEVICE_X4`/`-DFREEINK_DEVICE_X3` per env, so check whether
upstream renamed or added a required flag.

If you'd rather have git enforce the pin than trust a README line, make the
SDK a submodule: drop `/freeink-sdk/` from `.gitignore`, then
`git submodule add https://github.com/Free-Ink/freeink-sdk.git` and commit it
at the pinned commit. That costs a `--recurse-submodules` on every clone,
which is the trade.

CrossInk itself is only a reference for engineering decisions (see "Design
decisions" below) - nothing here depends on it being present.

## Installing without a toolchain (for end users)

`docs/` is a browser-based installer built on
[ESP Web Tools](https://esphome.github.io/esp-web-tools/): plug the X3 into a
computer, open the page in Chrome or Edge, press Install. No Python, no
PlatformIO, no drivers, no command line. GitHub Pages serves it over HTTPS,
which Web Serial requires.

Rebuild it after a firmware change:

```sh
scripts/build_web_installer.sh   # builds env:x3, merges, regenerates the manifest
git add docs && git commit && git push
```

That produces a single `docs/firmware/eNMEA-x3-<version>.bin` holding
bootloader + partition table + otadata + application, flashable at offset 0 -
so it also works with plain `esptool --chip esp32c3 write-flash 0x0 <file>` for
anyone who prefers a terminal.

**Limits worth knowing before pointing someone at it**: Web Serial is
Chrome/Edge on desktop only - not Safari, not Firefox, not any phone browser.
On Linux the user still needs serial-port permission (`dialout` group or the
PlatformIO udev rules), the same hurdle as flashing locally. The page says both.

**Installing resets saved settings.** The merged image spans the NVS partition
at `0x9000`, and esptool erases sectors before writing, so a fresh install
always comes up in `SETUP MODE`. Verified on hardware, not assumed. That is the
right behaviour for a new device; it does mean a firmware *update* costs the
user a re-provision.

**Going back to the e-reader** is a link, not a hosted binary. CrossInk has its
own one-click web installer at
[inky.crossink.dev](https://inky.crossink.dev/#flash-tools), which serves the
current official build for each device model. Hosting copies here would mean
shipping someone else's firmware, going stale, and owning support for it.
Nothing eNMEA does blocks the round trip - flashing rewrites internal flash
only, and the SD card is never touched.

## Build & flash

```sh
pio run -e x4 -t upload    # X4 / X4 Pro panel (default)
pio run -e x3 -t upload    # X3 panel
pio device monitor -e x4
```

Board is `esp32-c3-devkitm-1` / ESP32-C3, matching the X3/X4's actual MCU.

## First boot / setup flow

1. On first boot (no saved settings), the panel shows a QR-free "SETUP MODE"
   screen and the device starts a Wi-Fi access point named `eNMEA-Setup`.
2. Connect a phone or laptop to that AP, browse to `http://192.168.4.1/`.
3. Fill in your Wi-Fi SSID/password, pick UDP or TCP, and set the port (and
   host, for TCP). Submit - the device saves the settings to flash (NVS) and
   reboots.
4. It reconnects to your real Wi-Fi and starts the dashboard. The device's
   own IP is shown on screen, in the status row.
5. The settings form stays reachable for the whole session **two ways**: at
   `http://<device-ip>/` on your LAN, and at `http://192.168.4.1/` over the
   `eNMEA-Setup` AP, which stays up alongside the Wi-Fi connection. The AP is
   the one that matters - it works even when the saved settings are wrong for
   wherever the device currently is.

## Getting back to setup

Three ways in, in order of how little has to be working for them to succeed:

1. **The setup AP.** `eNMEA-Setup` is up whenever the device is on, whether or
   not it joined your Wi-Fi. Join it and browse to `http://192.168.4.1/`.
2. **Hold BACK for 3 seconds** on the device. It erases the saved settings and
   reboots into setup mode. A "KEEP HOLDING TO ERASE SETTINGS" line appears in
   the status row about half a second in, so the gesture isn't a silent trap.
   The settings page has an equivalent "Forget settings & reboot to setup"
   button.
3. **Erase NVS over USB**: `pio run -t erase`. Only needed if the firmware
   itself won't start.

A wrong Wi-Fi password does *not* strand the device: `connectToWifi()` gives up
after 15 seconds and falls back to the setup AP on its own.

## Powering off

Hold the power button for 2 seconds. The device draws a "POWERED OFF" screen,
puts the panel controller to sleep, drops the battery latch and enters deep
sleep armed to wake on the same button. Pressing power again is a full cold
boot.

Two things worth knowing:

- **On USB power, "off" means deep sleep, not dead.** The battery latch cut
  (X4 GPIO13) only removes battery power; the USB rail keeps the chip fed at
  deep-sleep current. Unplug it if you want the board genuinely unpowered.
- **The screen keeps showing "POWERED OFF" while it's off.** That's e-ink
  doing its job, not the device still running.

`setup()` calls `BoardConfig::holdPowerRails()` before anything else touches a
pin. On X4 revisions that don't self-latch, skipping that call means the board
dies the moment you release the power button after turning it on; it also
releases GPIO holds left behind by a previous power-off (this firmware's or
CrossInk's), which survive a reset and silently defeat later writes.

## UDP vs TCP

Not just a dropdown - the two behave differently, matching how real
NMEA-over-IP multiplexers work. **UDP** makes the device *listen* on the
configured port for broadcast traffic; it never dials out, and the Host field
is ignored entirely. **TCP** makes the device *dial out* to `host:port` as a
client, the way you'd point it at a multiplexer's TCP server mode.

Get this backwards and the dashboard sits at `SOURCE: LISTENING` forever with
no error - a UDP listener waiting for broadcasts that a TCP server will never
send. That's the first thing to check when no data shows up. The status row
tells the two failure shapes apart:

| Status | Means |
| --- | --- |
| `LISTENING` | UDP socket open, nothing has ever arrived |
| `CONNECTING` | TCP dial-out in progress, or waiting out the 3s retry gap |
| `CONNECTED` | socket up, bytes arriving |
| `NO DATA` | socket still up, but nothing for 10s - the source went quiet |
| `FAILED` | TCP connect refused/unreachable, or the UDP bind failed |

## Bring-up checklist (run through this before trusting anything on screen)

Everything below except font glyphs has now been checked against the real
`freeink-sdk` source - see `IMPLEMENTATION_PLAN.md` for the verification
notes.

1. **Framebuffer polarity - confirmed correct.** `EinkCanvas` assumes the
   panel's 1bpp buffer is MSB-first with `1=white, 0=black`. Confirmed
   against `FreeInkDisplay.cpp`'s `blitImage()` (`0x80 >> (x & 7)` masking
   and its own `// 1 = white, 0 = black` comment). No change needed.
2. **Font glyphs - checkable without hardware now.**
   `src/ui/Font5x7.{h,cpp}` is a small hand-designed 5x7 bitmap font (space,
   `- . : / ( )`, `0-9`, `A-Z`), generated by `scripts/gen_font.py`. Render it
   as terminal ASCII art with `python3 scripts/preview_font.py` (no args for
   the whole set, or pass a string). That reads the *generated* byte tables, so
   it catches a broken generator as well as broken art. Fix wrong glyphs by
   editing the row-art in `gen_font.py` and rerunning it - don't hand-edit the
   generated `.cpp` byte tables.

   `(` and `)` were missing until recently, which blanked out the `(n BAD)`
   checksum-failure counter in the sentence checklist - the one thing this tool
   most needs to show. Anything you add to a format string in `Dashboard.cpp`
   needs a glyph, or it silently draws nothing.
3. **X3 vs X4 panel init - confirmed and handled.** `FreeInkDisplay.h`
   confirms `setDisplayX3()` is the real method, called before `begin()`.
   `EinkCanvas::begin()` now calls it when built with `env:x3`
   (`-DENMEA_BOARD_X3`). Build `env:x4` (default) for X4/X4 Pro.
4. **SPI pins.** `src/BoardPins.h` mirrors CrossInk's `lib/hal/HalGPIO.h`
   pin-out (SCLK=8, MOSI=10, CS=21, DC=4, RST=5, BUSY=6). Verified against
   that file, not against a datasheet or continuity test - double check
   against your actual board revision if the panel does nothing at all.
5. **NMEA source.** `scripts/nmea_test_server.py` speaks both transports and
   exercises every decode path (it emits GGA/RMC/VTG/HDT/MTW/DBT/MWV/MWD/AIVDM
   plus GSA/GLL for the `OTHER:` line, and one deliberately-corrupted sentence
   every 20s so the `(n BAD)` counter has something to show):

   ```sh
   python3 scripts/nmea_test_server.py                # TCP - device dials in
   python3 scripts/nmea_test_server.py --proto udp    # UDP broadcast
   ```

   It prints this machine's IP at startup, which is exactly what goes in the
   device's Host field for TCP. **That address changes when you move between
   networks** - a host saved at one site will never answer at another, and the
   symptom is `SOURCE: FAILED` with no other clue. Watch the serial monitor:
   every failed TCP attempt logs the target, the device's own IP and gateway,
   and the three things worth checking.

   Or point any real tool at it (OpenCPN, `socat`, a phone nav app with
   NMEA-out) and confirm the "SENTENCES SEEN" list starts ticking up. If
   everything says `--`, check UDP-vs-TCP semantics above before assuming the
   parser is broken.

## Tests

```sh
test/run_tests.sh
```

Host-side tests for the framing and parsing layer - plain `g++`, no hardware,
no PlatformIO, about a second to run. `src/nmea/*.cpp` is pure C++ apart from
`millis()`, which `test/stubs/Arduino.h` supplies; that is what makes this
possible, so keep Arduino types out of that layer.

Run them after any change under `src/nmea/`. See `IMPLEMENTATION_PLAN.md`
Task 1 for what they cover and for the mutation-testing results that show they
actually catch regressions.

## Design decisions worth knowing about

- **No CrossInk `GfxRenderer`/`EpdFont` reuse.** That stack is built on
  CrossInk's own `HalDisplay`/`HalGPIO` and pulls in `FontCacheManager`,
  `FontDecompressor`, compressed font assets, and i18n machinery meant for
  paginated book text. A dashboard of eight short numeric/label boxes doesn't
  need any of that, so `EinkCanvas` draws straight onto `EInkDisplay`'s raw
  framebuffer with a minimal built-in font instead. Less capable, but zero
  coupling to CrossInk internals.
- **Buttons do two things only: power off, and erase settings.** Both are
  holds, not taps - a tap is too easy to hit by accident on a device whose
  only feedback is a 2-second e-ink redraw. SSID/password entry stays
  web-based: typing a WPA2 password on a 7-button ADC ladder is bad UX even
  with a working keyboard component, so the device-side escape hatch is
  "erase and start over", not on-device editing. The X3/X4 ladder does expose
  BACK/CONFIRM/LEFT/RIGHT (ADC pin 1) and UP/DOWN (ADC pin 2) plus POWER on
  GPIO3, so there's room for more gestures if a real need shows up.

- **The setup AP stays up alongside the Wi-Fi connection (`WIFI_AP_STA`).**
  A station-only settings page is unreachable in exactly the situation you
  need it: the saved config is wrong for where the device is now. Keeping the
  AP up costs some idle current and forces the AP onto the station's channel;
  for a bench tool that must never become unreachable, that's the right trade.
- **The display is double-buffered; partial redraws need a sync.**
  `EInkDisplay::displayBuffer()` ends with `swapBuffers()`, so
  `getFrameBuffer()` returns a different allocation after every present.
  Anything drawn once and expected to persist (the box chrome) vanishes on
  alternate frames unless the write buffer is re-synced from what was just
  displayed. `EinkCanvas::present()` calls `syncWriteBufferFromActive()` to
  maintain the invariant that the write buffer matches the panel. Remove it and
  the dashboard flickers every other refresh.

- **Text is drawn at two sizes only.** Everything read at a glance - the
  sentence checklist, source state, battery, box labels and values - is scale 2
  (10x14 px, 2px strokes). Scale 1 survives only for reference text you walk up
  to: the source address and the footer hints. The 5x7 font's 1px strokes are
  genuinely hard to read at scale 1 on this panel. Box labels are constrained by
  what fits at scale 2 inside a column, which is why they're short.

- **Redraw policy is time-based, not diff-based.** The dashboard redraws
  everything every 2s rather than comparing old/new values and only
  touching changed boxes. Simpler, and fine at this update rate; if panel
  wear or partial-refresh ghosting becomes a concern, diffing before
  redrawing is a contained change in `loop()` (`src/main.cpp`) plus
  `Dashboard::drawValues()`.
- **DPT sentence depth offset is ignored.** `DPT`'s second field (offset to
  waterline or keel) has an instrument-configured sign convention this tool
  has no way to know; showing the raw depth-below-transducer value instead
  of guessing wrong seemed more honest for a "verification" tool. See the
  comment in `src/nmea/NmeaParser.cpp`.
- **HDG heading is shown as magnetic, not corrected to true.** True heading
  from `HDG` would need applying its deviation/variation fields; v1 just
  shows the raw sensor reading, labeled `M`. `HDT` (already true) is shown
  as `T`.
- **AIS target count is a live 6-minute window, not a cumulative total.**
  `AisTargetTable::liveCount()` only counts MMSIs heard within the last
  `AIS_TARGET_STALE_MS` (360000ms, `src/nmea/NmeaTypes.h`) - chosen because
  Class B units can go that long between static/position reports while
  otherwise still present. A target that's gone quiet longer than that drops
  out of the count rather than inflating it forever. Only `VDM` (messages
  from other stations) counts targets; `VDO` (this vessel's own transponder
  output) does not, but both still get their own row in the sentence-ID
  checklist.
- **Wind box shows whatever MWV/MWD sent last, unconverted beyond knots.**
  Like RMC/VTG for speed/course, whichever of MWV or MWD arrives most
  recently wins - there's no cross-checking between them. MWV's direction is
  labeled `T` or `R` (true vs. relative-to-bow, per its own reference
  field); MWD's is always `T`. MWV speed is normalized to knots from
  whatever unit field it carries (K=km/h, M=m/s, N=knots already).

## License

MIT - see `LICENSE`. The two projects this one builds on or references are MIT
too: `freeink-sdk` (the hardware libraries this links against) and CrossInk
(reference material only; nothing here depends on it).

## What's still rough (known gaps, not hidden)

- No Fahrenheit option for water temp, no feet option for depth - Celsius
  and meters only in v1.
- `AppSettings::host` is unused in UDP mode; the web form doesn't grey it
  out, it's just labelled "TCP only" and ignored server-side.
- The status row reports `LISTENING` / `CONNECTING` / `CONNECTED` / `NO DATA`
  / `FAILED`, but nothing on screen says *why* a TCP connect failed - that
  detail only goes to the serial log.
- Nothing is shown on screen while the device is between dashboard redraws, so
  a button hold can take up to ~2s to acknowledge visually.
- The "sentences seen" checklist tracks at most `MAX_TRACKED_SENTENCE_IDS`
  (20) distinct sentence IDs total (12 dedicated rows + up to 8 more
  summarized on the `OTHER:` line); a feed sending more distinct types than
  that stops gaining new tracked rows once the table is full (already-
  tracked IDs keep updating). Not expected to matter for a typical
  multiplexer feed, but worth knowing if `OTHER:` ever looks incomplete.
