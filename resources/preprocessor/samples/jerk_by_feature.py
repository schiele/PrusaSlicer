"""Per-feature jerk control - tune jerk/junction values by extrusion role.

Sample script - jerk values are printer-specific. Calibrate your own
values before using (the defaults below are illustrative only).

Low jerk on external perimeters produces smoother corners and cleaner
surfaces. High jerk on infill and travel allows faster direction
changes without sacrificing print quality on visible features.

Supports:
  RepRapFirmware:  M566 X<val> Y<val> (values in mm/min)
  Klipper:         SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=<val>
  Marlin:          M205 X<val> Y<val>

Auto-detects firmware from gcode.settings.gcode_flavor when possible.

WARNING: RepRapFirmware M566 expects mm/min, not mm/s. The values
in JERK below are in mm/s and are converted automatically for RRF.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole

# Jerk values (mm/s) by feature type
JERK = {
    ExtrusionRole.ExternalPerimeter:        5,
    ExtrusionRole.Perimeter:                8,
    ExtrusionRole.OverhangPerimeter:        4,
    ExtrusionRole.InternalInfill:          12,
    ExtrusionRole.SolidInfill:             10,
    ExtrusionRole.TopSolidInfill:           6,
    ExtrusionRole.BridgeInfill:             5,
    ExtrusionRole.GapFill:                  6,
    ExtrusionRole.SupportMaterial:         10,
    ExtrusionRole.SupportMaterialInterface: 8,
    ExtrusionRole.Ironing:                  5,
    ExtrusionRole.Skirt:                    8,
    ExtrusionRole.WipeTower:               10,
}

# Jerk for travel moves (between extrusions)
TRAVEL_JERK = 12

DEFAULT_JERK = 8

# Override firmware detection: set to "marlin", "klipper", or "reprap"
# Leave as None to auto-detect from slicer settings
FIRMWARE_OVERRIDE = None


def detect_firmware(gcode: preFlight.GCode):
    """Detect firmware type from gcode_flavor setting."""
    if FIRMWARE_OVERRIDE:
        return FIRMWARE_OVERRIDE

    flavor = gcode.settings.gcode_flavor
    if "reprap" in flavor.lower() or "duet" in flavor.lower():
        return "reprap"
    if "klipper" in flavor.lower():
        return "klipper"
    return "marlin"


def format_command(firmware, jerk_value):
    """Format the jerk command for the detected firmware."""
    if firmware == "reprap":
        mm_per_min = jerk_value * 60
        return f"M566 X{mm_per_min:.0f} Y{mm_per_min:.0f}"
    if firmware == "klipper":
        return f"SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY={jerk_value:.1f}"
    return f"M205 X{jerk_value:.1f} Y{jerk_value:.1f}"


def process(gcode: preFlight.GCode):
    firmware = detect_firmware(gcode)
    current_jerk = None
    insertions = 0

    for layer in gcode.layers:
        for move in layer.moves:
            if move.type == MoveType.Travel:
                target = TRAVEL_JERK
            elif move.type == MoveType.Extrude:
                target = JERK.get(move.role, DEFAULT_JERK)
            else:
                continue

            if target != current_jerk:
                cmd = format_command(firmware, target)
                role_name = str(move.role).split(".")[-1] if move.type == MoveType.Extrude else "travel"
                gcode.insert(
                    move.gcode_line_id - 1,
                    cmd,
                    comment=f"jerk for {role_name}"
                )
                current_jerk = target
                insertions += 1

    print(f"[jerk_by_feature] {firmware}: {insertions} jerk transitions")
