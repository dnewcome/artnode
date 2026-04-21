#!/usr/bin/env python3
"""
Pygame visualization for the ESP-NOW mesh simulator.

Arranges N nodes side-by-side across a virtual canvas. A bridge samples a
time-animated scene (hue drifting across virtual X) at each node's LED
positions, fragments the result as a DMX universe per node, and broadcasts.

Hotkeys while running:
    SPACE     toggle pause
    L / l     +/- loss 5%
    J / j     +/- jitter 1ms
    R / r     +/- reorder 5%
    P         pulse 50% loss for 2s (visible desync)
    ESC / Q   quit
"""

import argparse
import math
import random
import time
import pygame

from mesh_sim import Bus, Node, Bridge, MESH_FRAG_SIZE

# ---- Scene: animated rainbow drifting in virtual X ----
def sample_scene(virt_x: float, virt_y: float, virt_w: float, t: float) -> tuple[int, int, int]:
    # Hue from virtual X, modulated over time; full saturation; brightness gentle sine in Y
    hue = ((virt_x / virt_w) + t * 0.2) % 1.0
    # HSV→RGB (cheap inline)
    i = int(hue * 6.0)
    f = hue * 6.0 - i
    v = 1.0
    p = 0.0
    q = 1.0 - f
    tt = f
    if i % 6 == 0: r, g, b = v, tt, p
    elif i % 6 == 1: r, g, b = q, v, p
    elif i % 6 == 2: r, g, b = p, v, tt
    elif i % 6 == 3: r, g, b = p, q, v
    elif i % 6 == 4: r, g, b = tt, p, v
    else: r, g, b = v, p, q
    # vertical brightness modulation so rows look distinct if virt_y varies
    vscale = 0.6 + 0.4 * math.sin(virt_y * 0.15 + t)
    return (int(r * 255 * vscale), int(g * 255 * vscale), int(b * 255 * vscale))


def build_universe_for_node(node: Node, t: float, virt_w: float) -> bytes:
    out = bytearray(node.num_leds * 3)
    for i in range(node.num_leds):
        vx, vy = node.led_pos(i)
        r, g, b = sample_scene(vx, vy, virt_w, t)
        out[i*3]     = r
        out[i*3 + 1] = g
        out[i*3 + 2] = b
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--nodes", type=int, default=6)
    ap.add_argument("--leds-per-node", type=int, default=60)
    ap.add_argument("--fps", type=float, default=30.0, help="DMX frame rate")
    ap.add_argument("--pattern-ms", type=float, default=500.0)
    ap.add_argument("--loss", type=float, default=0.0)
    ap.add_argument("--jitter-ms", type=float, default=0.0)
    ap.add_argument("--reorder", type=float, default=0.0)
    ap.add_argument("--window", type=str, default="1200x500")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    # Each node listens on its own universe and is placed horizontally on the canvas.
    # virt canvas height is 1 (single row) but scene uses both axes; use y=0 for all.
    led_spacing = 1.0   # 1 virtual unit per LED
    gap = 2.0           # gap between nodes (virtual units)
    node_width = args.leds_per_node * led_spacing
    virt_w = args.nodes * node_width + (args.nodes - 1) * gap

    # Sanity: universe must fit in ≤ MESH_MAX_FRAGS * MESH_FRAG_SIZE bytes
    universe_bytes = args.leds_per_node * 3
    assert universe_bytes <= 512, "LED data per node exceeds 512 bytes"
    assert universe_bytes <= MESH_FRAG_SIZE * 3, "exceeds MAX_FRAGS=3"

    rng = random.Random(args.seed)
    bus = Bus(loss=args.loss, jitter_ms=args.jitter_ms, reorder=args.reorder, rng=rng)

    def silent(_msg: str):
        pass

    nodes = []
    for u in range(args.nodes):
        origin_x = u * (node_width + gap)
        nodes.append(Node(
            name=f"node{u}", bus=bus, log=silent,
            universe=u, num_leds=args.leds_per_node,
            origin_x=origin_x, origin_y=0.0,
            step_x=led_spacing, step_y=0.0,
        ))
    bridge = Bridge("bridge", bus)

    # ---- pygame ----
    w, h = (int(s) for s in args.window.split("x"))
    pygame.init()
    screen = pygame.display.set_mode((w, h))
    pygame.display.set_caption("artnode mesh sim")
    font = pygame.font.SysFont("monospace", 14)
    clock = pygame.time.Clock()

    margin = 40
    draw_w = w - 2 * margin

    def vx_to_screen(vx: float) -> int:
        return int(margin + (vx / virt_w) * draw_w)

    def node_band_y(node_idx: int) -> int:
        # Stack nodes vertically too, so you can see per-node activity clearly
        lanes = args.nodes
        lane_h = (h - 120) / max(1, lanes)
        return int(80 + node_idx * lane_h + lane_h / 2)

    sim_t = 0.0
    sim_frame_t = 0
    next_frame = 0.0
    next_pattern = 0.0
    frame_dt = 1.0 / args.fps
    pattern_dt = args.pattern_ms / 1000.0
    paused = False
    pulse_loss_until = 0.0

    last_wall = time.monotonic()
    running = True
    while running:
        now = time.monotonic()
        dt = now - last_wall
        last_wall = now

        for evt in pygame.event.get():
            if evt.type == pygame.QUIT:
                running = False
            elif evt.type == pygame.KEYDOWN:
                if evt.key in (pygame.K_ESCAPE, pygame.K_q):
                    running = False
                elif evt.key == pygame.K_SPACE:
                    paused = not paused
                elif evt.key == pygame.K_l:
                    if evt.mod & pygame.KMOD_SHIFT:
                        bus.loss = min(1.0, bus.loss + 0.05)
                    else:
                        bus.loss = max(0.0, bus.loss - 0.05)
                elif evt.key == pygame.K_j:
                    if evt.mod & pygame.KMOD_SHIFT:
                        bus.jitter_ms = min(200.0, bus.jitter_ms + 1.0)
                    else:
                        bus.jitter_ms = max(0.0, bus.jitter_ms - 1.0)
                elif evt.key == pygame.K_r:
                    if evt.mod & pygame.KMOD_SHIFT:
                        bus.reorder = min(1.0, bus.reorder + 0.05)
                    else:
                        bus.reorder = max(0.0, bus.reorder - 0.05)
                elif evt.key == pygame.K_p:
                    pulse_loss_until = now + 2.0

        # Loss pulse overlay
        effective_loss = bus.loss
        if now < pulse_loss_until:
            effective_loss = max(effective_loss, 0.5)
        saved_loss = bus.loss
        bus.loss = effective_loss

        if not paused:
            # Drive sim in real time. One step per wall-clock tick.
            sim_t += dt
            # Emit any frames whose time has come
            while next_frame <= sim_t:
                for node in nodes:
                    payload = build_universe_for_node(node, next_frame, virt_w)
                    bridge.broadcast_universe(node.universe, payload, next_frame)
                bridge.tick_frame()
                sim_frame_t = bridge.frame_t
                next_frame += frame_dt
            while next_pattern <= sim_t:
                bridge.broadcast_pattern(1, 0, 0, next_pattern)
                next_pattern += pattern_dt
            bus.deliver_due(sim_t)

        bus.loss = saved_loss

        # ---- render ----
        screen.fill((16, 16, 20))
        # header
        header = (
            f"nodes={args.nodes}  leds/node={args.leds_per_node}  fps={args.fps:.0f}  "
            f"loss={bus.loss:.2f}{'*' if now < pulse_loss_until else ''}  "
            f"jitter={bus.jitter_ms:.1f}ms  reorder={bus.reorder:.2f}  "
            f"sent={bus.stats['sent']}  drop={bus.stats['dropped']}"
        )
        screen.blit(font.render(header, True, (220, 220, 220)), (10, 10))
        screen.blit(font.render(
            "[space] pause  [l/L] loss  [j/J] jitter  [r/R] reorder  [p] loss pulse  [q] quit",
            True, (150, 150, 160)), (10, 30))

        # Draw each node's LEDs as a horizontal strip at its virtual X range
        for idx, node in enumerate(nodes):
            y = node_band_y(idx)
            # node label + counters
            label = (f"{node.name} u={node.universe} "
                     f"rx={node.frames_received} drop={node.frames_dropped}")
            screen.blit(font.render(label, True, (180, 180, 200)), (10, y - 24))
            # faint backing bar
            x0 = vx_to_screen(node.origin_x)
            x1 = vx_to_screen(node.origin_x + (node.num_leds - 1) * node.step_x)
            pygame.draw.line(screen, (40, 40, 50), (x0, y), (x1, y), 1)
            # LEDs
            for i, (r, g, b) in enumerate(node.leds):
                vx, _ = node.led_pos(i)
                sx = vx_to_screen(vx)
                pygame.draw.circle(screen, (r, g, b), (sx, y), 5)

        pygame.display.flip()
        clock.tick(60)

    pygame.quit()


if __name__ == "__main__":
    main()
