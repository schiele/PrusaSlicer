#/|/ Copyright (c) preFlight 2025+ oozeBot, LLC
#/|/
#/|/ Released under AGPLv3 or higher
#/|/
"""Print analysis using numpy for vectorized computation.

Demonstrates how numpy accelerates bulk analysis of toolpath data.
Computes per-layer statistics and flags potential quality issues like
excessive volumetric flow or sudden speed changes between adjacent moves.

Requires: pip install numpy (via the Python Console in Preferences)
This is a read-only analysis script - it does not modify the G-code.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole

try:
    import numpy as np
except ImportError:
    np = None


def process(gcode: preFlight.GCode):
    if np is None:
        print("[numpy_analysis] numpy is not installed. To install:")
        print("  Open Preferences > Preprocessing > Open Python Console")
        print("  Run: python python\\get-pip.py")
        print("  Run: pip install numpy")
        return

    # Extract all extrusion moves into numpy arrays for fast computation
    extrusions = [m for m in gcode.moves if m.type == MoveType.Extrude]
    if not extrusions:
        print("[numpy_analysis] No extrusion moves found")
        return

    feedrates = np.array([m.feedrate for m in extrusions])
    vol_rates = np.array([m.volumetric_rate for m in extrusions])
    widths = np.array([m.width for m in extrusions])
    heights = np.array([m.height for m in extrusions])
    times = np.array([m.time for m in extrusions])

    # Compute positions for move-to-move distance and speed delta analysis
    positions = np.array([(m.x, m.y) for m in extrusions])
    distances = np.sqrt(np.sum(np.diff(positions, axis=0) ** 2, axis=1))
    speed_deltas = np.abs(np.diff(feedrates))

    # Overall statistics
    print(f"[numpy_analysis] === Print Analysis ({len(extrusions)} extrusion moves) ===")
    print(f"  Feedrate:   min={feedrates.min():.1f}  mean={feedrates.mean():.1f}  "
          f"max={feedrates.max():.1f}  std={feedrates.std():.1f} mm/s")
    print(f"  Vol. flow:  min={vol_rates.min():.2f}  mean={vol_rates.mean():.2f}  "
          f"max={vol_rates.max():.2f}  std={vol_rates.std():.2f} mm3/s")
    print(f"  Line width: min={widths.min():.3f}  mean={widths.mean():.3f}  "
          f"max={widths.max():.3f} mm")
    print(f"  Print time: {times.sum():.0f}s ({times.sum() / 60:.1f} min)")

    # Flag potential issues
    issues = []

    # High volumetric flow (common cause of under-extrusion)
    flow_95th = np.percentile(vol_rates, 95)
    high_flow = np.sum(vol_rates > 15.0)
    if high_flow > 0:
        issues.append(f"  {high_flow} moves exceed 15 mm3/s volumetric flow "
                      f"(95th percentile: {flow_95th:.1f} mm3/s)")

    # Sudden speed changes (can cause ringing/artifacts)
    large_deltas = np.sum(speed_deltas > 50.0)
    if large_deltas > 0:
        issues.append(f"  {large_deltas} speed jumps > 50 mm/s between adjacent moves "
                      f"(max delta: {speed_deltas.max():.1f} mm/s)")

    # Very short moves (can cause stuttering on some firmware)
    short_moves = np.sum(distances < 0.1)
    if short_moves > 0:
        issues.append(f"  {short_moves} moves shorter than 0.1mm "
                      f"(may cause firmware stuttering)")

    if issues:
        print(f"  Potential issues:")
        for issue in issues:
            print(issue)
    else:
        print(f"  No issues detected")

    # Per-role breakdown
    print(f"  --- By feature ---")
    roles = {}
    for m in extrusions:
        name = str(m.role).split(".")[-1]
        if name not in roles:
            roles[name] = []
        roles[name].append(m.volumetric_rate)

    for name, rates in sorted(roles.items(), key=lambda x: -len(x[1])):
        arr = np.array(rates)
        print(f"  {name:.<28s} {len(rates):>6} moves, "
              f"flow: {arr.mean():.2f} avg / {arr.max():.2f} max mm3/s")
