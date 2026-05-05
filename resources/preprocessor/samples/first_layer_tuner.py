"""Fine-tune first layer behavior beyond what slicer settings offer.

Sample script - adjust the ramp and role factors for your bed surface
and material. The defaults are conservative starting points.

Applies a speed ramp across the first layer - starting extra slow
for the initial lines to ensure adhesion, then gradually increasing
to normal first-layer speed. Also supports per-feature overrides
for the first layer specifically.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole

# First layer speed ramp: start at this fraction and ramp to 1.0
# over the first N moves
INITIAL_SPEED_FACTOR = 0.3      # 30% speed for the very first extrusions
RAMP_MOVES = 200                # ramp up over this many extrusion moves
RAMP_TARGET = 1.0               # reach full first-layer speed

# First layer feature-specific speed multipliers
# These apply ON TOP of the ramp
FIRST_LAYER_ROLE_FACTORS = {
    ExtrusionRole.ExternalPerimeter: 0.8,  # extra care on visible edge
    ExtrusionRole.Skirt:            0.9,
    ExtrusionRole.InternalInfill:   1.0,   # infill can go full speed
    ExtrusionRole.SolidInfill:      0.9,
}

# Extra squish: temporarily increase extrusion width for adhesion
# Set to 0.0 to disable
WIDTH_BOOST = 0.0  # mm added to extrusion width on first layer


def process(gcode: preFlight.GCode):
    if not gcode.layers:
        return

    first_layer = gcode.layers[0]
    extrude_index = 0

    for move in first_layer.moves:
        if move.type != MoveType.Extrude:
            continue

        # Speed ramp
        if extrude_index < RAMP_MOVES:
            t = extrude_index / RAMP_MOVES
            ramp = INITIAL_SPEED_FACTOR + t * (RAMP_TARGET - INITIAL_SPEED_FACTOR)
        else:
            ramp = RAMP_TARGET

        role_factor = FIRST_LAYER_ROLE_FACTORS.get(move.role, 0.9)
        move.annotation = f"first layer ramp {ramp * role_factor:.0%}"
        move.feedrate *= ramp * role_factor

        if WIDTH_BOOST > 0:
            move.width += WIDTH_BOOST

        extrude_index += 1

    print(f"[first_layer_tuner] Ramped {extrude_index} moves, "
          f"initial {INITIAL_SPEED_FACTOR:.0%} -> {RAMP_TARGET:.0%} "
          f"over {min(extrude_index, RAMP_MOVES)} moves")
