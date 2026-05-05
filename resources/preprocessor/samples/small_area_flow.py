"""Small area flow compensation - reduce over-extrusion in tight features.

Sample script - adjust the thresholds and flow factors for your hotend
and material. The defaults are conservative starting points.

Small features accumulate heat because the nozzle revisits the same area
before it has cooled. This causes the filament to flow more than intended,
producing blobs and bulging. This script detects small fill regions using
region_area and reduces flow (delta_e) proportionally - smaller areas get
more reduction.

Optionally also slows feedrate in small regions to give the part more
time to cool between passes.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole

# Regions smaller than this (mm^2) get flow reduction
SMALL_AREA_THRESHOLD = 100.0

# Regions smaller than this are "tiny" and get maximum reduction
TINY_AREA_THRESHOLD = 15.0

# Flow multiplier range: 1.0 = no change, 0.85 = 15% reduction
FLOW_AT_THRESHOLD = 0.95     # flow at SMALL_AREA_THRESHOLD
FLOW_AT_TINY = 0.85          # flow at TINY_AREA_THRESHOLD (maximum reduction)

# Speed reduction in small areas (1.0 = no change, 0.7 = 30% slower)
# Slowing down gives the part more cooling time between passes
SPEED_FACTOR_AT_TINY = 0.8

# Which roles to adjust (infill roles that suffer from heat buildup)
AFFECTED_ROLES = {
    ExtrusionRole.InternalInfill,
    ExtrusionRole.SolidInfill,
    ExtrusionRole.TopSolidInfill,
    ExtrusionRole.BridgeInfill,
    ExtrusionRole.Perimeter,
    ExtrusionRole.ExternalPerimeter,
}


def process(gcode: preFlight.GCode):
    modified = 0
    by_role = {}

    for move in gcode.moves:
        if move.type != MoveType.Extrude:
            continue
        if move.role not in AFFECTED_ROLES:
            continue
        if move.region_area <= 0 or move.region_area >= SMALL_AREA_THRESHOLD:
            continue

        # Linear interpolation between threshold and tiny
        t = (SMALL_AREA_THRESHOLD - move.region_area) / (SMALL_AREA_THRESHOLD - TINY_AREA_THRESHOLD)
        t = max(0.0, min(1.0, t))

        flow_factor = FLOW_AT_THRESHOLD + t * (FLOW_AT_TINY - FLOW_AT_THRESHOLD)
        speed_factor = 1.0 + t * (SPEED_FACTOR_AT_TINY - 1.0)

        move.delta_e *= flow_factor
        move.feedrate *= speed_factor
        move.annotation = f"small area {move.region_area:.0f}mm2 flow x{flow_factor:.2f}"
        modified += 1

        role_name = move.role.name
        if role_name not in by_role:
            by_role[role_name] = {"count": 0, "min_area": move.region_area}
        by_role[role_name]["count"] += 1
        by_role[role_name]["min_area"] = min(by_role[role_name]["min_area"], move.region_area)

    print(f"[small_area_flow] Adjusted {modified} moves in regions < {SMALL_AREA_THRESHOLD}mm^2")
    for role_name, stats in sorted(by_role.items(), key=lambda x: -x[1]["count"]):
        print(f"  {role_name}: {stats['count']} moves, "
              f"smallest region: {stats['min_area']:.1f}mm^2")
