# Session Handoff - 2026-09-01

Read this first if you're picking this project up cold. It's a snapshot of
one work session, not a permanent doc - `README.md` (user-facing) and
`IMPLEMENTATION_PLAN.md` (forward-looking tasks + verified-facts ledger) are
the durable references; this file just orients you on where things were
left and what to do next. Delete or ignore this file once the WiFi issue
below is resolved and the dashboard has been seen working on real hardware -
it's not meant to accumulate across sessions.

## What happened this session

1. **Feature work** (done, build-verified, NOT yet seen on a live dashboard):
   added AIS target counting (decodes MMSI out of `!AIVDM` payloads, tracks
   distinct targets with a 6-minute liveness window) and wind data (`MWV`/
   `MWD`) decoding, which the README already claimed but were never actually
   implemented. Dashboard grid grew from 3x2 (6 boxes) to 4x2 (8 boxes:
   +WIND, +AIS TARGETS); sentence checklist grew from 8 to 12 rows. Full
   detail and design rationale is in `README.md`'s "Design decisions" and
   `IMPLEMENTATION_PLAN.md`'s ledger - not repeated here.
2. **Two real bugs found and fixed** while verifying the above actually
   builds/works:
   - `platformio.ini` was missing a now-required `-DFREEINK_DEVICE_X4`/`_X3`
     build flag (upstream `freeink-sdk` added this requirement after this
     project was last verified against it). Fixed - both `env:x4` and
     `env:x3` build clean as of this session.
   - `EinkCanvas::width()`/`height()` used the *static* `EInkDisplay::
     DISPLAY_WIDTH`/`DISPLAY_HEIGHT` constants (always the X4's 800x480)
     instead of the SDK's runtime `getDisplayWidth()`/`getDisplayHeight()`
     accessors, despite `IMPLEMENTATION_PLAN.md`'s ledger claiming this was
     already handled correctly - it wasn't. On an X3 build this corrupted
     the framebuffer row stride (100 bytes assumed vs. the SDK's real
     99-byte X3 rows), which is almost certainly what caused the garbled
     diagonal black smears seen on first flash. Fixed; see
     `IMPLEMENTATION_PLAN.md`'s "2026-09-01 correction" note for detail.
3. **Hardware confirmed**: the physical unit is an **X3** panel (792x528).
   Build/flash with `env:x3`, not the default `env:x4`.
4. **WiFi connection to the real network is currently unresolved** - see
   below, this is the actual next thing to chase.

## The open problem: WiFi won't connect

Symptom: device shows `SETUP MODE`, user fills in the web form with the
real Wi-Fi SSID (`SITEX`, a work network) and password, submits, device
reboots, tries for 15s, fails, falls back to `SETUP MODE` again.

Diagnosed via two rounds of added serial logging (still in
`src/main.cpp::connectToWifi()`):
- `WiFi.status()` alone read `6` (`WL_DISCONNECTED`) - too generic to act on.
- Hooking `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` for the real ESP-IDF reason
  code showed **`reason=201` (`WIFI_REASON_NO_AP_FOUND`)**, repeating on
  every internal retry within the 15s window.
- Ruled out: not a range/power issue - user was physically in the building,
  in range, phone connects to `SITEX` with no trouble, and **the same
  physical board connected to this exact network fine when it was still
  running CrossInk** (the e-reader firmware this hardware shipped with).
- That last point is the important clue: since the radio/antenna and the
  network are both known-good, something about *this firmware's* connect
  sequence specifically is the difference. Compared against CrossInk's own
  connect code (`/home/chuck/CrossInk/src/activities/network/
  WifiSelectionActivity.cpp`, kept as read-only reference material per
  `IMPLEMENTATION_PLAN.md`) and found it does more than eNMEA's original
  bare `WiFi.begin()`:
  - `WiFi.persistent(false)` - CrossInk's own comment says this exists to
    "suppress SDK NVS auto-connect", i.e. it deliberately avoids the
    ESP-IDF WiFi driver's own internal NVS-cached credentials/config (a
    different NVS partition than this project's own `AppSettings`/
    `Preferences` storage under namespace `"enmea"`). Since this exact board
    previously ran CrossInk, it's plausible something is left over in that
    blob.
  - `WiFi.disconnect(false, false, 1000)` before reconnecting.
  - `WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN)` (esp32-arduino default is
    `WIFI_FAST_SCAN`) and `WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL)`.

**Action taken**: `connectToWifi()` in `src/main.cpp` now mirrors that same
sequence, and additionally does a full `WiFi.scanNetworks()` dump right
before connecting - every SSID it sees gets logged with its exact length
(catches invisible trailing-space-type mismatches), RSSI, channel, and
encryption type, with an explicit flag on whichever entry (if any) matches
the configured SSID exactly.

**This was built and flashed to the device before the session ended, but
NOT yet tested** - ran out of time before a fresh setup-form submission
could be tried against it. **This is the very next thing to do.**

### Next steps, in order

1. Get a serial monitor open (see "Toolchain" below for how, since the
   previous session's PlatformIO install won't exist on a fresh machine)
   and redo the setup-portal flow (connect to `eNMEA-Setup`, browse to
   `192.168.4.1`, re-enter SSID/password/protocol/host/port, submit).
2. Read the log. Cases:
   - **It connects.** Great - the CrossInk-matching fix was it. Move on to
     actually eyeballing the dashboard (see "What's never been visually
     verified" below) and, once confirmed, consider whether the diagnostic
     logging added this session (the disconnect-reason hook and the scan
     dump) should be trimmed down or left in - it's harmless either way but
     is fairly verbose for normal operation.
   - **Still fails, and the scan dump does NOT list the target SSID at
     all.** That's a real environmental/RF question at that point (channel,
     hidden SSID, regulatory-domain channel restriction, etc.) - the code
     is doing everything it can at that point; next step is comparing
     against what a phone or laptop's own WiFi scan sees on the same
     channel list, not more firmware changes.
   - **Still fails, but the scan dump DOES list the target SSID (exact
     match flagged).** Then the scan/discovery step works and the actual
     4-way-handshake/auth step is what's failing - re-check the
     `WiFi disconnect reason=` value from that attempt (should no longer be
     201 in this case) and look up the new number against
     `esp_wifi_types_generic.h`'s `wifi_err_reason_t` enum (grep the
     installed framework packages, see the "Toolchain" section for path
     pattern) - e.g. `202`/`15` would point at a wrong password.

## What's never been visually verified

The dashboard (4x2 box grid + 12-row sentence checklist) has not been seen
rendering on the actual e-ink panel with live data - the WiFi issue above
has blocked getting that far. Once WiFi connects, point it at the test
server (see below) and actually look at the screen: does the 4-column grid
fit without clipping on the X3's 792px width, do WIND and AIS TARGETS
render correctly, does the sentence checklist's 12 rows fit and stay
legible. All of that layout math was verified on paper/by build success
only, per the caveats already in this session's chat history.

## Toolchain - none of this persists across machines/sessions

The previous session's PlatformIO install lived in a `/tmp/claude-*`
scratchpad path tied to that session - it will not exist here. Bootstrap
fresh:

```sh
cd "path/to/eNMEA"   # this project directory
python3 -m venv .venv-pio
.venv-pio/bin/pip install --upgrade pip
.venv-pio/bin/pip install platformio
git clone https://github.com/Free-Ink/freeink-sdk.git   # not vendored, gitignored
.venv-pio/bin/pio run -e x3 -t upload
.venv-pio/bin/pio device monitor -e x3
```

(`.venv-pio/` and `freeink-sdk/` are both already covered by `.gitignore`.)
Last session verified buildable against `freeink-sdk` commit
`fad70f28a982c978737410e535a4f7276ce28c19` (2026-09-01) - if a fresh clone
fails with a `BoardConfig.h` `#error` about no `FREEINK_DEVICE_<NAME>`
selected, that's the same upstream-drift risk recurring again; see
`platformio.ini`'s comments and `IMPLEMENTATION_PLAN.md`'s ledger.

Find the serial port with `.venv-pio/bin/pio device list` - was
`/dev/ttyACM0` last session (native USB-CDC, not a USB-serial chip), but
confirm on this machine.

## Test NMEA/AIS data source

`scripts/nmea_test_server.py` - stdlib-only Python, no install needed.
Streams a realistic mix of GGA/RMC/VTG/HDT/MTW/DBT/MWV/MWD (slowly-drifting
values so nothing goes STALE), a few cycling AIVDM sentences from fake
MMSIs (366053209, 366999712, 244660564, 235009802, 367123450) so AIS
TARGETS shows a nonzero count, some GSA/GLL traffic to exercise the
`OTHER:` checklist line, and a deliberately-corrupted-checksum GGA every
~20s to confirm the checksum-failure counter actually works. All real
checksums independently verified against the parser's own logic before
being trusted - see this session's chat log if you want the verification
method.

```sh
python3 scripts/nmea_test_server.py --port 10110
```

TCP mode only (matches the device's "dials out to host:port" behavior).
Point the device's setup form at this machine's LAN IP (`hostname -I` /
`ip addr` / `ifconfig`, whichever applies) on port `10110`, protocol TCP.

## Repo state

Still not a git repository (`IMPLEMENTATION_PLAN.md` Task 5, "repo
hygiene", is still open) - confirm with the project owner before deciding
whether/when to `git init`, per that task's own note not to assume.
