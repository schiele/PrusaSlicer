"""Custom fan speed curves based on layer height and feature type.

Sample script - adjust the fan curve and role overrides for your
material. The defaults below are tuned for PLA on a typical setup.

Goes beyond the slicer's simple min/max fan settings by providing
full programmatic control. Useful for materials like PETG that
need careful cooling management - aggressive on bridges, gentle
on bulk, ramping through the transition zone.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole


# Height-based fan curve (Z in mm -> fan %)
# Linearly interpolates between defined points
FAN_CURVE = [
    (0.0,   0),    # no fan on first layer
    (0.6,   0),    # still off through first few layers
    (1.0,  30),    # gentle ramp
    (3.0,  50),    # mid-print cruise
    (10.0, 60),    # slight increase for upper layers
]

# Per-role overrides (these take priority over the height curve)
ROLE_FAN = {
    ExtrusionRole.BridgeInfill:       100,
    ExtrusionRole.OverhangPerimeter:  100,
    ExtrusionRole.TopSolidInfill:      80,  # cosmetic surface needs cooling
    ExtrusionRole.Ironing:             60,
    ExtrusionRole.SupportMaterialInterface: 70,
}

# Roles that should always use the height curve (no override)
CURVE_ONLY_ROLES = {
    ExtrusionRole.InternalInfill,
    ExtrusionRole.SolidInfill,
    ExtrusionRole.Perimeter,
    ExtrusionRole.ExternalPerimeter,
}


def interpolate_fan(z):
    if z <= FAN_CURVE[0][0]:
        return FAN_CURVE[0][1]
    if z >= FAN_CURVE[-1][0]:
        return FAN_CURVE[-1][1]
    for i in range(len(FAN_CURVE) - 1):
        z0, f0 = FAN_CURVE[i]
        z1, f1 = FAN_CURVE[i + 1]
        if z0 <= z <= z1:
            t = (z - z0) / (z1 - z0)
            return f0 + t * (f1 - f0)
    return FAN_CURVE[-1][1]


def process(gcode: preFlight.GCode):
    modified = 0
    for layer in gcode.layers:
        base_fan = interpolate_fan(layer.z)

        for move in layer.moves:
            if move.type != MoveType.Extrude:
                continue

            if move.role in ROLE_FAN:
                move.annotation = f"fan override {ROLE_FAN[move.role]:.0f}%"
                move.fan_speed = ROLE_FAN[move.role]
            else:
                move.annotation = f"fan curve {base_fan:.0f}%"
                move.fan_speed = base_fan
            modified += 1

    print(f"[fan_curves] Applied custom fan curve to {modified} moves")
