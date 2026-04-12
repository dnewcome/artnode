#!/usr/bin/env python3
"""
Art-Net test pattern sender for artnode-rpi.

Targets a 64x64 HUB75 panel by default (4096 pixels, 24 universes).
Pure stdlib — no pip installs required.

Usage:
    python3 send_test_pattern.py [options]

Examples:
    python3 send_test_pattern.py --ip 192.168.68.66
    python3 send_test_pattern.py --ip 192.168.68.66 --pattern plasma
    python3 send_test_pattern.py --ip 192.168.68.66 --pattern bars --seconds 5
    python3 send_test_pattern.py --list
"""

import argparse
import math
import random
import socket
import struct
import sys
import time

# ---------------------------------------------------------------------------
# Defaults — adjust if your setup differs
# ---------------------------------------------------------------------------
DEFAULT_IP   = "192.168.68.66"
ARTNET_PORT  = 6454
PANEL_W      = 64
PANEL_H      = 64

# Derived
TOTAL_PIXELS = PANEL_W * PANEL_H                        # 4096
RAW_BYTES    = TOTAL_PIXELS * 3                         # 12288
UNIVERSES    = (RAW_BYTES + 511) // 512                 # 24

# ---------------------------------------------------------------------------
# Art-Net helpers
# ---------------------------------------------------------------------------

def artdmx_packet(universe: int, data: bytes) -> bytes:
    """Build an ArtDMX packet for the given universe."""
    # Length must be even and <= 512; pad short chunks
    if len(data) < 512:
        data = data + bytes(512 - len(data))
    return (
        b'Art-Net\x00'
        + struct.pack('<H', 0x5000)     # OpCode ArtDMX (LE)
        + struct.pack('>H', 14)         # ProtVer (BE)
        + b'\x00'                       # Sequence (disabled)
        + b'\x00'                       # Physical
        + struct.pack('<H', universe)   # Universe (LE)
        + struct.pack('>H', 512)        # Length (BE)
        + data
    )

def send_frame(sock: socket.socket, ip: str, pixels: list[tuple[int,int,int]]) -> None:
    """
    Transmit one full frame as UNIVERSES consecutive ArtDMX packets.
    pixels: row-major list of (r, g, b) tuples, length == TOTAL_PIXELS.
    """
    raw = bytearray(RAW_BYTES)
    for i, (r, g, b) in enumerate(pixels):
        raw[i*3]   = r & 0xFF
        raw[i*3+1] = g & 0xFF
        raw[i*3+2] = b & 0xFF

    dest = (ip, ARTNET_PORT)
    for u in range(UNIVERSES):
        start = u * 512
        sock.sendto(artdmx_packet(u, bytes(raw[start:start+512])), dest)

# ---------------------------------------------------------------------------
# Color utilities
# ---------------------------------------------------------------------------

def hsv(h: float, s: float = 1.0, v: float = 1.0) -> tuple[int,int,int]:
    """h/s/v in [0,1] → (r, g, b) in [0,255]."""
    if s == 0:
        c = int(v * 255)
        return c, c, c
    h6 = (h % 1.0) * 6.0
    i  = int(h6)
    f  = h6 - i
    p, q, t_ = v*(1-s), v*(1-s*f), v*(1-s*(1-f))
    r, g, b = [(v,t_,p), (q,v,p), (p,v,t_), (p,q,v), (t_,p,v), (v,p,q)][i % 6]
    return int(r*255), int(g*255), int(b*255)

def clamp(x: float) -> int:
    return max(0, min(255, int(x)))

# ---------------------------------------------------------------------------
# Patterns
# ---------------------------------------------------------------------------
# Each pattern is a function f(t: float) → list[(r,g,b)] of length TOTAL_PIXELS
# t is elapsed time in seconds.

def pat_bars(t):
    """Six vertical color bars: R G B C M Y — good first sanity check."""
    colors = [(255,0,0),(0,255,0),(0,0,255),(0,255,255),(255,0,255),(255,255,0)]
    n = len(colors)
    return [colors[x * n // PANEL_W] for y in range(PANEL_H) for x in range(PANEL_W)]

def pat_white(t):
    return [(255, 255, 255)] * TOTAL_PIXELS

def pat_red(t):
    return [(255, 0, 0)] * TOTAL_PIXELS

def pat_green(t):
    return [(0, 255, 0)] * TOTAL_PIXELS

def pat_blue(t):
    return [(0, 0, 255)] * TOTAL_PIXELS

def pat_black(t):
    return [(0, 0, 0)] * TOTAL_PIXELS

def pat_grid(t):
    """8-pixel white grid on black — useful for checking panel alignment."""
    return [(40,40,40) if (x % 8 == 0 or y % 8 == 0) else (0,0,0)
            for y in range(PANEL_H) for x in range(PANEL_W)]

def pat_corners(t):
    """Lit corners and centre pixel — confirms row/column orientation."""
    bright = (200, 200, 200)
    centre = (0, 200, 0)
    px = [(0,0,0)] * TOTAL_PIXELS
    for (cy, cx) in [(0,0),(0,PANEL_W-1),(PANEL_H-1,0),(PANEL_H-1,PANEL_W-1)]:
        px[cy * PANEL_W + cx] = bright
    px[(PANEL_H//2) * PANEL_W + (PANEL_W//2)] = centre
    return px

def pat_rainbow(t):
    """Horizontal rainbow that sweeps over time."""
    speed = 0.25
    return [hsv((x / PANEL_W + t * speed) % 1.0)
            for y in range(PANEL_H) for x in range(PANEL_W)]

def pat_gradient(t):
    """Top-to-bottom hue gradient that rotates."""
    speed = 0.15
    return [hsv((y / PANEL_H + t * speed) % 1.0)
            for y in range(PANEL_H) for x in range(PANEL_W)]

def pat_plasma(t):
    """2D sine interference plasma."""
    speed = 1.2
    pixels = []
    for y in range(PANEL_H):
        yf = y / PANEL_H * 6.0
        for x in range(PANEL_W):
            xf = x / PANEL_W * 6.0
            v = (math.sin(xf + t * speed)
               + math.sin(yf + t * speed * 0.7)
               + math.sin((xf + yf) * 0.5 + t * (speed + 0.4))) / 3.0
            pixels.append(hsv((v * 0.5 + 0.5 + t * 0.08) % 1.0, 1.0, 0.9))
    return pixels

def pat_chase(t):
    """Bright horizontal dot with fading trail."""
    speed = 20.0  # pixels/sec
    pos   = (t * speed) % PANEL_W
    trail = 8
    pixels = []
    for y in range(PANEL_H):
        for x in range(PANEL_W):
            dist = (x - pos) % PANEL_W
            if dist < trail:
                brightness = 1.0 - dist / trail
                h = (t * 0.1) % 1.0
                pixels.append(hsv(h, 0.8, brightness))
            else:
                pixels.append((0, 0, 0))
    return pixels

def pat_twinkle(t):
    """Random sparkle on a dark background."""
    density = 0.04  # fraction of pixels lit each frame
    pixels = [(0, 0, 0)] * TOTAL_PIXELS
    count = int(TOTAL_PIXELS * density)
    for _ in range(count):
        idx = random.randint(0, TOTAL_PIXELS - 1)
        pixels[idx] = hsv(random.random(), 0.5 + random.random() * 0.5)
    return pixels

def pat_ripple(t):
    """Expanding circular ripple from the centre."""
    cx, cy  = PANEL_W / 2.0, PANEL_H / 2.0
    speed   = 12.0
    spacing = 14.0
    pixels  = []
    for y in range(PANEL_H):
        for x in range(PANEL_W):
            r = math.sqrt((x - cx)**2 + (y - cy)**2)
            v = math.sin(r / spacing * math.pi * 2 - t * speed)
            h = (r / 40.0 + t * 0.05) % 1.0
            pixels.append(hsv(h, 1.0, max(0.0, v)))
    return pixels

def pat_scan(t):
    """Single bright scan line that sweeps top to bottom."""
    row = int((t * 20) % PANEL_H)
    return [(200, 200, 200) if y == row else
            (10, 10, 10)    if y == (row - 1) % PANEL_H else
            (0, 0, 0)
            for y in range(PANEL_H) for x in range(PANEL_W)]

# ---------------------------------------------------------------------------
# Pattern registry
# ---------------------------------------------------------------------------

PATTERNS = {
    "bars":     (pat_bars,     "Six vertical color bars R G B C M Y"),
    "white":    (pat_white,    "All white"),
    "red":      (pat_red,      "All red"),
    "green":    (pat_green,    "All green"),
    "blue":     (pat_blue,     "All blue"),
    "black":    (pat_black,    "All off"),
    "grid":     (pat_grid,     "8px grid — panel alignment check"),
    "corners":  (pat_corners,  "Lit corners + centre pixel — orientation check"),
    "rainbow":  (pat_rainbow,  "Horizontal rainbow sweep"),
    "gradient": (pat_gradient, "Top-to-bottom hue gradient"),
    "plasma":   (pat_plasma,   "2D sine interference plasma"),
    "chase":    (pat_chase,    "Horizontal dot with fading trail"),
    "twinkle":  (pat_twinkle,  "Random sparkle"),
    "ripple":   (pat_ripple,   "Expanding ripple from centre"),
    "scan":     (pat_scan,     "Scan line sweep"),
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Send Art-Net test patterns to artnode-rpi.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Examples:\n"
               f"  %(prog)s --ip {DEFAULT_IP}\n"
               f"  %(prog)s --ip {DEFAULT_IP} --pattern plasma\n"
               f"  %(prog)s --ip {DEFAULT_IP} --pattern bars --seconds 5\n"
               f"  %(prog)s --list",
    )
    parser.add_argument("--ip",      default=DEFAULT_IP,
                        help=f"Target IP address (default: {DEFAULT_IP})")
    parser.add_argument("--pattern", default="rainbow",
                        choices=list(PATTERNS.keys()),
                        metavar="PATTERN",
                        help="Pattern name (default: rainbow)")
    parser.add_argument("--fps",     type=float, default=30.0,
                        help="Frames per second (default: 30)")
    parser.add_argument("--seconds", type=float, default=0.0,
                        help="Stop after N seconds; 0 = run until Ctrl-C (default: 0)")
    parser.add_argument("--list",    action="store_true",
                        help="List available patterns and exit")
    args = parser.parse_args()

    if args.list:
        print("Available patterns:")
        for name, (_, desc) in PATTERNS.items():
            print(f"  {name:<10}  {desc}")
        return

    fn, desc = PATTERNS[args.pattern]
    frame_dur = 1.0 / args.fps
    deadline  = time.monotonic() + args.seconds if args.seconds > 0 else math.inf

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(f"Pattern : {args.pattern} — {desc}")
    print(f"Target  : {args.ip}:{ARTNET_PORT}")
    print(f"Panel   : {PANEL_W}×{PANEL_H}  ({TOTAL_PIXELS} px, {UNIVERSES} universes)  — edit PANEL_W/H at top of script to change")
    print(f"FPS     : {args.fps}")
    print("Press Ctrl-C to stop.\n")

    frames     = 0
    t          = 0.0
    start      = time.monotonic()
    report_at  = start + 5.0

    try:
        while time.monotonic() < deadline:
            tick = time.monotonic()

            send_frame(sock, args.ip, fn(t))
            frames += 1
            t += frame_dur

            now = time.monotonic()
            if now >= report_at:
                fps_actual = frames / (now - start)
                print(f"  {frames:6d} frames  {fps_actual:5.1f} fps  t={t:.1f}s")
                report_at = now + 5.0

            sleep = frame_dur - (now - tick)
            if sleep > 0:
                time.sleep(sleep)

    except KeyboardInterrupt:
        elapsed = time.monotonic() - start
        print(f"\nStopped after {frames} frames ({elapsed:.1f}s, "
              f"{frames/elapsed:.1f} fps avg)")
    finally:
        # Leave the panel dark on exit
        send_frame(sock, args.ip, [(0, 0, 0)] * TOTAL_PIXELS)
        sock.close()

if __name__ == "__main__":
    main()
