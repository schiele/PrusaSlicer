"""Edge slowdown - reduce speed near bed edges based on actual bed geometry.

Sample script - adjust EDGE_SPEED_FACTOR and MARGIN_MM for your printer's
frame rigidity.

Reads the build plate dimensions from gcode.settings.bed_shape and computes
the center of the bed. Moves at the center run at full speed; moves near any
edge are scaled down proportionally. This prevents ringing and artifacts
caused by fast direction changes near the edges where the frame is least
rigid.
"""

import preFlight
from preFlight import MoveType

# How much to slow down at the very edge (0.5 = 50% of original speed)
EDGE_SPEED_FACTOR = 0.5

# The margin (mm) from each edge where slowdown begins.
# Set to 0 to use the full bed - every move scales based on distance to center.
MARGIN_MM = 0


def process(gcode: preFlight.GCode):
    bed_shape = gcode.bed_shape
    if not bed_shape or len(bed_shape) < 3:
        print("[edge_slowdown] No bed shape available, skipping")
        return

    xs = [pt[0] for pt in bed_shape]
    ys = [pt[1] for pt in bed_shape]
    bed_x_min = min(xs)
    bed_x_max = max(xs)
    bed_y_min = min(ys)
    bed_y_max = max(ys)
    bed_cx = (bed_x_min + bed_x_max) / 2.0
    bed_cy = (bed_y_min + bed_y_max) / 2.0
    half_w = (bed_x_max - bed_x_min) / 2.0
    half_h = (bed_y_max - bed_y_min) / 2.0

    margin = MARGIN_MM
    if margin <= 0:
        margin = max(half_w, half_h)

    print(f"[edge_slowdown] Bed: {bed_x_min:.0f},{bed_y_min:.0f} -> "
          f"{bed_x_max:.0f},{bed_y_max:.0f} "
          f"(center {bed_cx:.0f},{bed_cy:.0f})")
    print(f"[edge_slowdown] Edge factor: {EDGE_SPEED_FACTOR}, "
          f"margin: {margin:.0f}mm")

    modified = 0
    for move in gcode.moves:
        if move.type != MoveType.Extrude:
            continue

        # Distance from center as a fraction of the margin (0 = center, 1 = edge)
        dx = abs(move.x - bed_cx) / half_w if half_w > 0 else 0
        dy = abs(move.y - bed_cy) / half_h if half_h > 0 else 0
        edge_proximity = max(dx, dy)

        # Only apply within the margin zone
        ramp_start = 1.0 - (margin / max(half_w, half_h))
        if edge_proximity <= ramp_start:
            continue

        # Linear ramp: 1.0 at ramp_start -> EDGE_SPEED_FACTOR at edge (1.0)
        t = (edge_proximity - ramp_start) / (1.0 - ramp_start)
        t = min(t, 1.0)
        factor = 1.0 - t * (1.0 - EDGE_SPEED_FACTOR)

        move.annotation = f"edge slowdown {factor:.0%}"
        move.feedrate *= factor
        modified += 1

    print(f"[edge_slowdown] Modified {modified} moves "
          f"({EDGE_SPEED_FACTOR:.0%} at edges)")
