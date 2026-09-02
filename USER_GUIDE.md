# eNMEA User Guide

**Xteink X3 · NMEA 0183 over Wi-Fi verification tool**

eNMEA turns an Xteink X3 e-reader board into a bench instrument that answers
one question: *is this NMEA feed actually alive and correct?* It joins your
Wi-Fi, listens to a NMEA 0183 source over the network, checks every sentence's
checksum, and shows you what arrived on an e-ink dashboard.

It is a **verification tool**, not a chartplotter. It shows you what the feed
says, unconverted and unsmoothed, so you can tell whether an instrument is
sending what you think it's sending.

---

## What you need

- The Xteink X3, charged (or on USB)
- A 2.4 GHz Wi-Fi network — the ESP32-C3 has **no 5 GHz radio**
- A NMEA 0183 source reachable over that network: a multiplexer, OpenCPN,
  a phone nav app with NMEA output, `socat`, or the bundled test server
- A phone or laptop, for the one-time setup form

---

## First-time setup

The device has no on-screen keyboard. You configure it from a phone through a
Wi-Fi access point it hosts itself.

**1. Power on.** The screen shows `SETUP MODE` and the device starts an open
Wi-Fi network called **`eNMEA-Setup`**.

**2. Join `eNMEA-Setup`** from your phone. Ignore the "no internet" warning —
this network only reaches the device.

**3. Browse to `http://192.168.4.1/`**

**4. Fill in the form:**

| Field | What to enter |
| --- | --- |
| **Wi-Fi SSID** | Your 2.4 GHz network name |
| **Wi-Fi Password** | Your network password |
| **Protocol** | `UDP` or `TCP` — see below, this one matters |
| **NMEA Source Host** | TCP only: the IP of the machine running the source |
| **Port** | Usually `10110`, the common NMEA-over-IP port |

**5. Press "Save & Reboot".** The device stores the settings and restarts. It
joins your Wi-Fi and the dashboard appears within about 15 seconds.

The setup page stays reachable afterwards — you never have to factory-reset
just to change the port. See **Changing settings** below.

---

## UDP or TCP — pick the right one

This is the single most common reason a working device shows no data. They are
not interchangeable, and choosing wrong produces **no error message**.

**UDP — the device listens.** It opens the port and waits for broadcast
traffic to arrive. It never dials out. The Host field is ignored entirely.
Use this when your source *broadcasts* NMEA onto the network.

**TCP — the device dials out.** It connects to `Host:Port` as a client, the way
you'd point it at a multiplexer's TCP server mode. Use this when your source
*listens* for connections.

If you're unsure, look at the source: if it says "listening on port N" or
"server", you want **TCP**. If it says "broadcasting", you want **UDP**.

> **The Host address changes between networks.** A laptop that was
> `192.168.1.50` at the office is a different address at home. A saved host
> from another site will never answer — the status line will read `FAILED`.

---

## Reading the dashboard

```
 eNMEA - NMEA 0183 WI-FI VERIFIER                        TCP 192.168.1.142:10110
 SOURCE: CONNECTED                   SETUP: 192.168.1.71 OR AP      BATT 87%
 ─────────────────────────────────────────────────────────────────────────────
 SENTENCES SEEN        ┌──────────┐┌──────────┐┌──────────┐┌──────────┐
 GGA 128 1S            │ POSITION ││  SPEED   ││  COURSE  ││ HEADING  │
 RMC 128 1S            │ 47.6062  ││ 6.4 KT   ││ 118 T    ││ 47 M     │
 VTG 128 1S            │-122.3321 ││          ││          ││          │
 HDT 64 2S             └──────────┘└──────────┘└──────────┘└──────────┘
 HDG --                ┌──────────┐┌──────────┐┌──────────┐┌──────────┐
 MTW 42 3S             │ SEA TEMP ││  DEPTH   ││   WIND   ││ AIS TGTS │
 ...                   │ 16.8 C   ││ 24.3 M   ││ 12.4 KT  ││    5     │
 OTHER:                │          ││          ││ 212 T    ││          │
 GSA GLL               └──────────┘└──────────┘└──────────┘└──────────┘
 HOLD POWER 2S: SHUT DOWN   HOLD BACK 3S: ERASE SETTINGS   SETUP AP: ENMEA-SETUP
```

### The status line

| Shows | Means |
| --- | --- |
| `LISTENING` | UDP socket open — nothing has ever arrived |
| `CONNECTING` | TCP dial-out in progress, or waiting out the 3-second retry |
| `CONNECTED` | Socket up and data arriving |
| `NO DATA` | Socket still up, but nothing for 10 seconds — the source went quiet |
| `FAILED` | TCP connection refused/unreachable, or the UDP port couldn't be opened |

On the right, `SETUP: <ip> OR AP` tells you where to reach the settings page,
and **`BATT 87%`** is the battery charge — read from the X3's fuel gauge, not
estimated from voltage. It reads `CHRG` instead of `BATT` while charging.

Keep an eye on it. eNMEA holds a Wi-Fi connection, runs its own access point,
and refreshes the screen every two seconds — a much heavier duty cycle than the
e-reader firmware this board shipped with, so it will flatten a charge
noticeably faster.

### The sentence checklist (left column)

One row per sentence type eNMEA understands, whether or not it has arrived:

- **`GGA 128 1S`** — 128 valid GGA sentences, the most recent 1 second ago
- **`GGA 128 (2) 1S`** — same, but 2 sentences **failed their checksum**. Any
  number in parentheses means corrupted data is reaching the device.
- **`HDG --`** — this sentence type has never arrived
- Counts cap at `9999`, ages at `99+` seconds

**`OTHER:`** lists sentence types that arrived and passed checksum but that
eNMEA doesn't decode into a box — GSA, GLL, ZDA and so on. Their presence
confirms the feed is broader than the dashboard shows.

### The value boxes

| Box | Source sentences | Notes |
| --- | --- | --- |
| **POSITION** | GGA, RMC | Decimal degrees. Latitude on top, longitude below. Negative = South/West |
| **SPEED** | RMC, VTG | Knots |
| **COURSE** | RMC, VTG | Degrees true |
| **HEADING** | HDT, HDG | `T` = true, `M` = magnetic (uncorrected) |
| **SEA TEMP** | MTW | Celsius |
| **DEPTH** | DBT, DPT | Metres below the transducer |
| **WIND** | MWV, MWD | Speed in knots; direction `T` = true, `R` = relative to bow |
| **AIS TGTS** | VDM | Distinct vessels heard in the last 6 minutes |

`--` means that value has never been received. **`STALE`** inside a box means
the value arrived once but hasn't updated in 10 seconds — the number shown is
the last one received, not a current reading.

Where two sentences feed one box, **the most recent one wins.** There is no
cross-checking between them; that's deliberate, so you see what the feed
actually sent.

The screen redraws every 2 seconds, with a deeper repaint about once a minute
to clear e-ink ghosting. A brief flash during that repaint is normal.

---

## Powering off

**Hold the power button for about 2 seconds.** A `KEEP HOLDING TO SHUT DOWN`
message appears after half a second so you know it registered. The screen then
shows `POWERED OFF` and the device sleeps.

**Press the power button again to turn it back on.**

Two things that look like faults but aren't:

- **The screen still shows "POWERED OFF" while it's off.** E-ink holds its last
  image with no power at all. That's the display working correctly.
- **On USB power, "off" means deep sleep.** The battery is disconnected but the
  USB rail still feeds the chip. Unplug it for a genuinely unpowered board.

---

## Changing settings

Three ways, in order of how little has to be working:

**1. The setup AP — always available.** `eNMEA-Setup` stays up the whole time
the device is on, even while it's connected to your Wi-Fi. Join it, browse to
`http://192.168.4.1/`, change anything, save. This works even when the saved
settings are completely wrong for where you are.

**2. From your network.** The settings page is also at `http://<device-ip>/` —
the address shown on the dashboard status line.

**3. Start over.** Either press **"Forget settings & reboot to setup"** at the
bottom of the settings page, or **hold the BACK button for 3 seconds** on the
device. Both erase the saved Wi-Fi and source settings and return to
`SETUP MODE`.

---

## Testing without a real NMEA source

The project includes a test server that generates a realistic feed — position,
speed, course, heading, sea temperature, depth, wind, AIS targets, plus a
deliberately corrupted sentence every 20 seconds so you can watch the `(n)`
checksum-failure counter work.

```sh
# TCP — set the device to TCP, Host = the IP this prints, Port 10110
python3 scripts/nmea_test_server.py

# UDP — set the device to UDP, Port 10110 (Host is ignored)
python3 scripts/nmea_test_server.py --proto udp
```

It prints your machine's IP address at startup — that's exactly what goes in
the device's Host field.

---

## Troubleshooting

| Symptom | Cause and fix |
| --- | --- |
| `SOURCE: LISTENING` forever | The device is in **UDP** mode but your source is a TCP server. Switch protocol at the setup page. |
| `SOURCE: FAILED` | TCP couldn't connect. Check the source is running, the Host is right *for this network*, and both are on the same subnet. |
| `SOURCE: CONNECTED` then `NO DATA` | Link is fine, the source stopped sending. Check the source, not the device. |
| Screen says `WIFI FAILED` | Wrong password, or the network is 5 GHz-only. The device falls back to the setup AP automatically — rejoin `eNMEA-Setup` and correct it. |
| Sentences arrive but a box stays `--` | That box's sentence type isn't in the feed. Check the checklist: if the row shows `--`, the source never sent it. |
| A row shows a number in parentheses | Genuine checksum failures — corrupted data on the wire. Suspect the physical link feeding your multiplexer. |
| Values shown but marked `STALE` | That instrument stopped reporting while others kept going. |
| Nothing on screen at all | Hold power 2 s to shut down, press again to restart. If still blank, reflash. |

For anything deeper, connect USB and watch the serial log at 115200 baud — the
device logs its Wi-Fi scan (with every visible SSID), the connection result,
and every TCP attempt with the reason it failed.

---

## Limits worth knowing

- **Units are fixed:** knots, Celsius, metres. No Fahrenheit or feet option.
- **Heading from HDG is magnetic**, shown as `M`, with no deviation/variation
  correction applied. HDT is already true and shows `T`.
- **Depth ignores the DPT offset field.** The value is depth below the
  transducer, not below the waterline or keel — that offset's sign convention
  is instrument-specific and guessing it would defeat the purpose.
- **AIS counts distinct MMSIs heard in the last 6 minutes**, not a running
  total. A vessel that goes quiet for longer drops out. Only VDM (other
  vessels) counts; VDO (your own transponder) does not, though both appear in
  the checklist.
- **Up to 20 distinct sentence types are tracked** — the 12 with dedicated rows
  plus 8 on the `OTHER:` line. A feed with more types stops gaining new rows
  once full; the ones already tracked keep updating.
- **The setup AP is always on**, which shares the radio with the Wi-Fi
  connection. Harmless at NMEA data rates, but it does cost battery.
- **Battery percentage comes from the X3's BQ27220 fuel gauge** over I2C, so
  it's a real state-of-charge reading rather than a voltage guess. If a read
  fails transiently the last good value stays on screen.
