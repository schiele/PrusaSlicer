"""Optimize feedrates based on motion analysis and annotate moves.

Sample script - uses physics-based calculations so no tuning is needed,
but verify the results match your firmware's motion planner behavior.

Uses distance, junction angle, acceleration, and max entry speed to
identify moves that can never reach their commanded feedrate due to
insufficient distance. Clamps those moves to their achievable peak
speed, making the commanded feedrate match what the firmware will
actually execute.

Also annotates every extrusion move with its distance and junction
angle for visual inspection in the G-code preview and exported file.

Benefits:
  - Flow calculations become accurate (no over-extrusion on short moves)
  - Preview speed display shows realistic values
  - Reduces planner computation on 8-bit boards
"""

import math
import preFlight
from preFlight import MoveType


def process(gcode: preFlight.GCode):
    if tuple(int(x) for x in preFlight.version.split(".")) < (0, 9, 13):
        print(f"[motion_optimizer] Requires preFlight 0.9.13+ (running {preFlight.version})")
        return

    clamped = 0
    annotated = 0
    total_extrusions = 0
    max_overshoot = 0.0

    for layer in gcode.layers:
        for move in layer.moves:
            if move.type != MoveType.Extrude:
                continue

            total_extrusions += 1

            if move.distance <= 0 or move.acceleration <= 0:
                continue

            # Max achievable speed: accelerate for half the distance, decelerate for the other half
            # v_peak = sqrt(v_entry^2 + a * d) for a symmetric accel/decel triangle
            v_entry = move.max_entry_speed
            v_peak = math.sqrt(v_entry * v_entry + move.acceleration * move.distance)

            if move.feedrate > v_peak:
                overshoot = move.feedrate - v_peak
                if overshoot > max_overshoot:
                    max_overshoot = overshoot

                move.annotation = (f"dist={move.distance:.2f}mm jA={move.junction_angle:.1f}deg "
                                   f"clamped {move.feedrate:.1f}->{v_peak:.1f}mm/s")
                move.feedrate = v_peak
                clamped += 1
            else:
                move.annotation = f"dist={move.distance:.2f}mm jA={move.junction_angle:.1f}deg"

            annotated += 1

    pct = (clamped / total_extrusions * 100) if total_extrusions > 0 else 0
    print(f"[motion_optimizer] {annotated} moves annotated, "
          f"{clamped}/{total_extrusions} ({pct:.1f}%) clamped to achievable speed")
    if max_overshoot > 0:
        print(f"[motion_optimizer] Largest overshoot: {max_overshoot:.1f} mm/s")

    metrics = gcode.overall_metrics
    if metrics["max_commands_per_sec"] > 0:
        print(f"[motion_optimizer] Peak command rate: {metrics['max_commands_per_sec']} cmd/s "
              f"(layer {metrics['max_layer']})")
