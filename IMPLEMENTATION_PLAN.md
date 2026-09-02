# eNMEA Implementation Plan

Read this whole document before touching code. It is written for an agent
picking up this project cold, with no memory of how it got here.

## What this project is

A standalone firmware sketch that turns an Xteink X3/X4 board (ESP32-C3,
800x480 or 792x528 1-bit e-ink) into an NMEA 0183-over-Wi-Fi verification
tool: connect to a NMEA multiplexer over UDP or TCP, validate sentence
checksums, and show a dashboard (12-row sentence-ID checklist + an 8-box
Position/Speed/Course/Heading/Water Temp/Water Depth/Wind/AIS Targets grid)
refreshed every ~2s. See README.md's "Design decisions" for the AIS
live-count window and wind-sentence-precedence details.

It is **not** a CrossInk/CrossPoint fork or plugin, and does not import
CrossInk's activity/app framework, HAL layer, or `GfxRenderer`/`EpdFont`
text-rendering stack. It only reuses two low-level hardware libraries from
`freeink-sdk` (the open-source SDK CrossInk itself is built on): the e-ink
panel driver and board/device config. Read `README.md` in this repo first -
it covers the user-facing setup flow, the bring-up checklist, and the
design decisions already made (why no on-device keyboard, why no
`GfxRenderer` reuse, etc). This document is the forward-looking task list;
`README.md` is the record of decisions already settled.

## Where things live

- **This project**: wherever this file is (`platformio.ini` is a sibling).
- **`freeink-sdk`**: not vendored here - clone
  `https://github.com/Free-Ink/freeink-sdk.git` into `./freeink-sdk/` next
  to `platformio.ini` before building (see `README.md`). A reference clone
  also exists at `/home/chuck/CrossInk/freeink-sdk/` (populated as a git
  submodule of `/home/chuck/CrossInk/` - run `git submodule update --init
  freeink-sdk` there if it's ever empty again). Treat `/home/chuck/CrossInk`
  as read-only reference material, not a dependency of this project.
- **CrossInk firmware** (reference only, do not depend on it):
  `/home/chuck/CrossInk/`. Useful for cross-checking how a verified-working
  firmware calls the same SDK (e.g. `lib/hal/HalDisplay.cpp`,
  `lib/hal/HalGPIO.h`, `src/activities/network/WifiSelectionActivity.cpp`).
- **crossink-simulator** (reference only, integration not yet decided):
  `/home/chuck/crossink-simulator/`. **Not present on this machine as of
  2026-09-01** - the ledger notes below were written against a clone that is
  no longer there, so re-verify them against a fresh clone before acting on
  Task 3. `/home/chuck/CrossInk/` itself *is* present.

## Verified-facts ledger

Everything below was checked against real source, not inferred, as of this
plan being written. **Re-verify anything you rely on if enough time has
passed that `freeink-sdk` could have changed** (`git -C freeink-sdk log -1`
to check) - this ledger is a snapshot, not a live contract.

**2026-09-01 update**: the drift this section warns about happened. A fresh
clone of `freeink-sdk` (commit `fad70f28a982c978737410e535a4f7276ce28c19`)
added a hard requirement in `BoardConfig.h` for at least one
`-DFREEINK_DEVICE_<NAME>` build flag, which `platformio.ini` didn't pass -
`pio run -e x4`/`-e x3` failed outright with a `#error` before this was
fixed (now passes `-DFREEINK_DEVICE_X4=1` / `-DFREEINK_DEVICE_X3=1` per env
- see `platformio.ini`). Both `pio run -e x4` and `-e x3` build clean as of
this commit. Nothing else in this ledger needed changes for that update.

### `freeink::FreeInkDisplay` (aliased to `EInkDisplay` via `EInkDisplay.h`)

Source: `freeink-sdk/libs/display/FreeInkDisplay/include/FreeInkDisplay.h`,
`.../src/FreeInkDisplay.cpp`.

- Constructor: `FreeInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy)`.
- `enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH }`.
- `DISPLAY_WIDTH`/`DISPLAY_HEIGHT` = 800/480 (X4 family); X3 is a *different*
  panel with its own `X3_DISPLAY_WIDTH`/`X3_DISPLAY_HEIGHT` = 792/528 - the
  runtime `getDisplayWidth()`/`getDisplayHeight()` accessors reflect
  whichever panel `begin()` actually initialized, so **use those, not the
  static constants, anywhere layout math needs the real size**.
  **2026-09-01 correction**: this line previously claimed `EinkCanvas`
  already did this - it didn't. `width()`/`height()` called the static
  `DISPLAY_WIDTH`/`DISPLAY_HEIGHT` constants (always 800x480) regardless of
  which panel was actually initialized, so an `env:x3` build computed a
  wrong framebuffer row stride in `setPixel()` (100 bytes from width 800 vs
  the SDK's real 99-byte X3 rows) and corrupted every row after the first -
  this is almost certainly why an early X3 flash showed garbled diagonal
  black smears instead of the dashboard. Fixed in `EinkCanvas.h` to call
  `display_.getDisplayWidth()`/`getDisplayHeight()`.
- `void setDisplayX3()` - call before `begin()` to select the X3 controller.
  No call = X4 path. `eNMEA` handles this at compile time via the
  `ENMEA_BOARD_X3` build flag (`platformio.ini` `env:x3`) rather than
  `freeink-sdk`'s runtime `XteinkDetect` fingerprint probe - deliberately
  simpler, at the cost of the binary only working for the board it was
  built for.
- `void begin()`, `void clearScreen(uint8_t color = 0xFF) const`,
  `uint8_t* getFrameBuffer() const` - all confirmed, no surprises.
- `void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false)`
  - **has default arguments** (not just at the CrossInk `HalDisplay` wrapper
  layer as originally assumed while offline from the SDK - the SDK method
  itself defaults both params). `EinkCanvas::present()` passes both
  explicitly anyway; that's fine, not a bug, just no longer load-bearing.
- **DOUBLE-BUFFERED - `displayBuffer()` ends with `swapBuffers()`.**
  `getFrameBuffer()` therefore returns a *different allocation* after every
  present, and anything drawn in one frame but not redrawn in the next
  disappears on alternate refreshes. This bit this project hard: `drawChrome()`
  draws the box borders/labels once and `drawValues()` only patches value
  regions, so every other frame patched the buffer that never got the chrome -
  the boxes flickered on and off at the 2s cadence, with `setup()`'s
  "CONNECTING..." splash showing through from the stale buffer underneath.
  Fixed by calling `syncWriteBufferFromActive()` at the end of
  `EinkCanvas::present()`, which copies the just-displayed frame back into the
  write buffer (the SDK header documents the call for exactly this
  patch-and-redisplay pattern). One ~52 KB memcpy per refresh.
  **If you add any draw-once-persist-forever UI, that sync is what makes it
  legal.**
- Framebuffer format, confirmed in `FreeInkDisplay.cpp`'s `blitImage()`
  (`0x80 >> (x & 7)` masking, and its own `// 1 = white, 0 = black`
  comment): 1bpp, MSB-first per byte, row-major, `1=white`/`0=black`.
  `EinkCanvas::setPixel()` already implements this correctly.
- Async refresh exists (`displayBufferAsync`, `displayBufferAsyncNoShadow`,
  `waitRefreshComplete`, `supportsAsyncRefresh`) if a future redraw-latency
  optimization wants it. Not used yet - `EinkCanvas::present()` is
  synchronous/blocking, which is fine at a 2s update cadence.

### `InputManager` (buttons + touch)

Source: `freeink-sdk/libs/hardware/InputManager/include/InputManager.h`.

- `InputManager()` default-constructs (reads board pins from
  `BoardConfig::ACTIVE` internally - no pins passed by the caller).
- `void begin()`, `void update()` (call every loop iteration),
  `bool isPressed(uint8_t)`, `bool wasPressed(uint8_t)`,
  `bool wasReleased(uint8_t)`, `bool wasAnyPressed()`, `bool wasAnyReleased()`,
  `unsigned long getHeldTime()`, `unsigned long getPowerButtonHeldTime()`.
- Button indices: `BTN_BACK=0, BTN_CONFIRM=1, BTN_LEFT=2, BTN_RIGHT=3,
  BTN_UP=4, BTN_DOWN=5, BTN_POWER=6` - these are the library's own
  constants; CrossInk's `HalGPIO` just re-exports the same values under the
  same names, so there's no translation layer to reinvent.
- Touch API also exists (`hasTouch()`, `getTouchPoint()`, etc.) but is
  irrelevant here - X3/X4 have no touch controller.
- **Now in `lib_deps` and used** - see Task 2 below, which is done.
- Button availability on X3/X4 (`InputStyle::XteinkAdcLadder`), confirmed
  against `InputManager.cpp`'s ladder tables: `ADC_RANGES_1` decodes four
  buttons on `BUTTON_ADC_PIN_1` (BACK/CONFIRM/LEFT/RIGHT) and `ADC_RANGES_2`
  two on pin 2 (UP/DOWN, offset by +4), plus POWER as a plain GPIO. So all
  seven `BTN_*` constants are real on this hardware.
- `getPowerButtonHeldTime()` returns `millis() - powerButtonPressStart` **while
  the button is still down** (`InputManager.cpp:568`), so it works for a
  fires-mid-hold gesture. `getHeldTime()` does not - its span is first-press to
  final-release, as the header says. The BACK gesture times its own hold.

### `freeink::PowerManager` - the power-off path (now used)

Source: `freeink-sdk/libs/hardware/PowerManager/{include,src}/PowerManager.*`.

- `armPowerButtonWakeup()`, `armWakeOnPins()`, `waitForPowerButtonRelease()`,
  `powerDownRailsForSleep()`, `deepSleep()`, `deepSleepUntilPowerButton()`.
- Reads the wake pin and its polarity from `BoardConfig::ACTIVE.input`, and
  picks the SoC-correct wake source (`esp_deep_sleep_enable_gpio_wakeup` on the
  C3; ext1 on Xtensa parts). Nothing chip-specific is needed in this project.
- **Do not call `deepSleepUntilPowerButton()` on this hardware.** Its
  `deepSleep()` runs `esp_sleep_config_gpio_isolate()` *after* arming the wake
  source, which on the ESP32-C3 overwrites the power pin's sleep input config
  so short presses get missed. `src/PowerControl.cpp` does the steps by hand in
  CrossInk's order instead: wait for release -> isolate -> arm -> hold -> sleep.
  This is the same trap CrossInk documents in `HalPowerManager::startDeepSleep()`.

### `BoardConfig` - two calls that must happen first in `setup()`

- **`BoardConfig::holdPowerRails()`** asserts the battery latch
  (X4: `power.latch0` = GPIO13). Battery-latched revisions power off the moment
  the power button is released without it. It also `gpio_hold_dis()`es pins
  first, which is the *only* way to clear a hold left by a previous power-off -
  those survive a reset and a reflash, and silently make later `digitalWrite()`s
  no-ops.
- **`BoardConfig::releaseSdRail()`** powers the X3's SD rail (GPIO13,
  active-high). That rail shares the display SPI bus: left held off, the
  unpowered card clamps SCLK/MOSI and the panel never hears a command. The SDK
  header explicitly says apps that skip SD must call this before
  `display.begin()`. This project has no SD support, so nothing else would.
- X3 declares no `power.latch0` at all (it uses GPIO13 as `sd.powerEnable`),
  X4 declares no `sd.powerEnable`. Iterating both, skipping `PIN_UNASSIGNED`,
  covers the pair without board-specific branches.

### `BoardConfig` (general)

Source: `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`.

- Already a `lib_deps` entry (pulled in transitively by `FreeInkDisplay` for
  `BoardConfig::MAX_FRAMEBUFFER_BYTES` sizing) but nothing in this project's
  own code calls it directly yet.
- Has per-board predicates like `isX4Pro()`, `isSticky()`, etc., and an
  `ACTIVE`/`DEFAULT_DEVICE` profile system - only relevant if this project
  ever needs to support more than the X3/X4 pair it targets now (Sticky/X4
  Pro are S3+PSRAM boards; out of scope per the original design conversation
  unless asked for explicitly).

### Simulator (`crossink-simulator`) - read before attempting integration

This is **not** a generic "simulate any freeink-sdk consumer" tool. It ships
its own CrossInk-HAL-compatible shim layer (`HalDisplay.h/.cpp`,
`HalGPIO.h/.cpp`, etc. in `crossink-simulator/src/`) built specifically for
code that calls CrossInk's `HalDisplay`/`HalGPIO` classes, plus a *separate*,
unrelated no-op stub `EInkDisplay.h` that exists only so headers referencing
`EInkDisplay::SOMETHING` still compile - **that stub does not render
anything**. Actual SDL2 window output happens inside the simulator's
`HalDisplay.cpp` (confirmed: it owns its own static framebuffer, SDL
window/renderer/texture, and reads it directly - it does not delegate to the
stub `EInkDisplay`).

Consequence: `EinkCanvas` (`src/ui/EinkCanvas.h/.cpp`), which wraps the real
`EInkDisplay`/`FreeInkDisplay` directly, would compile against the
simulator's stub but **render nothing visible** - none of its calls reach
the SDL window. Getting real visual simulator output requires either:

(a) Giving `EinkCanvas` a `#ifdef SIMULATOR` path that goes through the
    simulator's own `HalDisplay` class instead of `EInkDisplay` - meaning
    this project would depend on `crossink-simulator` + a slice of
    CrossInk's HAL naming for simulator builds only, or
(b) Building a small SDL-backed `EInkDisplay`-shaped shim of this project's
    own, decoupled from CrossInk's HAL entirely, exercised only under
    `#ifdef SIMULATOR`.

Separately, on the networking side: `crossink-simulator/src/NetworkClient.h`
+`.cpp` (aliased to `WiFiClient` via `using WiFiClient = NetworkClient;` in
`WiFi.h`) is a **real host-backed TCP socket** (uses `getaddrinfo`/`socket`/
`connect`/`send` against actual OS sockets) - TCP-mode NMEA parsing is
genuinely testable in the simulator by pointing it at a local `nc -l` or
`socat` TCP source. `NetworkUDP` (`crossink-simulator/src/NetworkUdp.h`), by
contrast, is a **complete no-op stub** - `parsePacket()` always returns 0,
`read()` always returns 0 - and there is no `WiFiUDP` alias defined anywhere
in the simulator at all. **UDP mode cannot be exercised through the
simulator in its current form**, regardless of what's decided for (a)/(b)
above; it needs real hardware, or a unit test that bypasses the socket layer
entirely (see "Task: host-side parser unit tests").

Given the depth of (a)/(b), simulator integration is scoped as its own task
below, not a prerequisite for anything else - don't block other work on it.

## Tasks

Ordered by suggested priority. Each has a concrete "done" bar. Do not batch
unrelated tasks into one commit/PR - keep the diffs reviewable.

### Task 1: Host-side parser unit tests (no hardware, no simulator needed)

**Why first**: `NmeaLineReader` and `NmeaParser`
(`src/nmea/NmeaLineReader.{h,cpp}`, `src/nmea/NmeaParser.{h,cpp}`) are pure
C++ with no Arduino/ESP32 dependency except `millis()` (used for
timestamping fields in `NmeaParser.cpp`). They can be compiled and tested
with plain `g++`, entirely decoupled from hardware, the simulator, or
PlatformIO. This is the highest-confidence, lowest-cost verification
available for the part of the codebase most likely to have a subtle bug
(checksum math, field-index-off-by-one, lat/lon sign handling).

**What to do**:
1. Create `test/nmea_parser/` (a plain directory, not a PlatformIO
   `test_*` unless you also want `pio test` wired up - a standalone
   `g++ -std=c++20` invocation is enough for v1).
2. Stub `millis()` (a `static unsigned long fakeMillis = 0;` plus a
   returning function is enough - `NmeaParser.cpp` only calls it to
   timestamp fields, tests can ignore or check monotonicity loosely).
3. Write cases covering, at minimum:
   - Valid `$GPGGA` with a real checksum -> `data.hasPosition` true,
     lat/lon match hand-computed values, `result.checksumValid` true.
   - Same sentence with one bit of the checksum flipped -> `data` fields
     **unchanged** from their pre-call state, `result.checksumValid` false,
     `result.hasAddress` still true (so the caller can still count it as a
     checksum failure against the right ID - see `NmeaSource::handleByte`).
   - `$GPRMC` with status `V` (invalid fix) -> position/speed/course NOT
     updated even though the checksum is valid (mirrors the `fields[2][0] ==
     'A'` gate in `NmeaParser.cpp`).
   - `$GPVTG`, `$--HDT`, `$--HDG`, `$--MTW`, `$--DBT`, `$--DPT`, `$--MWV`,
     `$--MWD` - one valid case each, confirming the right struct field lands
     with the right value and the right `has*`/`headingIsTrue`/
     `windDirectionIsTrue` flags. For `MWV` also cover its 3 speed units
     (K/M/N) converting to the same knots value, and a `status` field of `V`
     leaving `data` untouched (mirrors the RMC status-`A` gate).
   - `!AIVDM` - a known test sentence with a hand-verified expected MMSI
     (e.g. `!AIVDM,1,1,,B,15M67FC000G?ufbE\`FepT@3n00Sa,0*5C` decodes to MMSI
     366053209 - a value independently verifiable against any AIS decoder,
     not just this parser) confirms `AisTargetTable` gets exactly one entry
     with that MMSI. Also cover: re-feeding the same sentence doesn't grow
     the target count (dedup by MMSI), and a fragment-2 sentence
     (`fields[2] != "1"`) is ignored rather than decoding garbage from a
     mid-message payload slice. `!AIVDO` should update the sentence
     checklist but must NOT add an AIS target - it's this vessel's own
     transponder output.
   - A sentence type outside the known set (e.g. `$GPGSA...*checksum`) -
     `result.hasAddress` true, `result.sentenceId` == `"GSA"`,
     `data` untouched (proves "seen but not decoded" sentences still get
     checksum-tracked, matching the `SentenceTable`/"OTHER:" line design in
     `Dashboard.cpp`).
   - Feed `NmeaLineReader` a stream with **two sentences run together with
     no CR/LF between them** (some multiplexers do this) and confirm it
     resyncs cleanly on the second sentence's `$`.
   - Feed it >82 bytes without a terminator - confirm it drops and resyncs
     without corrupting the next real sentence.
4. Compute at least 2-3 of the expected checksums by hand (XOR of the ASCII
   bytes between `$` and `*`) rather than trusting a copy-pasted example
   sentence's checksum blindly - the point of these tests is independent
   verification, not circular trust in whatever sample sentences you find
   online.

**Done when**: this exits 0 -

```sh
g++ -std=c++20 -Isrc -Itest/stubs test/nmea_parser/*.cpp src/nmea/*.cpp \
    -o /tmp/nmea_parser_test && /tmp/nmea_parser_test
```

- and a deliberately introduced bug (e.g. flip a `>=` to `>` in a field-count
guard in `NmeaParser.cpp`) makes at least one test fail - i.e. confirm the
tests actually exercise the logic, not just that they compile.

Note the two things an earlier draft of this bar got wrong: the parser sources
have to be *on the command line* (they aren't header-only), and
`NmeaParser.cpp` includes `<Arduino.h>`, which does not exist on the host - so
`test/stubs/Arduino.h` supplying `millis()` is a prerequisite, not an
afterthought.

The AIS vector in step 3 has since been independently confirmed: for
`!AIVDM,1,1,,B,15M67FC000G?ufbE\`FepT@3n00Sa,0*5C` the checksum really is `5C`,
the message type is 1, and the MMSI is 366053209.

### Task 2: On-device button input - DONE (2026-09-01)

Implemented as two hold gestures, both live in every mode (dashboard and
setup), handled by `handleGestures()` in `src/main.cpp`:

- **POWER held 2s -> shut down.** `src/PowerControl.cpp` draws a POWERED OFF
  screen, sleeps the panel, drops the battery latch, cuts switched rails and
  deep-sleeps armed to wake on the power button. See the PowerManager ledger
  entry above for why it does not use the SDK's convenience helper.
- **BACK held 3s -> erase settings and reboot into setup mode.** The web form
  grew an equivalent `POST /forget` button.

Both show a "KEEP HOLDING..." line in the status row about 500ms in, so
neither is a silent trap. The power gesture is armed only after the button has
been seen released once, so the press that wakes the device can't immediately
switch it back off.

`InputManager` and `PowerManager` are now in `[base] lib_deps`, and `setup()`
calls `BoardConfig::holdPowerRails()` / `releaseSdRail()` before touching any
other pin.

**Correction to this task's original rationale**: it claimed the only way back
into setup was erasing NVS, and that a mistyped password stranded the device.
Neither was true - `connectToWifi()` already fell back to the setup AP after a
15s failure. The real gap was narrower and is what actually got fixed: no way
back into setup when Wi-Fi *does* connect but the saved NMEA source is wrong
for where the device now is. The setup AP now stays up alongside the station
connection (`WIFI_AP_STA`), which addresses that directly; the button gesture
is the belt-and-braces path for when even that fails.

**Verified on hardware 2026-09-01**, both gestures:

- BACK held 3s erases the saved settings and the device comes back up in
  setup mode.
- POWER held 2s shuts the device down, which exercises the whole deep-sleep
  sequence behind it: panel sleep, battery-latch release, rail power-down and
  wake arming, in the hand-rolled order described above.

Task 2 is complete. The one thing worth re-checking over time is *repeated*
off/on cycling - a GPIO hold left set wrongly accumulates across cycles, so a
board that won't power back on (or wakes to a blank panel) after several
cycles would point at `holdPowerRails()` / `releaseSdRail()` or the latch
handling in `PowerControl.cpp`.

### Task 3: Simulator integration (optional, scoped separately)

See "Simulator (`crossink-simulator`)" in the ledger above for the full
constraint picture before starting. Recommended approach: option (b) there
(a small self-contained SDL shim) over (a) (depending on the simulator's
CrossInk-shaped `HalDisplay`), to preserve this project's decoupling from
CrossInk - but this is a judgment call the next agent/human should confirm
with the project owner before investing the time, since it's a real chunk
of new code (an SDL2 window + texture pipeline) for a project whose whole
premise was minimizing scope. If done:

1. New PlatformIO env (e.g. `env:simulator`) that does NOT pull in
   `freeink-sdk`'s `EInkDisplay` at all - swap `EinkCanvas`'s backing type
   behind a `#ifdef SIMULATOR` (mirroring the pattern CrossInk itself uses
   throughout `src/main.cpp`, e.g. its `setupDisplayAndFonts()`).
2. TCP-mode NMEA testing becomes possible against a real local `nc -l
   <port>` or `socat` feeding hand-written sentences.
3. Explicitly do **not** claim UDP works under this env - document the same
   `NetworkUDP` stub limitation from the ledger in this project's own
   README once this lands, so it doesn't get "fixed" by someone assuming
   it's a bug in this project's code rather than the simulator's.

**Done when**: `pio run -e simulator -t run_simulator`-equivalent (or
whatever launch mechanism is chosen) shows the actual dashboard layout in a
window, updating on a fed TCP stream, alongside a note in `README.md`
pointing out the UDP gap for that env.

### Task 4: Feature polish (do after 1-2 are solid; low individual risk)

Each of these is independent and can be picked up in any order:

- **HDG magnetic-to-true conversion.** `NmeaParser.cpp`'s `HDG` branch
  currently stores the raw magnetic reading and sets `headingIsTrue =
  false`. `HDG` also carries deviation/variation fields (`fields[2..5]`)
  that could compute a true heading. Only do this if the sign convention
  for deviation E/W and variation E/W is confirmed against the NMEA 0183
  spec (or a real GPS/compass's live output) - getting the sign backwards
  silently produces a plausible-looking but wrong heading, which is worse
  than the current honestly-labeled-magnetic value for a *verification*
  tool. Cross-check against a second source before trusting your own
  derivation.
- **Fahrenheit / feet unit options.** `Dashboard.cpp`'s water temp and depth
  boxes are Celsius/meters only. Likely a `AppSettings` field (new
  `Preferences` key, see `AppSettings.cpp`'s existing key pattern) plus a
  form field in `ProvisioningPortal.cpp`'s `sendForm()`/`handleSave()`, plus
  a conversion at the point `Dashboard::drawValues()` formats the string.
- **Diff-based redraw.** `main.cpp`'s `loop()` currently redraws the whole
  dashboard on a fixed timer regardless of whether any value changed. If
  panel wear or visible ghosting from the every-2s `FAST_REFRESH` becomes a
  concern on real hardware, compare old vs. new formatted strings per box in
  `Dashboard::drawValues()` and skip `EinkCanvas::present()` when nothing
  changed. Don't do this preemptively without a real observed problem -
  it's added complexity for a hypothetical.
- ~~**Connection-state nuance.**~~ DONE. `NmeaSource` now exposes a
  `SourceState` (`LISTENING`/`CONNECTING`/`CONNECTED`/`NO DATA`/`FAILED`) and
  tracks `lastRxMs()`. This also fixed a latch bug: UDP's old `connected_` flag
  was set on the first packet and never cleared, so a dead feed read
  "CONNECTED" indefinitely.
- **Surface the TCP failure reason on screen.** Right now `FAILED` is all the
  panel says; which of "wrong host", "server not running" or "different
  subnet" it was only reaches the serial log. The device's own IP is already
  on screen, so showing the target it couldn't reach next to it would close
  most of the gap.

### Task 5: Repo hygiene - DONE (2026-09-01)

`git init` done on 2026-09-01; branch `main`, initial commit `7b63dea` holding
the full scaffold. Confirmed with the project owner that this is its own repo
rather than part of a larger one.

`.gitignore` covers `.pio/`, `freeink-sdk/`, `__pycache__/`, `.vscode/`,
`compile_commands.json` and `platformio.local.ini`. Verified nothing from
`freeink-sdk/` or `.pio/` was staged, and that no credentials are in tracked
files (the device's Wi-Fi password lives only in NVS on the board).

The `freeink-sdk` pin is now enforced by an explicit `git checkout <commit>`
in README's setup instructions rather than an unpinned `git clone` - see that
section, which also documents how to convert it to a real submodule if a
README line isn't strong enough.

**Remaining**: no remote is configured. Add one when there's somewhere to push.

## Non-goals (don't reintroduce scope that was deliberately cut)

- No CrossInk `GfxRenderer`/`EpdFont`/`FontCacheManager` reuse - see
  README's "Design decisions" section for why.
- No on-device text entry for SSID/password - web form only, by design, not
  an oversight to "fix."
- No support for Sticky/X4 Pro (S3+PSRAM) boards unless explicitly
  requested - this project was scoped for the plain ESP32-C3 X3/X4 pair.
- No CrossInk activity/app framework dependency of any kind, including
  through the simulator (see Task 3's emphasis on not depending on the
  simulator's `HalDisplay`).

## How to verify you haven't broken anything

**Hardware-verified 2026-09-01 evening** (see `handoff-home.md`): both envs
build clean and the X3 was flashed and run end to end - boots as
`xteink_x3 792x528`, joins Wi-Fi, connects to a TCP NMEA source on the first
attempt, decodes and displays live data on a 2s redraw. PlatformIO lives in a
venv at `~/.venvs/pio/bin/pio` (system Python is externally managed); note the
Dropbox-synced `.pio/build/` tree can hold stale artifacts from another machine.

**Still unverified on hardware**: the power-off gesture and the BACK-hold
settings erase. Test those on battery, unplugged - see check 4 below.

There is no CI here yet. Minimum bar before considering any task's changes
"done":

1. `pio run -e x4` builds clean (add `-e x3` too if you touched anything
   `ENMEA_BOARD_X3`-conditional).
2. If you touched `src/nmea/*`, re-run the Task 1 unit tests.
3. If you touched anything under `src/ui/*`, flash real hardware if
   available and eyeball the dashboard - there is no automated visual
   check, and the simulator gap (Task 3) means there's no shortcut around
   this yet. For font changes specifically, `python3 scripts/preview_font.py`
   renders the generated tables as terminal ASCII art, which catches missing
   or malformed glyphs without hardware.
4. If you touched the power path (`src/PowerControl.cpp`, the `setup()` rail
   calls), test on **battery, unplugged from USB** - on USB power the latch cut
   is invisible because the USB rail keeps the chip fed, so a broken shutdown
   looks identical to a working one. Confirm: the device stays on after you
   release the power button at boot; a 2s hold powers it down; a press wakes
   it; and it survives a few off/on cycles (a latch or GPIO hold left set
   wrongly shows up as a board that won't turn back on, or a panel that stays
   blank after wake).
5. If you touched `src/net/*`, run `scripts/nmea_test_server.py` in both
   `--proto tcp` and `--proto udp` and confirm the checklist ticks up and the
   status row reaches `CONNECTED` in each.
6. Update `README.md` if you changed user-facing behavior (setup flow,
   supported sentence types, unit options) or a design decision documented
   there. Update this file's "Tasks" section (mark done / remove / add
   follow-ups) so the next agent isn't working from a stale list.
