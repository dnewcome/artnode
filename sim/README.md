# Mesh simulator

Python simulator for the ESP-NOW mesh protocol in `src/espnow_mesh.{h,cpp}`.
Models a shared broadcast bus with configurable loss, jitter, and reorder; runs
N slave nodes against one bridge; verifies fragmentation/reassembly and pattern
sync. On-wire byte layout matches the C structs, so the same packet bytes could
be fed to firmware later.

## Run

```
python3 sim/mesh_sim.py                                     # clean bus, 3 nodes
python3 sim/mesh_sim.py --nodes 8 --loss 0.1 --quiet        # 10% loss
python3 sim/mesh_sim.py --jitter-ms 5 --reorder 0.2         # delivery chaos
python3 sim/mesh_sim.py --universe-bytes 512 --duration 2   # 3-fragment frames
```

Summary lines print sent/dropped/delivered counts and per-node frames received
vs. expected.

## Flags

| flag | meaning |
|---|---|
| `--nodes N` | number of slaves |
| `--universes U` | DMX universes the bridge emits per frame |
| `--universe-bytes B` | bytes per universe (≤512) |
| `--fps F` | DMX frame rate |
| `--pattern-ms MS` | pattern sync interval |
| `--duration S` | seconds to simulate |
| `--loss P` | per-recipient drop probability |
| `--jitter-ms MS` | uniform delivery jitter window |
| `--reorder P` | probability a packet gets extra delay (causes reorder) |
| `--seed N` | RNG seed |
| `--quiet` | summary only |

## Visualization

```
pip install pygame
python3 sim/mesh_viz.py                            # 6 nodes, clean bus
python3 sim/mesh_viz.py --nodes 8 --loss 0.1       # with packet loss
python3 sim/mesh_viz.py --jitter-ms 10 --reorder 0.3
```

Arranges N nodes side-by-side across a virtual canvas. A time-animated
rainbow drifts across virtual X; each node receives its own universe and
draws its strip in its own horizontal lane. Degraded delivery shows as
stale/mismatched colors between nodes.

Hotkeys: `space` pause, `l`/`L` -/+ loss, `j`/`J` -/+ jitter,
`r`/`R` -/+ reorder, `p` 2-second 50%-loss pulse, `q` quit.

## What it does NOT simulate

- Radio effects beyond loss/jitter/reorder (no collisions, no RSSI, no channel)
- The firmware's 4-slot concurrent reassembly is modeled, but WiFi scheduling
  interactions (e.g. Artnet UDP vs. ESP-NOW priority on the MCU) are not
- Pattern rendering (only the sync packet delivery is checked)
