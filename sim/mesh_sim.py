#!/usr/bin/env python3
"""
ESP-NOW mesh simulator for artnode.

Mirrors the protocol in src/espnow_mesh.{h,cpp}: a shared broadcast bus,
MeshDmxPacket fragmentation/reassembly, and MeshPatternPacket sync.
The on-wire byte layout matches the C structs so this can also be used
to fuzz the firmware decoder later.

Run examples:
    python3 mesh_sim.py                       # 3 slaves, clean bus
    python3 mesh_sim.py --nodes 8 --loss 0.1  # 8 slaves, 10% packet loss
    python3 mesh_sim.py --jitter-ms 5 --reorder 0.2
"""

import argparse
import heapq
import random
import struct
import time
from dataclasses import dataclass, field
from typing import Callable

# ---- Protocol constants (must match espnow_mesh.h) ----
MESH_MAGIC       = 0xAE
MESH_PKT_DMX     = 0x01
MESH_PKT_PATTERN = 0x02
MESH_FRAG_SIZE   = 240
MESH_MAX_FRAGS   = 3

# struct __attribute__((packed)) MeshDmxPacket {
#     uint8_t  magic, type, universe, seq, frag, num_frags;
#     uint16_t total_len;
#     uint8_t  data[MESH_FRAG_SIZE];
# };
DMX_HDR    = "<BBBBBBH"             # 8 bytes
DMX_HDR_SZ = struct.calcsize(DMX_HDR)
DMX_PKT_SZ = DMX_HDR_SZ + MESH_FRAG_SIZE

# struct __attribute__((packed)) MeshPatternPacket {
#     uint8_t  magic, type, pattern, param1, param2;
#     uint32_t frame_t;
# };
PAT_FMT    = "<BBBBBI"
PAT_PKT_SZ = struct.calcsize(PAT_FMT)


def pack_dmx(universe: int, seq: int, frag: int, num_frags: int,
             total_len: int, payload: bytes) -> bytes:
    if len(payload) > MESH_FRAG_SIZE:
        raise ValueError("fragment payload too large")
    padded = payload + b"\x00" * (MESH_FRAG_SIZE - len(payload))
    return struct.pack(DMX_HDR, MESH_MAGIC, MESH_PKT_DMX,
                       universe, seq, frag, num_frags, total_len) + padded


def pack_pattern(pattern: int, p1: int, p2: int, frame_t: int) -> bytes:
    return struct.pack(PAT_FMT, MESH_MAGIC, MESH_PKT_PATTERN,
                       pattern, p1, p2, frame_t & 0xFFFFFFFF)


# ---- Bus: shared broadcast medium with optional loss / jitter / reorder ----
class Bus:
    def __init__(self, loss: float = 0.0, jitter_ms: float = 0.0,
                 reorder: float = 0.0, rng: random.Random | None = None):
        self.loss = loss
        self.jitter_ms = jitter_ms
        self.reorder = reorder
        self.rng = rng or random.Random()
        self.subscribers: list[Callable[[bytes, str], None]] = []
        self.queue: list[tuple[float, int, bytes, str]] = []
        self._counter = 0
        self.stats = {"sent": 0, "dropped": 0, "delivered": 0}

    def subscribe(self, cb: Callable[[bytes, str], None]):
        self.subscribers.append(cb)

    def send(self, sender: str, data: bytes, now: float):
        self.stats["sent"] += 1
        for sub in self.subscribers:
            if self.rng.random() < self.loss:
                self.stats["dropped"] += 1
                continue
            delay = 0.0
            if self.jitter_ms > 0:
                delay = self.rng.uniform(0, self.jitter_ms) / 1000.0
            if self.rng.random() < self.reorder:
                # bump delay to a larger window so this packet swaps order with siblings
                delay += self.rng.uniform(self.jitter_ms, self.jitter_ms * 4 + 5) / 1000.0
            self._counter += 1
            heapq.heappush(self.queue,
                           (now + delay, self._counter, sub, data, sender))

    def deliver_due(self, now: float):
        while self.queue and self.queue[0][0] <= now:
            _, _, sub, data, sender = heapq.heappop(self.queue)
            sub(data, sender)
            self.stats["delivered"] += 1


# ---- Slave node: mirrors EspNowMesh._onReceive reassembly logic ----
@dataclass
class ReassemblySlot:
    active: bool = False
    universe: int = 0
    seq: int = 0
    num_frags: int = 0
    total_len: int = 0
    recv_mask: int = 0
    buf: bytearray = field(default_factory=lambda: bytearray(512))


class Node:
    def __init__(self, name: str, bus: Bus, log: Callable[[str], None],
                 universe: int = 0, num_leds: int = 0,
                 origin_x: float = 0.0, origin_y: float = 0.0,
                 step_x: float = 1.0, step_y: float = 0.0):
        self.name = name
        self.bus = bus
        self.log = log
        self.universe = universe
        self.num_leds = num_leds
        self.origin_x = origin_x
        self.origin_y = origin_y
        self.step_x = step_x
        self.step_y = step_y
        self.leds = [(0, 0, 0)] * num_leds
        self.slots = [ReassemblySlot() for _ in range(4)]
        self.last_pattern: tuple[int, int, int, int] | None = None
        self.frames_received = 0
        self.frames_dropped = 0  # incomplete reassemblies evicted
        bus.subscribe(self._recv)

    def led_pos(self, i: int) -> tuple[float, float]:
        return (self.origin_x + i * self.step_x,
                self.origin_y + i * self.step_y)

    def _slot_for(self, universe: int, seq: int, num_frags: int, total_len: int) -> ReassemblySlot:
        oldest = 0
        for i, s in enumerate(self.slots):
            if s.active and s.universe == universe:
                if s.seq == seq:
                    return s
                # Stale — evict (matches firmware behaviour)
                if s.recv_mask != (1 << s.num_frags) - 1:
                    self.frames_dropped += 1
                s.active = False
            if not s.active:
                oldest = i
        s = self.slots[oldest]
        s.active = True
        s.universe = universe
        s.seq = seq
        s.num_frags = num_frags
        s.total_len = total_len
        s.recv_mask = 0
        for i in range(len(s.buf)):
            s.buf[i] = 0
        return s

    def _recv(self, data: bytes, sender: str):
        if sender == self.name:
            return  # don't loop own broadcasts
        if len(data) < 2 or data[0] != MESH_MAGIC:
            return
        ptype = data[1]
        if ptype == MESH_PKT_PATTERN:
            if len(data) < PAT_PKT_SZ:
                return
            _, _, pat, p1, p2, frame_t = struct.unpack(PAT_FMT, data[:PAT_PKT_SZ])
            self.last_pattern = (pat, p1, p2, frame_t)
            self.log(f"[{self.name}] pattern={pat} p1={p1} p2={p2} t={frame_t}")
            return
        if ptype != MESH_PKT_DMX:
            return
        if len(data) < DMX_PKT_SZ:
            return
        _, _, uni, seq, frag, num_frags, total_len = struct.unpack(DMX_HDR, data[:DMX_HDR_SZ])
        payload = data[DMX_HDR_SZ:DMX_HDR_SZ + MESH_FRAG_SIZE]

        if num_frags == 1:
            length = min(total_len, MESH_FRAG_SIZE)
            self._on_complete(uni, bytes(payload[:length]))
            return

        slot = self._slot_for(uni, seq, num_frags, total_len)
        offset = frag * MESH_FRAG_SIZE
        frag_len = min(MESH_FRAG_SIZE, total_len - offset)
        if 0 <= offset and offset + frag_len <= len(slot.buf):
            slot.buf[offset:offset + frag_len] = payload[:frag_len]
            slot.recv_mask |= (1 << frag)

        full = (1 << num_frags) - 1
        if slot.recv_mask == full:
            self._on_complete(slot.universe, bytes(slot.buf[:slot.total_len]))
            slot.active = False

    def _on_complete(self, universe: int, frame: bytes):
        self.frames_received += 1
        if universe == self.universe and self.num_leds > 0:
            # Unpack RGB triplets into the LED buffer
            n = min(self.num_leds, len(frame) // 3)
            for i in range(n):
                self.leds[i] = (frame[i*3], frame[i*3+1], frame[i*3+2])
        digest = sum(frame) & 0xFFFF
        self.log(f"[{self.name}] universe={universe} len={len(frame)} sum={digest:#06x}")


# ---- Bridge: broadcasts DMX universes + periodic pattern sync ----
class Bridge:
    def __init__(self, name: str, bus: Bus):
        self.name = name
        self.bus = bus
        self.seq = 0
        self.frame_t = 0

    def broadcast_universe(self, universe: int, payload: bytes, now: float):
        n = (len(payload) + MESH_FRAG_SIZE - 1) // MESH_FRAG_SIZE
        if n == 0:
            return
        if n > MESH_MAX_FRAGS:
            n = MESH_MAX_FRAGS
        seq = self.seq & 0xFF
        self.seq = (self.seq + 1) & 0xFF
        for f in range(n):
            off = f * MESH_FRAG_SIZE
            chunk = payload[off:off + MESH_FRAG_SIZE]
            pkt = pack_dmx(universe, seq, f, n, len(payload), chunk)
            self.bus.send(self.name, pkt, now)

    def broadcast_pattern(self, pattern: int, p1: int, p2: int, now: float):
        pkt = pack_pattern(pattern, p1, p2, self.frame_t, )
        self.bus.send(self.name, pkt, now)

    def tick_frame(self):
        self.frame_t = (self.frame_t + 1) & 0xFFFFFFFF


# ---- Synthetic DMX source ----
def synth_universe(universe: int, frame_t: int, length: int) -> bytes:
    # Cheap deterministic pattern — enough to verify bytes match end-to-end
    return bytes(((universe * 31 + frame_t + i) & 0xFF) for i in range(length))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--nodes", type=int, default=3, help="number of slave nodes")
    ap.add_argument("--universes", type=int, default=2, help="number of DMX universes the bridge sends")
    ap.add_argument("--universe-bytes", type=int, default=512, help="bytes per universe (≤512; matches firmware buf)")
    ap.add_argument("--fps", type=float, default=30.0, help="DMX frame rate")
    ap.add_argument("--pattern-ms", type=float, default=500.0, help="pattern sync interval")
    ap.add_argument("--duration", type=float, default=2.0, help="seconds to simulate")
    ap.add_argument("--loss", type=float, default=0.0, help="per-recipient packet loss probability")
    ap.add_argument("--jitter-ms", type=float, default=0.0, help="uniform jitter window per delivery")
    ap.add_argument("--reorder", type=float, default=0.0, help="probability a packet gets extra delay (causes reorder)")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--quiet", action="store_true", help="suppress per-packet logs; print summary only")
    args = ap.parse_args()
    if args.universe_bytes > 512:
        ap.error("--universe-bytes must be ≤512 (reassembly buf is 512 in firmware)")

    rng = random.Random(args.seed)
    bus = Bus(loss=args.loss, jitter_ms=args.jitter_ms, reorder=args.reorder, rng=rng)

    def log(msg: str):
        if not args.quiet:
            print(msg)

    nodes = [Node(f"node{i}", bus, log) for i in range(args.nodes)]
    bridge = Bridge("bridge", bus)

    t = 0.0
    end = args.duration
    frame_dt = 1.0 / args.fps
    next_frame = 0.0
    next_pattern = 0.0
    pattern_dt = args.pattern_ms / 1000.0
    # Sim tick ≤ smallest scheduled event — keep modest for jitter resolution
    tick = min(frame_dt, pattern_dt, 0.005)

    while t < end:
        if t >= next_frame:
            for u in range(args.universes):
                payload = synth_universe(u, bridge.frame_t, args.universe_bytes)
                bridge.broadcast_universe(u, payload, t)
            bridge.tick_frame()
            next_frame += frame_dt
        if t >= next_pattern:
            # pattern=1, p1/p2 arbitrary; frame_t carried inside packet
            bridge.broadcast_pattern(1, 0, 0, t)
            next_pattern += pattern_dt
        bus.deliver_due(t)
        t += tick
    # drain
    bus.deliver_due(t + 10.0)

    print("\n--- summary ---")
    print(f"bus: sent={bus.stats['sent']} dropped={bus.stats['dropped']} "
          f"delivered={bus.stats['delivered']}")
    expected_frames = int(args.duration * args.fps) * args.universes
    for n in nodes:
        print(f"{n.name}: frames_received={n.frames_received}/{expected_frames} "
              f"reassembly_dropped={n.frames_dropped} "
              f"last_pattern={n.last_pattern}")


if __name__ == "__main__":
    main()
