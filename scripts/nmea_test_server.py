#!/usr/bin/env python3
"""Standalone NMEA 0183 + AIS test server for exercising the eNMEA dashboard.

Speaks both transports the device supports, because picking the wrong one is
the most common reason the dashboard shows nothing:

    python3 scripts/nmea_test_server.py                 # TCP server (device dials in)
    python3 scripts/nmea_test_server.py --proto udp     # UDP broadcast (device listens)

TCP mode listens on --port and accepts one client at a time. UDP mode
broadcasts to the subnet, which is what the device's UDP mode expects - it
listens on the port and never dials out, so nothing needs to know its address.

Prints this machine's addresses at startup: in TCP mode that is exactly what
goes in the device's Host field, and it changes whenever you move between
networks.

No third-party dependencies - stdlib only.
"""
import argparse
import math
import socket
import time


def checksum(body: str) -> str:
    cs = 0
    for ch in body:
        cs ^= ord(ch)
    return f"{cs:02X}"


def sentence(body: str) -> str:
    prefix = body[0]
    inner = body[1:]
    return f"{prefix}{inner}*{checksum(inner)}\r\n"


def nmea_lat(deg: float) -> tuple[str, str]:
    hemi = "N" if deg >= 0 else "S"
    deg = abs(deg)
    d = int(deg)
    m = (deg - d) * 60
    return f"{d:02d}{m:07.4f}", hemi


def nmea_lon(deg: float) -> tuple[str, str]:
    hemi = "E" if deg >= 0 else "W"
    deg = abs(deg)
    d = int(deg)
    m = (deg - d) * 60
    return f"{d:03d}{m:07.4f}", hemi


def sixbit_encode(value: int) -> str:
    # Inverse of NmeaParser.cpp's decodeAisMmsi sixBitValue() - verified
    # against a known test vector (MMSI 366053209) when eNMEA's decoder was
    # written, so this is provably the correct inverse mapping.
    return chr(value + 48) if value < 40 else chr(value + 56)


def encode_ais_payload(msg_type: int, mmsi: int, min_chars: int = 14) -> str:
    bits = f"{msg_type:06b}" + "00" + f"{mmsi:030b}"  # type + repeat(0) + mmsi
    while len(bits) < min_chars * 6:
        bits += "0"  # pad - only the header (first 38 bits) needs to be meaningful
    if len(bits) % 6 != 0:
        bits += "0" * (6 - len(bits) % 6)
    return "".join(sixbit_encode(int(bits[i : i + 6], 2)) for i in range(0, len(bits), 6))


def aivdm(mmsi: int, channel: str = "A") -> str:
    payload = encode_ais_payload(1, mmsi)
    return sentence(f"!AIVDM,1,1,,{channel},{payload},0")


AIS_TARGETS = [366053209, 366999712, 244660564, 235009802, 367123450]


def local_addresses():
    """(ip, broadcast) for each non-loopback IPv4 the OS will actually route from."""
    found = []
    try:
        import fcntl
        import struct
    except ImportError:  # non-Linux fallback: whatever the default route uses
        fcntl = None
    # The connect-to-a-remote-address trick returns the IP the kernel would use
    # as a source for outbound traffic - no packet is sent (UDP connect is local).
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("8.8.8.8", 53))
        ip = probe.getsockname()[0]
        found.append((ip, ip.rsplit(".", 1)[0] + ".255"))
    except OSError:
        pass
    finally:
        probe.close()
    return found


def sentence_batch(tick, t0, state):
    """One tick's worth of sentences, as a list of complete NMEA lines."""
    now = time.time()
    elapsed = now - t0
    hhmmss = time.strftime("%H%M%S", time.gmtime(now))

    # Slowly drift position/course/speed/heading so STALE never fires and the
    # dashboard visibly updates each redraw.
    lat_now = state["lat"] + 0.0006 * math.sin(elapsed / 40.0)
    lon_now = state["lon"] + 0.0006 * math.cos(elapsed / 40.0)
    course_now = (state["course"] + elapsed * 0.5) % 360
    speed_now = state["speed"] + 0.8 * math.sin(elapsed / 15.0)
    heading_now = (state["heading"] + elapsed * 0.5) % 360
    wind_dir_now = (state["wind_dir"] + elapsed * 1.5) % 360
    water_temp = state["water_temp"]
    depth = state["depth"]
    wind_speed = state["wind_speed"]

    lat_s, lat_h = nmea_lat(lat_now)
    lon_s, lon_h = nmea_lon(lon_now)

    lines = []
    lines.append(sentence(
        f"$GPGGA,{hhmmss}.00,{lat_s},{lat_h},{lon_s},{lon_h},1,08,0.9,3.2,M,-19.6,M,,"))
    lines.append(sentence(
        f"$GPRMC,{hhmmss}.00,A,{lat_s},{lat_h},{lon_s},{lon_h},"
        f"{speed_now:.1f},{course_now:.1f},{time.strftime('%d%m%y', time.gmtime(now))},,"))
    lines.append(sentence(
        f"$GPVTG,{course_now:.1f},T,,M,{speed_now:.1f},N,{speed_now * 1.852:.1f},K"))

    if tick % 2 == 0:
        lines.append(sentence(f"$HEHDT,{heading_now:.1f},T"))
    if tick % 3 == 0:
        lines.append(sentence(f"$YXMTW,{water_temp:.1f},C"))
        lines.append(sentence(f"$SDDBT,{depth * 3.281:.1f},f,{depth:.1f},M,{depth * 0.5468:.1f},F"))
    if tick % 2 == 1:
        lines.append(sentence(f"$WIMWV,{wind_dir_now:.1f},T,{wind_speed:.1f},N,A"))
    if tick % 5 == 0:
        lines.append(sentence(
            f"$WIMWD,{wind_dir_now:.1f},T,{wind_dir_now:.1f},M,{wind_speed:.1f},N,{wind_speed * 0.5144:.1f},M"))
    # Extra "other" sentence types so the OTHER: line has something to show.
    if tick % 4 == 0:
        lines.append(sentence("$GPGSA,A,3,04,05,09,12,,,,,,,,,1.8,0.9,1.6"))
    if tick % 6 == 0:
        lines.append(sentence("$GPGLL,{},{},{},{},{},A,A".format(lat_s, lat_h, lon_s, lon_h, hhmmss)))

    # Cycle a handful of AIS targets through so the count settles above zero
    # without spamming every tick.
    if tick % 2 == 0:
        mmsi = AIS_TARGETS[(tick // 2) % len(AIS_TARGETS)]
        lines.append(aivdm(mmsi))
    if tick % 7 == 0:
        lines.append(aivdm(AIS_TARGETS[0], channel="A"))

    if state["bad_every"] and now - state["last_bad"] >= state["bad_every"]:
        state["last_bad"] = now
        good = sentence(f"$GPGGA,{hhmmss}.00,{lat_s},{lat_h},{lon_s},{lon_h},1,08,0.9,3.2,M,-19.6,M,,")
        lines.append(good[:-4] + "FF\r\n")  # corrupt the checksum on purpose

    return lines


def serve_tcp(args, state, t0):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.bind, args.port))
    srv.listen(1)
    print(f"[nmea-test-server] TCP listening on {args.bind}:{args.port}")
    for ip, _ in local_addresses():
        print(f"[nmea-test-server]   -> set the device's Host to {ip}, Port {args.port}, Protocol TCP")
    print("[nmea-test-server]   the device dials in; nothing happens here until it does")

    while True:
        print("[nmea-test-server] waiting for a connection...")
        conn, addr = srv.accept()
        print(f"[nmea-test-server] client connected: {addr}")
        try:
            tick = 0
            while True:
                conn.sendall("".join(sentence_batch(tick, t0, state)).encode("ascii"))
                tick += 1
                time.sleep(1.0)
        except (BrokenPipeError, ConnectionResetError):
            print("[nmea-test-server] client disconnected")
        finally:
            conn.close()


def serve_udp(args, state, t0):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    targets = [args.udp_target] if args.udp_target else [bcast for _, bcast in local_addresses()]
    if not targets:
        targets = ["255.255.255.255"]
    print(f"[nmea-test-server] UDP broadcasting to {', '.join(targets)} port {args.port}")
    print(f"[nmea-test-server]   -> set the device to Protocol UDP, Port {args.port}; Host is ignored")

    tick = 0
    while True:
        payload = "".join(sentence_batch(tick, t0, state)).encode("ascii")
        # One datagram per sentence: NMEA-over-UDP sources send whole sentences,
        # and a single oversized datagram risks fragmentation on the way in.
        for line in payload.splitlines(keepends=True):
            for target in targets:
                sock.sendto(line, (target, args.port))
        if tick % 10 == 0:
            print(f"[nmea-test-server] tick {tick}: sent {len(payload)} bytes")
        tick += 1
        time.sleep(1.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--proto", choices=("tcp", "udp"), default="tcp",
                    help="tcp: run a server the device dials into. udp: broadcast to the device.")
    ap.add_argument("--port", type=int, default=10110)
    ap.add_argument("--bind", default="0.0.0.0", help="TCP bind address")
    ap.add_argument("--udp-target", default=None,
                    help="UDP destination (default: this machine's subnet broadcast address)")
    ap.add_argument("--bad-checksum-every", type=int, default=20,
                    help="seconds between deliberately-corrupted GGA sentences (0 to disable)")
    args = ap.parse_args()

    t0 = time.time()
    state = {
        "lat": 47.6062, "lon": -122.3321,  # Elliott Bay, Seattle - arbitrary start point
        "course": 45.0, "speed": 6.2, "heading": 47.0,
        "water_temp": 16.8, "depth": 24.3,
        "wind_dir": 210.0, "wind_speed": 12.4,
        "bad_every": args.bad_checksum_every, "last_bad": t0,
    }

    if args.proto == "tcp":
        serve_tcp(args, state, t0)
    else:
        serve_udp(args, state, t0)


if __name__ == "__main__":
    main()
