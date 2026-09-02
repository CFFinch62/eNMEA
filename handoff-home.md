# Handoff — 2026-09-01 evening (home session)

Written for whoever picks this up next, including me-in-a-week. This is the
session where eNMEA went from "never worked end to end" to "works on the
bench." It records what was broken, what the actual causes were, and the
things that would be expensive to rediscover.

**State at end of session:** working, and every code path exercised on the
device. The X3 boots, joins Wi-Fi, connects to a TCP NMEA source, decodes and
displays live data, shows battery charge from the fuel gauge, and can be
switched off and re-provisioned entirely from the buttons. Nothing here is
"compiles, should work".

Beyond the firmware, the evening also produced: a git repo pushed to
<https://github.com/CFFinch62/eNMEA>, 124 host-side parser tests (mutation-tested
with seven injected bugs, all caught), a one-click browser installer at
<https://cffinch62.github.io/eNMEA/> with a support FAQ, and a verified round
trip to CrossInk and back - run twice, no errors.

---

## Where things stood at the start

Three complaints, all real:

1. Couldn't shut the device down — no software power-off existed at all.
2. Once connected to Wi-Fi, no way back into setup to point it somewhere else.
3. Never connected to the TCP test server.

Plus context from earlier in the day: the device **never connected to Wi-Fi at
the office**, but **did connect at home**. That mostly exonerates the firmware
— the office network is very likely 5 GHz-only or otherwise unsuitable, and the
ESP32-C3 has no 5 GHz radio. The Wi-Fi scan logging added earlier (it prints
every visible SSID with a match marker) is what will confirm that next time
the device is at the office.

---

## The five firmware bugs

### 1. No power-off path, and the power rails were never held

`setup()` never called `BoardConfig::holdPowerRails()`. Two consequences: on X4
revisions that don't self-latch the board dies when you release the power
button, and — more relevant here — **GPIO holds left by a previous power-off
survive a reset and a reflash**, silently turning later `digitalWrite()`s on
those pins into no-ops. `releaseSdRail()` matters on the X3 specifically: its
SD rail is GPIO13 and shares the display SPI bus, so a rail left held off lets
the unpowered card clamp SCLK/MOSI and the panel never hears a command.

Both are now the first two calls in `setup()`, before anything touches a pin.

Power-off itself is `src/PowerControl.cpp`. **Do not replace it with
`PowerManager::deepSleepUntilPowerButton()`** — that helper's `deepSleep()`
calls `esp_sleep_config_gpio_isolate()` *after* arming the wake source, which on
the ESP32-C3 overwrites the power pin's sleep input config so short presses get
missed. CrossInk hit this and documents it in
`HalPowerManager::startDeepSleep()`; we mirror their ordering by hand: wait for
release → isolate → arm → hold → sleep.

### 2. The setup page was unreachable exactly when it was needed

The portal ran station-only, so it was reachable only from the network the
device had already joined — useless when the saved config is wrong for where
you are. It now runs `WIFI_AP_STA`: the `eNMEA-Setup` AP stays up for the whole
session alongside the Wi-Fi connection. Plus a `POST /forget` button on the
page and a 3-second BACK hold on the device.

Cost of the always-on AP: the C3 shares one radio between SoftAP and station.
Measured ~20% TCP retransmission with the congestion window pinned at the
minimum. Harmless at NMEA data rates — data kept up fine — but it is the first
thing to suspect if throughput ever matters. Making the AP stoppable after
configuration is the obvious mitigation if so.

### 3. The dashboard flickered — the display is double-buffered

**This is the one that would cost someone a whole evening to rediscover.**

`EInkDisplay::displayBuffer()` ends with `swapBuffers()`, so `getFrameBuffer()`
returns a *different allocation* after every present. eNMEA's design assumed a
single persistent framebuffer: `drawChrome()` draws the box borders and labels
**once**, and every later frame only patches value regions.

Result: on alternating frames it patched the buffer that never received the
chrome — and which still held `setup()`'s `"CONNECTING..."` splash text. On
screen the boxes flickered on and off at the 2-second redraw cadence with
"CONNECTING" showing through underneath.

Fix: `EinkCanvas::present()` now calls `display_.syncWriteBufferFromActive()`
after displaying, which copies the just-shown frame back into the write buffer.
That restores the invariant every drawing call site already assumed — *the
write buffer matches what is on the panel* — so partial redraws stay valid. One
~52 KB memcpy every 2 seconds. The SDK header documents the call for exactly
this pattern.

**Rule of thumb:** anything drawn in one frame and not redrawn in the next will
vanish every other refresh unless that sync happens.

### 4. Latched UDP connection state

The old `connected_` flag was set on the first UDP packet and never cleared, so
a dead feed read "CONNECTED" forever. Replaced with a real `SourceState`
(`LISTENING` / `CONNECTING` / `CONNECTED` / `NO DATA` / `FAILED`) plus
`lastRxMs()`. This is also what makes the "no data" case diagnosable at a
glance instead of ambiguous.

### 5. TCP socket leak on retry

`pollTcp()` re-dialled without `stop()`. A `WiFiClient` that failed to connect
still owns its lwIP fd, and the stack has ~10; leaking one per 3-second retry
eventually wedges every attempt with an error indistinguishable from "server
not there." Now `stop()`s first, uses a bounded 4-second connect timeout so a
wrong host can't stall `loop()`, and logs every attempt with the target, the
device's own IP and gateway, and the three things worth checking.

---

## Why it "never connected to the TCP server"

Never definitively proven — NVS was erased before the evidence could be read,
which in hindsight was the wrong call. Both candidates are now impossible to
hit silently:

- **Protocol mismatch.** `AppSettings` defaults to UDP and the form's dropdown
  defaults to UDP. Save without touching it and the device listens for
  broadcasts while a TCP-only script waits for a caller. The old status line
  said "WAITING" for both this and a failed TCP connect, which is exactly the
  reported symptom.
- **Stale host.** A source address saved at the office is a different machine's
  address at home.

The status line now distinguishes them, the serial log states the mode
explicitly on startup, and the test server speaks both transports.

---

## Legibility rework

The 5x7 font at scale 1 has 1-pixel strokes — genuinely hard to read on a
792px panel, and the sentence checklist was the worst of it. Now:

- Checklist rows, status line, section headers and box labels are all scale 2.
- Checklist row pitch 24px, starting at y=72; counts clamp at 9999 and ages at
  `99+` so the worst case `GGA 9999 (99) 99+` is exactly the 17 characters that
  fit in the 208px column.
- `OTHER:` wraps across up to two lines of four IDs instead of running off into
  the value grid (it previously overflowed at 8 IDs).
- Grid row height is now derived from `canvas_.height()`, so the X3's extra
  48px becomes taller boxes rather than dead space (220px rows vs the X4's 196).
- Box labels shortened to fit at scale 2: `SEA TEMP`, `DEPTH`, `AIS TGTS`.
- Added `(` and `)` glyphs to the font. They were missing, which blanked out
  the `(n BAD)` checksum-failure counter — the single most important thing a
  verification tool has to show.

`python3 scripts/preview_font.py` renders the generated tables as terminal
ASCII art, so glyph problems are now findable without hardware.

---

## Battery indicator (added at the end of the session)

The status row now shows `BATT nn%` (or `CHRG nn%` while charging), read from
the X3's **BQ27220 fuel gauge over I2C** — a real state-of-charge value, not a
voltage estimate. Verified on hardware: `Battery: 100%  4333 mV`.

Nothing board-specific was needed. `FREEINK_BATTERY_I2C_GAUGE` is auto-defined
by `BoardConfig.h` from `FREEINK_DEVICE_X3`, `BatteryMonitor` initialises Wire
itself, and the same API silently falls back to the ADC divider on the X4. The
only change was adding `BatteryMonitor` to `lib_deps`.

`readStatus()` is used rather than `readPercentage()` because its per-field
`Known` flags distinguish a genuine 0% from a failed I2C read; the last good
value is kept so a transient failure doesn't blank the indicator.

This also needed a `%` glyph added to the font — it didn't have one. That's the
second time a missing glyph would have silently drawn nothing (parentheses were
the first). **Check `scripts/preview_font.py` before putting a new character on
screen.**

---

## Partition table: why it matches CrossInk's exactly

Found by testing the round trip, after the web installer shipped: flashing
CrossInk or CrossPoint back onto a device running eNMEA **failed**, with
`Partition table is missing an OTA app slot`.

Cause: eNMEA used the stock `huge_app.csv` - a 4 MB layout with a single app
partition. CrossInk's installer (Inky) installs by writing into the *inactive*
OTA slot, so a table with only `ota_0` gives it nowhere to write. eNMEA was
turning these devices into a one-way trip, and the installer page said the
opposite.

Fixed by adopting CrossInk's own `partitions.csv` verbatim: 16 MB, `app0` and
`app1` at 6.25 MB each. eNMEA has no OTA path and never writes `app1` - **the
second slot exists purely so the device can be flashed back.** Anyone tempted
to reclaim that space should read `partitions.csv`'s header first.

This also fixed a plain bug: `platformio.ini` declared
`board_upload.flash_size = 16MB` while `huge_app.csv` mapped only the first
4 MB, leaving three quarters of the chip unaddressable.

Recovery for a device already stuck on an old build: reinstall eNMEA from the
web installer (which rewrites the partition table), then use Inky.

**Verified end to end**: the full revert/reload cycle was run twice - eNMEA to
CrossInk through Inky, then CrossInk back to eNMEA through the web installer -
with no errors in either direction. That exercises the partition-table rewrite
both ways, the otadata handoff (CrossInk may boot from app1; our image writes
otadata pointing at app0), and the NVS wipe.

**Lesson worth keeping**: the installer page claimed "installing eNMEA is not a
one-way door" and pointed at Inky, on reasoning alone - nobody had run the round
trip. The reasoning was sound and the claim was false. Untested claims about
recovery paths are the ones that hurt most, because they are only discovered by
the person who needs them to be true.

---

## Source profiles - the bench workflow

Added after the firmware was working, once the actual use case became clear:
this is not a device that gets configured once and installed on a boat. It gets
pointed at a different piece of equipment every few minutes - a shelf of AIS
units, multiplexers and gateways, each with its own access point, IP and port.

The original single-configuration model cost a couple of minutes per unit:
join a different AP on a phone, retype four fields, wait out a 15-second
reboot. Now `AppSettings` stores 8 named profiles and the buttons switch
between them - UP/DOWN to browse, CONFIRM to apply, reconnecting in place.

Decisions worth not re-litigating:

- **Browsing applies nothing.** UP/DOWN only move a selection; CONFIRM commits.
  A stray press on a bench must never tear down a feed being watched.
- **Switching resets the counters.** Carrying the previous unit's sentence
  totals into the next one would make a silent device look alive - the exact
  false positive this tool exists to prevent.
- **BACK forgets one profile, not all of them.** It used to erase everything,
  which was fine when "everything" was one config and disastrous when it is
  eight. A full wipe lives on the settings page.
- **One versioned NVS blob, not ~48 keys.** A partial write cannot leave a
  half-configured profile, and an unrecognised layout comes up unconfigured
  rather than joining a network named from arbitrary bytes. The pre-profile
  NVS was rejected cleanly on first boot - visible in the log as
  `No usable profile (0 stored)`.
- **Editing an inactive profile does not reboot.** Only a change to the running
  configuration restarts anything, so the next unit can be prepped while still
  watching the current one.

---

## Getting it to end users

The firmware working is only half of it. A marine electronics user is not going
to install PlatformIO, so the project now ships a browser installer:
**<https://cffinch62.github.io/eNMEA/>** (ESP Web Tools, served by GitHub Pages
from `docs/`). Plug in the X3, open the page in Chrome or Edge, press Install.

Rebuild it after any firmware change with `scripts/build_web_installer.sh`,
then commit `docs/`. It merges bootloader + partition table + otadata + app into
one image flashable at offset 0, and regenerates the manifest.

Things that were not obvious and are worth not rediscovering:

- **The offsets are not guessable.** 0x0 for the bootloader comes from esptool's
  own `ESP32C3ROM.BOOTLOADER_FLASH_OFFSET`; it is 0x1000 on the classic ESP32,
  which is the usual way to get this wrong. otadata and app0 come from the
  build's own `partitions.bin`.
- **Installing always wipes settings.** The merged image spans the NVS
  partition and esptool erases before writing, so every install - including an
  update - comes up in `SETUP MODE`. Verified, not assumed.
- **`boot_app0.bin` is not padding.** It is a real otadata record selecting
  app0, which is what makes the device boot our app after CrossInk may have
  been running from app1.
- **Opening `docs/index.html` from disk cannot work.** A `file://` page has
  origin `null`, so the manifest fetch is blocked by CORS - and ESP Web Tools
  reports it as "Failed to download manifest", which points nowhere near the
  cause. Chrome treats `file://` as a secure context, so its HTTPS guard does
  not catch it first. The page now detects this and says so.
- **ESP Web Tools never checks `response.ok`** before `.json()`, so any
  non-JSON response surfaces under that same misleading message. The manifest
  URL is version-stamped to keep a stale cache out of it.
- **The dialog owns the serial port.** On success the page closes it via the
  dialog's own `_closeDialog()` and shows plain next steps. Never detach the
  element instead - closing is what releases the port, and a held port is the
  "device is busy" failure the page itself documents.

The page also carries a support FAQ, which exists because every problem hit
while building this is one an end user will hit: charge-only USB cables, a port
held by another tab, 2.4GHz-only Wi-Fi, a TCP host address that is only valid
on one network, and settings disappearing on every install.

---

## Environment notes (the things that cost time)

- **PlatformIO is not installed system-wide.** Chuck's Python is externally
  managed (PEP 668), so it lives in a venv: **`~/.venvs/pio/bin/pio`**.
  Deliberately outside the Dropbox tree — the project directory syncs, and the
  toolchain is gigabytes.
- **`.pio/build/` in this directory syncs via Dropbox**, so it can contain
  stale artifacts from another machine. It did: the tree here was from the
  office machine and referenced a `~/.platformio` that doesn't exist on this
  one. Don't trust it; check object timestamps if a build looks suspiciously
  fast.
- **Serial needed udev rules.** Chuck was not in `dialout`. Installing
  `99-platformio-udev.rules` fixed it (`/dev/ttyACM0` is now 0666).
- **`pio device monitor` fails in a non-interactive shell** — miniterm demands
  a TTY. Read the port with pyserial directly instead; toggle RTS to reset the
  C3 if you need to capture boot output.
- **Close the serial port before flashing.** An open reader makes esptool's
  final reset fail with "chip stopped responding" *after* the data has been
  written and verified.
- **`default_envs` is now `x3`.** There is no X4 in this project — see below.

---

## Verified on hardware tonight

The X3 (`7C:E8:B1:70:B7:40`) at 192.168.1.71:

- Boots as `xteink_x3, panel 792x528` — the X3 panel path and stride are right
- Joins Wi-Fi, RSSI ~-55 dBm, channel 6
- `TCP: attempt 1 -> 192.168.1.142:10110` → `TCP: connected.` first try
- Sustained data flow, dashboard values updating, 2-second redraw cadence
- Reconnects automatically after a reflash without reconfiguration
- Both `pio run -e x3` and `-e x4` compile clean, no warnings from project
  sources (x3: 34.8% flash, 12.7% RAM)

- **BACK held 3 s erases settings and reboots into setup mode** — confirmed.
- **POWER held 2 s shuts the device down** — confirmed. This is the whole
  deep-sleep sequence working: panel sleep, battery-latch release, rail
  power-down, and wake arming in the hand-rolled order described in bug 1.

Every code path written tonight has now been run on the device.

The one thing to keep half an eye on is *repeated* off/on cycling. GPIO holds
survive a reset, so a wrongly-set one accumulates rather than showing up on the
first try: the symptom would be a board that won't power back on, or that wakes
to a blank panel, after several cycles rather than the first. If that ever
appears, look at `holdPowerRails()` / `releaseSdRail()` in `setup()` and the
latch loop in `PowerControl.cpp`.

---

## Hardware reality check

**There is only an X3.** No X4 exists for this project. `env:x4` compiles but
has never run on hardware, and every X4-specific claim in the docs should be
treated as unverified. Flashing an x4 binary to an X3 is not harmless — the
panels differ in size, the framebuffer row stride comes out wrong (100 vs 99
bytes), and the screen fills with diagonal black smears. That already happened
once during early bring-up. `default_envs = x3` now guards against it.

---

## What's next

1. **`NmeaSource` and `src/ui/` have no automated tests.** The parser layer is
   covered now, so these are the soft spot. `NmeaSource` needs an interface seam
   to be testable at all - it depends on `WiFiClient`/`WiFiUDP` directly.
2. **Settle the office Wi-Fi question.** The device never joined there but works
   at home; almost certainly a 5GHz-only network, since the ESP32-C3 has no
   5GHz radio. The boot log lists every SSID it can actually see, so one trip
   with a serial console answers it.
3. **Consider Improv Wi-Fi over serial.** ESP Web Tools can provision Wi-Fi
   itself immediately after flashing, if the firmware speaks the Improv serial
   protocol. That would remove the phone-and-access-point step entirely: install
   and configure in one browser session. It is real firmware work, but it is the
   single biggest remaining reduction in "things a user can get wrong".
4. **Make the setup AP stoppable** if the ~20% TCP retransmission rate ever
   matters. The always-on SoftAP shares the radio with the station connection.
   Harmless at NMEA data rates; measured, not theoretical.
5. **The X4 build has never run on hardware.** It compiles. That is all that is
   known about it.

## Documents

- `README.md` — developer-facing: design decisions, bring-up checklist, build
- `USER_GUIDE.md` — user-facing: setup, dashboard reference, troubleshooting
- `IMPLEMENTATION_PLAN.md` — forward task list and the verified-facts ledger
- `handoff-home.md` — this file: what broke tonight and why
- `partitions.csv` — read its header before changing anything about the layout
- `docs/` — the browser installer (GitHub Pages source), plus the dashboard photo
- `test/run_tests.sh` — host-side parser tests, ~1 second, no hardware
- `scripts/` — `build_web_installer.sh`, `nmea_test_server.py` (TCP **and** UDP),
  `gen_font.py`, `preview_font.py`

Repo: <https://github.com/CFFinch62/eNMEA> (public, MIT). `git init` and the
GitHub push both happened tonight; `default_envs` is `x3`.
