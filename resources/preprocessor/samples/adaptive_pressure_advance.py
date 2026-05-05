"""Adaptive Pressure Advance - speed, flow, and corner-aware PA tuning.

Sample script - ALL values below are illustrative. You MUST calibrate
PA_BASE and SPEED_FACTOR for your specific printer/filament combination
before using this in production.

Standard pressure advance uses a single global value, but optimal PA
depends on instantaneous conditions:

  Speed:  Faster moves build more nozzle pressure, needing higher PA.
  Flow:   High volumetric rates increase melt zone pressure.
  Corners: Sharp direction changes cause pressure spikes - the nozzle
           decelerates hard while residual pressure keeps pushing
           filament, causing corner bulge. Reducing PA at corners
           prevents the aggressive decompression from over-correcting.

This script computes PA per-move from the actual (post-acceleration)
feedrate, volumetric flow rate, and junction angle, then inserts
firmware commands at every significant PA transition.

Model:
  pa = pa_base
     + speed_factor  * actual_feedrate
     + vol_factor    * max(0, actual_vol_rate - vol_threshold)
     - corner_reduction(junction_angle)

Supports Marlin (M900), Klipper (SET_PRESSURE_ADVANCE), RepRapFirmware (M572).
"""

import math
import preFlight
from preFlight import MoveType, ExtrusionRole


# ---------------------------------------------------------------------------
# Speed model
# ---------------------------------------------------------------------------

# Base PA at zero speed (your single-value calibration starting point)
PA_BASE = 0.040

# PA increase per mm/s of actual speed
# Calibrate: print PA patterns at 40mm/s and 120mm/s, solve the linear fit
SPEED_FACTOR = 0.0003

# Safety clamps
PA_MIN = 0.000
PA_MAX = 0.120


# ---------------------------------------------------------------------------
# Corner compensation
# ---------------------------------------------------------------------------
# junction_angle: 0 = straight, +/- 90 = right angle, +/- 180 = reversal
# Sharp corners cause pressure spikes during deceleration. Reducing PA
# at corners prevents the decompression phase from gouging the corner.

CORNER_ENABLED = True
CORNER_ANGLE_THRESHOLD = 15.0    # degrees - ignore gentler bends
CORNER_ANGLE_FULL = 90.0         # degrees - full reduction at this sharpness
CORNER_PA_REDUCTION = 0.015      # max PA subtracted at sharp corners


# ---------------------------------------------------------------------------
# Volumetric flow compensation
# ---------------------------------------------------------------------------
# High flow rates increase melt zone pressure beyond what speed alone predicts.
# This adds a second linear term above a volumetric threshold.

VOLUMETRIC_ENABLED = True
VOLUMETRIC_THRESHOLD = 5.0       # mm3/s - no adjustment below this
VOLUMETRIC_FACTOR = 0.002        # PA increase per mm3/s above threshold


# ---------------------------------------------------------------------------
# Per-role base overrides
# ---------------------------------------------------------------------------
# Some features need a fixed PA regardless of speed/flow.
# Set a float to override PA_BASE for that role, or None to use the global.

ROLE_PA_BASE = {
    ExtrusionRole.BridgeInfill:       0.000,   # no PA during free-air bridging
    ExtrusionRole.OverhangPerimeter:  0.020,   # gentle PA for overhangs
    ExtrusionRole.Ironing:            0.010,   # minimal for surface finishing
}

# Roles that get a fixed PA with no adaptive adjustment at all
FIXED_ROLES = {
    ExtrusionRole.BridgeInfill: 0.000,
}


# ---------------------------------------------------------------------------
# First layer
# ---------------------------------------------------------------------------

SKIP_FIRST_LAYER = True
FIRST_LAYER_PA = 0.030           # fixed PA for layer 0, or None to emit nothing


# ---------------------------------------------------------------------------
# Hysteresis and firmware
# ---------------------------------------------------------------------------

# Only emit a command when PA changes by more than this (avoids command spam)
PA_CHANGE_THRESHOLD = 0.002

# None = auto-detect from gcode_flavor; or force "marlin" / "klipper" / "reprap"
FIRMWARE_OVERRIDE = None


# ===========================================================================
# Implementation
# ===========================================================================

def detect_firmware(gcode: preFlight.GCode) -> str:
    if FIRMWARE_OVERRIDE:
        return FIRMWARE_OVERRIDE
    flavor = gcode.settings.gcode_flavor.lower()
    if "klipper" in flavor:
        return "klipper"
    if "reprap" in flavor or "duet" in flavor:
        return "reprap"
    return "marlin"


def format_pa_command(firmware: str, pa: float, extruder: int = 0) -> str:
    if firmware == "klipper":
        return f"SET_PRESSURE_ADVANCE ADVANCE={pa:.4f}"
    if firmware == "reprap":
        return f"M572 D{extruder} S{pa:.4f}"
    return f"M900 K{pa:.4f}"


def corner_reduction(junction_angle: float) -> float:
    """Compute PA reduction from junction sharpness."""
    if not CORNER_ENABLED:
        return 0.0
    sharpness = abs(junction_angle)
    if sharpness <= CORNER_ANGLE_THRESHOLD:
        return 0.0
    t = (sharpness - CORNER_ANGLE_THRESHOLD) / (CORNER_ANGLE_FULL - CORNER_ANGLE_THRESHOLD)
    t = min(t, 1.0)
    return CORNER_PA_REDUCTION * t


def compute_pa(move) -> float:
    """Compute the adaptive PA value for a single extrusion move."""
    if move.role in FIXED_ROLES:
        return FIXED_ROLES[move.role]

    base = ROLE_PA_BASE.get(move.role, PA_BASE)

    speed = move.actual_feedrate if move.actual_feedrate > 0 else move.feedrate
    pa = base + SPEED_FACTOR * speed

    if VOLUMETRIC_ENABLED and move.actual_volumetric_rate > VOLUMETRIC_THRESHOLD:
        pa += VOLUMETRIC_FACTOR * (move.actual_volumetric_rate - VOLUMETRIC_THRESHOLD)

    pa -= corner_reduction(move.junction_angle)

    return max(PA_MIN, min(PA_MAX, pa))


def process(gcode: preFlight.GCode):
    firmware = detect_firmware(gcode)
    current_pa = None
    insertions = 0
    stats = {"speed_range": [999, 0], "pa_range": [999, 0], "corner_adjustments": 0}

    for layer in gcode.layers:
        # First layer handling
        if layer.id == 0:
            if SKIP_FIRST_LAYER and FIRST_LAYER_PA is not None:
                first_ext = next((m for m in layer.moves if m.type == MoveType.Extrude), None)
                if first_ext and current_pa != FIRST_LAYER_PA:
                    cmd = format_pa_command(firmware, FIRST_LAYER_PA, first_ext.extruder_id)
                    gcode.insert(first_ext.gcode_line_id, cmd, "before",
                                 comment=f"first layer PA {FIRST_LAYER_PA:.3f}")
                    current_pa = FIRST_LAYER_PA
                    insertions += 1
            continue

        for move in layer.moves:
            if move.type != MoveType.Extrude:
                continue

            target_pa = compute_pa(move)

            # Track stats
            speed = move.actual_feedrate if move.actual_feedrate > 0 else move.feedrate
            stats["speed_range"][0] = min(stats["speed_range"][0], speed)
            stats["speed_range"][1] = max(stats["speed_range"][1], speed)
            stats["pa_range"][0] = min(stats["pa_range"][0], target_pa)
            stats["pa_range"][1] = max(stats["pa_range"][1], target_pa)
            if abs(move.junction_angle) > CORNER_ANGLE_THRESHOLD:
                stats["corner_adjustments"] += 1

            if current_pa is None or abs(target_pa - current_pa) >= PA_CHANGE_THRESHOLD:
                role_name = str(move.role).split(".")[-1]
                comment_parts = [f"PA {target_pa:.3f} {role_name} @{speed:.0f}mm/s"]
                if abs(move.junction_angle) > CORNER_ANGLE_THRESHOLD:
                    comment_parts.append(f"corner {move.junction_angle:.0f}deg")
                cmd = format_pa_command(firmware, target_pa, move.extruder_id)
                gcode.insert(move.gcode_line_id, cmd, "before",
                             comment=" ".join(comment_parts))
                current_pa = target_pa
                insertions += 1

    lo_spd, hi_spd = stats["speed_range"]
    lo_pa, hi_pa = stats["pa_range"]
    print(f"[adaptive_pa] {firmware}: {insertions} PA commands inserted")
    print(f"  speed range: {lo_spd:.0f}-{hi_spd:.0f} mm/s")
    print(f"  PA range:    {lo_pa:.4f}-{hi_pa:.4f}")
    print(f"  corner adjustments: {stats['corner_adjustments']} moves")
