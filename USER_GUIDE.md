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

**3. Browse to `http://192.168.7.1/`**

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

![The eNMEA dashboard on an Xteink X3. Down the left, a checklist of sentence
types with counts and ages. Across the right, eight boxes: position, speed,
course, heading, sea temperature, depth, wind and AIS target count. The top line
reads SOURCE: CONNECTED alongside the device's address and battery
charge.](docs/images/dashboard.jpg)

*A live feed. Reading it: `GGA 377 (19) 0S` is 377 good GGA sentences against 19
that failed their checksum, the last one under a second ago. `HDG --` and
`DPT --` are types this feed never sent.*


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

## Profiles — for benches and shelves of equipment

eNMEA stores **up to eight complete configurations** — name, Wi-Fi network and
password, protocol, host and port — and switches between them from the device's
own buttons. This is what makes it practical for checking a shelf of AIS units,
multiplexers and gateways, where every product ships with a different access
point, address and port.

Set them up once from the settings page, then:

| Button | Does |
| --- | --- |
| **UP** / **DOWN** | Step through saved profiles. The name appears on screen; nothing changes yet. |
| **CONFIRM** | Apply the one shown — the device reconnects in a few seconds. No phone, no reboot. |
| **BACK** (hold 3 s) | Forget the profile in use and fall back to the next one stored. |

Browsing with UP/DOWN is free: nothing is applied until you press CONFIRM, so a
stray press never interrupts a feed you're watching.

The active profile's name is shown in the title line, so there is never any
doubt which one is running. Switching resets the sentence counters — carrying
the last unit's totals over would make a silent device look alive.

**Erasing everything** is deliberately not on a button. Hold BACK and you lose
one profile; to clear all eight there's a separate button at the bottom of the
settings page.

## Changing settings

Three ways, in order of how little has to be working:

**1. The setup AP — always available.** `eNMEA-Setup` stays up the whole time
the device is on, even while it's connected to your Wi-Fi. Join it, browse to
`http://192.168.7.1/`, change anything, save. This works even when the saved
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

## Using it with a NMEA 2000 gateway

A converter such as the ONWA KC-2W bridges both a NMEA 2000 backbone and wired
NMEA 0183 instruments onto Wi-Fi, and serves them on **two different ports**:

| Port | Carries |
| --- | --- |
| `10110` | NMEA 0183 arriving at the gateway from wired instruments |
| `10111` | 0183 the gateway converted **out of** NMEA 2000 |

Set eNMEA to **TCP**, the gateway's address, and whichever port you want to
examine. Checking both localises a fault in about a minute, without touching a
wire or opening a laptop:

| 10110 | 10111 | What it means |
| --- | --- | --- |
| data | data | Both sides are alive. If a display still shows nothing, the problem is that display's own setup — its baud rate, or which sentences it has been told to accept. |
| data | nothing | Wired 0183 is arriving. The N2K side is the problem: backbone wiring, termination, power, or PGN selection at the gateway. |
| nothing | data | The backbone is healthy. Wired 0183 is the problem: wiring, polarity, or a baud-rate mismatch at the instrument. |
| nothing | nothing | Neither side is delivering. Suspect the gateway itself, its power, or the Wi-Fi link — check `SOURCE:` first. |

This is what eNMEA is for. It answers "is the data actually there?" separately
from "does my display show it?", which are the two questions that otherwise get
tangled together. **A sentence appearing at all — even one eNMEA doesn't decode
into a box — proves the wiring, the backbone and the gateway are working.** That
is why the `OTHER:` list matters as much as the value boxes: engine, tank and
similar data arrive as sentence types this tool deliberately doesn't interpret,
and seeing them listed with a valid checksum is the confirmation you need.

> **Note:** the gateway's own access point is at 192.168.4.1, and eNMEA's setup
> AP deliberately sits on 192.168.7.1 so the two never collide.

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
| It joined Wi-Fi at home but not elsewhere | If you use TCP, the Host address belongs to one particular network. Moving the device makes it wrong — re-enter it from the setup page. |
| Won't join a network it should | The ESP32-C3 is **2.4 GHz only**. A 5 GHz-only network, or a router that steers devices onto 5 GHz under a shared name, cannot work. The serial log lists every network the device can actually see. |
| Settings vanished after a firmware update | Expected. Installing always clears saved settings — the flash image covers the storage area they live in. |
| Battery drops faster than the e-reader did | Expected. Constant Wi-Fi, a hosted access point and a 2-second screen refresh cost far more than page-turning. |

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
- **The setup page is at 192.168.7.1, not the usual 192.168.4.1.** Most ESP32
  devices — including marine Wi-Fi gateways — use 192.168.4.1 for their own
  access point. If eNMEA used it too, a device joined to such a gateway would
  have two interfaces on one subnet and would try to reach the gateway at its
  own address. Moving ours avoids that entirely.
- **Up to 48 sentence types are tracked**, and whatever doesn't fit on screen is
  shown as `+N MORE` rather than hidden. The list never silently under-reports.
- **Battery percentage comes from the X3's BQ27220 fuel gauge** over I2C, so
  it's a real state-of-charge reading rather than a voltage guess. If a read
  fails transiently the last good value stays on screen.
