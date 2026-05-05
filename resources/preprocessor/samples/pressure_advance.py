"""Per-feature pressure advance - tune PA values by extrusion role.

Sample script - PA values are printer-specific. Calibrate your own
values before using (the defaults below are illustrative only).

Most firmwares apply a single pressure advance value globally, but
optimal compensation varies by feature. External perimeters need
lower PA for clean corners, infill tolerates higher PA for better
flow consistency, and bridges need zero PA to avoid pushing filament
during free-air extrusion.

This script inserts PA commands at every role transition so each
feature type gets its own tuned value.

Supports:
  Marlin:          M900 K<value>
  Klipper:         SET_PRESSURE_ADVANCE ADVANCE=<value>
  RepRapFirmware:  M572 D0 S<value>

Auto-detects firmware from gcode.settings.gcode_flavor when possible.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole

# PA values per feature type - tune these with a calibration print
PA_VALUES = {
    ExtrusionRole.ExternalPerimeter:        0.040,
    ExtrusionRole.Perimeter:                0.050,
    ExtrusionRole.OverhangPerimeter:        0.020,
    ExtrusionRole.TopSolidInfill:           0.040,
    ExtrusionRole.SolidInfill:              0.060,
    ExtrusionRole.InternalInfill:           0.080,
    ExtrusionRole.BridgeInfill:             0.000,
    ExtrusionRole.GapFill:                  0.030,
    ExtrusionRole.SupportMaterial:          0.060,
    ExtrusionRole.SupportMaterialInterface: 0.040,
    ExtrusionRole.Skirt:                    0.050,
    ExtrusionRole.Ironing:                  0.020,
}

DEFAULT_PA = 0.050

# Override firmware detection: set to "marlin", "klipper", or "reprap"
# Leave as None to auto-detect from slicer settings
FIRMWARE_OVERRIDE = None


def detect_firmware(gcode: preFlight.GCode):
    """Detect firmware type from gcode_flavor setting."""
    if FIRMWARE_OVERRIDE:
        return FIRMWARE_OVERRIDE

    flavor = gcode.settings.gcode_flavor
    if "klipper" in flavor.lower():
        return "klipper"
    if "reprap" in flavor.lower() or "duet" in flavor.lower():
        return "reprap"
    return "marlin"


def format_command(firmware, pa_value, extruder=0):
    """Format the PA command for the detected firmware."""
    if firmware == "klipper":
        return f"SET_PRESSURE_ADVANCE ADVANCE={pa_value:.4f}"
    if firmware == "reprap":
        return f"M572 D{extruder} S{pa_value:.4f}"
    return f"M900 K{pa_value:.4f}"


def process(gcode: preFlight.GCode):
    firmware = detect_firmware(gcode)
    insertions = 0
    current_pa = None

    for layer in gcode.layers:
        if layer.id == 0:
            continue

        for move in layer.moves:
            if move.type != MoveType.Extrude:
                continue

            target_pa = PA_VALUES.get(move.role, DEFAULT_PA)
            if target_pa != current_pa:
                cmd = format_command(firmware, target_pa, move.extruder_id)
                role_name = str(move.role).split(".")[-1]
                gcode.insert(move.gcode_line_id, cmd, "before",
                             comment=f"PA {target_pa:.3f} for {role_name}")
                current_pa = target_pa
                insertions += 1

    print(f"[pressure_advance] {firmware}: {insertions} PA transitions (skipped first layer)")
