"""Cap volumetric flow rate to prevent extruder skipping.

Sample script - adjust the constants below for your hotend and material.

Particularly useful for high-speed prints where certain features
(solid infill, gap fill) can exceed the hotend's melt capacity.
Adjusts feedrate per-move to stay within the limit while preserving
the commanded flow for features that are already safe.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole

MAX_FLOW = 15.0  # mm3/s - typical for a standard V6 at 210C PLA

# Per-role overrides for features that tolerate less flow
ROLE_LIMITS = {
    ExtrusionRole.BridgeInfill: 8.0,
    ExtrusionRole.OverhangPerimeter: 10.0,
    ExtrusionRole.TopSolidInfill: 12.0,
}


def process(gcode: preFlight.GCode):
    clamped = 0
    for layer in gcode.layers:
        for move in layer.moves:
            if move.type != MoveType.Extrude or move.mm3_per_mm == 0:
                continue

            limit = ROLE_LIMITS.get(move.role, MAX_FLOW)
            if move.volumetric_rate > limit:
                move.annotation = f"flow clamped {move.volumetric_rate:.1f}->{limit:.1f} mm3/s"
                move.feedrate = limit / move.mm3_per_mm
                clamped += 1

    print(f"[flow_limiter] Clamped {clamped} moves to {MAX_FLOW} mm3/s max")
