"""Context-aware retraction tuning.

Sample script - adjust the travel thresholds and retraction factors
for your bowden/direct drive setup.

Adjusts retraction distance and speed based on what comes before
and after the travel move. Short hops between perimeters need less
retraction than long travels over infill. Reduces stringing while
minimizing unnecessary filament grinding.
"""

import preFlight
from preFlight import MoveType, ExtrusionRole
import math

# Short travel threshold (mm) - reduce retraction for short hops
SHORT_TRAVEL = 2.0
SHORT_RETRACT_FACTOR = 0.5  # use 50% of normal retraction

# Long travel threshold (mm) - increase retraction for long moves
LONG_TRAVEL = 20.0
LONG_RETRACT_FACTOR = 1.3  # use 130% of normal retraction

# Roles where stringing is cosmetically visible
VISIBLE_ROLES = {
    ExtrusionRole.ExternalPerimeter,
    ExtrusionRole.TopSolidInfill,
    ExtrusionRole.Ironing,
}

# Extra retraction when traveling TO a visible feature
VISIBLE_DESTINATION_BONUS = 1.2


def travel_distance(move_from, move_to):
    dx = move_to.x - move_from.x
    dy = move_to.y - move_from.y
    return math.sqrt(dx * dx + dy * dy)


def process(gcode: preFlight.GCode):
    modified = 0
    moves = gcode.moves
    i = 0

    while i < len(moves):
        move = moves[i]
        if move.type != MoveType.Retract:
            i += 1
            continue

        # Find the travel and next extrusion after this retraction
        travel_dist = 0.0
        next_role = None
        prev_role = None

        # Look backward for the role before retraction
        for j in range(i - 1, max(i - 10, -1), -1):
            if moves[j].type == MoveType.Extrude:
                prev_role = moves[j].role
                break

        # Look forward through travels to find destination
        prev_pos = move
        for j in range(i + 1, min(i + 50, len(moves))):
            if moves[j].type == MoveType.Travel:
                travel_dist += travel_distance(prev_pos, moves[j])
                prev_pos = moves[j]
            elif moves[j].type == MoveType.Extrude:
                next_role = moves[j].role
                break
            elif moves[j].type == MoveType.Unretract:
                continue
            else:
                break

        # Determine retraction multiplier
        factor = 1.0

        if travel_dist < SHORT_TRAVEL:
            factor *= SHORT_RETRACT_FACTOR
        elif travel_dist > LONG_TRAVEL:
            factor *= LONG_RETRACT_FACTOR

        if next_role in VISIBLE_ROLES:
            factor *= VISIBLE_DESTINATION_BONUS

        if factor != 1.0:
            move.annotation = f"retract x{factor:.2f}"
            move.delta_e *= factor
            modified += 1

        i += 1

    print(f"[retraction_optimizer] Adjusted {modified} retractions based on context")
