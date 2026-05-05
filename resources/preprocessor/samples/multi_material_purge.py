"""Intelligent purge optimization for multi-material prints.

Sample script - adjust MIN_PURGE, MAX_PURGE, and DARK_TO_LIGHT_PENALTY
for your MMU/AMS setup. Results depend on filament type and nozzle size.

Adjusts purge volumes based on the actual color transition being made.
Dark-to-light transitions need more purging than light-to-dark.
Transitions between similar colors need almost none. Can reduce
filament waste on multi-material prints compared to fixed purge volumes.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole
import math


def hex_to_rgb(color_hex):
    h = color_hex.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def color_distance(c1, c2):
    """Perceptual color distance (simple weighted Euclidean)."""
    r1, g1, b1 = c1
    r2, g2, b2 = c2
    dr = (r1 - r2) * 0.30
    dg = (g1 - g2) * 0.59
    db = (b1 - b2) * 0.11
    return math.sqrt(dr * dr + dg * dg + db * db)


def luminance(rgb):
    r, g, b = rgb
    return 0.299 * r + 0.587 * g + 0.114 * b


# Purge volume range (mm3)
MIN_PURGE = 20.0    # nearly same color
MAX_PURGE = 180.0   # worst case: black to white

# Extra multiplier when going from dark to light
DARK_TO_LIGHT_PENALTY = 1.4


def calculate_purge_volume(from_color, to_color):
    dist = color_distance(from_color, to_color)
    max_dist = 255.0  # max possible distance

    # Base purge proportional to color distance
    ratio = dist / max_dist
    volume = MIN_PURGE + ratio * (MAX_PURGE - MIN_PURGE)

    # Dark-to-light needs more purging
    if luminance(from_color) < luminance(to_color):
        lum_diff = luminance(to_color) - luminance(from_color)
        penalty = 1.0 + (DARK_TO_LIGHT_PENALTY - 1.0) * (lum_diff / 255.0)
        volume *= penalty

    return min(volume, MAX_PURGE)


def process(gcode: preFlight.GCode):
    if gcode.extruder_count < 2:
        print("[multi_material_purge] Single extruder, nothing to do")
        return

    colors = [hex_to_rgb(c) for c in gcode.extruder_colors]
    optimized = 0
    saved_volume = 0.0

    current_extruder = 0
    for layer in gcode.layers:
        for move in layer.moves:
            if move.type != MoveType.ToolChange:
                continue

            next_extruder = move.extruder_id
            if next_extruder == current_extruder:
                continue

            from_color = colors[current_extruder]
            to_color = colors[next_extruder]
            optimal_purge = calculate_purge_volume(from_color, to_color)

            # Find the wipe tower moves following this tool change
            # and scale their extrusion to match optimal purge
            wipe_moves = layer.moves_by_role(ExtrusionRole.WipeTower)
            if wipe_moves:
                current_purge = sum(m.delta_e * m.mm3_per_mm for m in wipe_moves)
                if current_purge > 0:
                    scale = optimal_purge / current_purge
                    for m in wipe_moves:
                        m.annotation = f"purge x{scale:.2f}"
                        m.delta_e *= scale
                    saved_volume += max(0, current_purge - optimal_purge)
                    optimized += 1

            current_extruder = next_extruder

    saved_grams = saved_volume * 0.00124  # PLA density approximation
    print(f"[multi_material_purge] Optimized {optimized} tool changes, "
          f"saved ~{saved_volume:.0f}mm3 ({saved_grams:.1f}g) of filament")
