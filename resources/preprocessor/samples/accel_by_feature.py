"""Per-feature acceleration control via M204.

Sample script - set acceleration values for your printer's capabilities.
The defaults are moderate values for a typical CoreXY.

Inserts firmware acceleration commands at extrusion role transitions.
Gives fine-grained control over print dynamics - low acceleration
for external perimeters (smooth corners), high acceleration for
infill (speed), and tuned values for everything in between.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole

# Acceleration values (mm/s2) by feature type
ACCEL = {
    ExtrusionRole.ExternalPerimeter:       1500,
    ExtrusionRole.Perimeter:               3000,
    ExtrusionRole.OverhangPerimeter:       1000,
    ExtrusionRole.InternalInfill:          5000,
    ExtrusionRole.SolidInfill:             3000,
    ExtrusionRole.TopSolidInfill:          2000,
    ExtrusionRole.BridgeInfill:            1500,
    ExtrusionRole.GapFill:                 2000,
    ExtrusionRole.SupportMaterial:         3000,
    ExtrusionRole.SupportMaterialInterface: 2000,
    ExtrusionRole.Ironing:                 2000,
    ExtrusionRole.Skirt:                   2000,
    ExtrusionRole.WipeTower:               3000,
}

# Travel move acceleration (between extrusions)
TRAVEL_ACCEL = 5000

DEFAULT_ACCEL = 3000


def process(gcode: preFlight.GCode):
    current_accel = None
    insertions = 0

    for layer in gcode.layers:
        for move in layer.moves:
            if move.type == MoveType.Travel:
                target = TRAVEL_ACCEL
            elif move.type == MoveType.Extrude:
                target = ACCEL.get(move.role, DEFAULT_ACCEL)
            else:
                continue

            if target != current_accel:
                role_name = str(move.role).split(".")[-1] if move.type == MoveType.Extrude else "travel"
                gcode.insert(
                    move.gcode_line_id - 1,
                    f"M204 P{target} T{TRAVEL_ACCEL}",
                    comment=f"accel for {role_name}"
                )
                current_accel = target
                insertions += 1

    print(f"[accel_by_feature] Inserted {insertions} acceleration changes")
