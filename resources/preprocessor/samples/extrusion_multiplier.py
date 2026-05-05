"""Per-feature extrusion multiplier - adjust flow ratio by feature type.

Sample script - tune the multiplier values for your filament. The
defaults are subtle adjustments to illustrate the concept.

Applies different extrusion multipliers to different feature types.
Useful for fine-tuning flow without affecting all features equally -
e.g. slightly over-extrude external perimeters for better surface
finish while under-extruding infill to save material.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole

# Extrusion multipliers per feature type (1.0 = no change)
MULTIPLIERS = {
    ExtrusionRole.ExternalPerimeter:       1.02,  # slight over-extrusion for surface quality
    ExtrusionRole.Perimeter:               1.00,
    ExtrusionRole.OverhangPerimeter:       0.95,  # reduce flow on overhangs
    ExtrusionRole.InternalInfill:          0.95,  # save material on infill
    ExtrusionRole.SolidInfill:             1.00,
    ExtrusionRole.TopSolidInfill:          1.02,  # better top surface
    ExtrusionRole.BridgeInfill:            0.90,  # less flow for cleaner bridges
    ExtrusionRole.GapFill:                 0.90,  # gap fill tends to over-extrude
    ExtrusionRole.SupportMaterial:         0.95,
    ExtrusionRole.SupportMaterialInterface: 0.95,
    ExtrusionRole.Skirt:                   1.00,
    ExtrusionRole.Ironing:                 1.00,
}


def process(gcode: preFlight.GCode):
    modified = 0
    by_role = {}

    for move in gcode.moves:
        if move.type != MoveType.Extrude or move.delta_e <= 0:
            continue

        multiplier = MULTIPLIERS.get(move.role)
        if multiplier is None or multiplier == 1.0:
            continue

        move.annotation = f"flow x{multiplier:.2f}"
        move.delta_e *= multiplier
        modified += 1
        by_role[move.role] = by_role.get(move.role, 0) + 1

    print(f"[extrusion_multiplier] Modified {modified} moves")
    for role, count in sorted(by_role.items(), key=lambda x: -x[1]):
        print(f"    {role}: {count} moves at {MULTIPLIERS[role]:.0%}")
