# preFlight Pre-Processor Python API
#
# This module is provided by preFlight via pybind11 bindings.
# These type stubs define the API contract - the actual implementation
# lives in C++ and is exposed to Python at runtime.
#
# Scripts define a process(gcode) function that preFlight calls
# after slicing, before preview rendering.

from enum import IntEnum
from typing import List, Dict, Optional, Tuple, Callable

# Module-level constant: preFlight version string (e.g. "0.9.13")
version: str


# ---------------------------------------------------------------------------
# Enums - mirror the C++ enums from GCodeProcessor
# ---------------------------------------------------------------------------

class MoveType(IntEnum):
    Noop = 0
    Retract = 1
    Unretract = 2
    Seam = 3
    ToolChange = 4
    ColorChange = 5
    PausePrint = 6
    CustomGCode = 7
    Travel = 8
    Wipe = 9
    Extrude = 10


class ExtrusionRole(IntEnum):
    NoRole = 0
    Perimeter = 1
    ExternalPerimeter = 2
    OverhangPerimeter = 3
    InterlockingPerimeter = 4
    InternalInfill = 5
    SolidInfill = 6
    TopSolidInfill = 7
    Ironing = 8
    BridgeInfill = 9
    GapFill = 10
    Skirt = 11
    SupportMaterial = 12
    SupportMaterialInterface = 13
    WipeTower = 14
    Custom = 15


class CustomEventType(IntEnum):
    ColorChange = 0
    PausePrint = 1
    ToolChange = 2
    Template = 3
    Custom = 4


# ---------------------------------------------------------------------------
# Data classes - thin wrappers over C++ structures via pybind11
# ---------------------------------------------------------------------------

class Move:
    """A single G-code movement command (MoveVertex).

    All positional values in mm, feedrates in mm/s, temperatures in C.
    Writable properties update both the internal structure and the
    corresponding G-code line in the virtual file.
    """

    # Read/write properties (propagate to G-code)
    feedrate: float              # commanded feedrate (mm/s) -> F parameter
    delta_e: float               # filament displacement (mm) -> E parameter
    fan_speed: float             # fan percentage (0-100) -> M106 insertion
    temperature: float           # hotend temperature (C) -> M104 insertion

    # Read/write properties (preview only, no G-code propagation)
    width: float                 # extrusion line width (mm)
    height: float                # layer/extrusion height (mm)

    # Per-move annotation (overrides gcode.annotation for this move)
    annotation: str

    # Read-only properties
    type: MoveType
    role: ExtrusionRole
    x: float
    y: float
    z: float
    extruder_id: int             # active extruder (0-based)
    actual_feedrate: float       # after acceleration limits (mm/s)
    mm3_per_mm: float            # volumetric rate constant
    volumetric_rate: float       # feedrate * mm3_per_mm (mm3/s)
    actual_volumetric_rate: float
    color_id: int                # sequential color change counter
    layer_id: int                # which layer this move belongs to
    gcode_line_id: int           # source line in the virtual file (1-based)
    internal_only: bool          # True for G2/G3 arc segments
    time: float                  # move duration in seconds (normal mode)
    time_stealth: float          # move duration in seconds (stealth mode)

    # Motion analysis (populated from time estimation)
    distance: float              # XYZ path length (mm)
    junction_angle: float        # angle from previous move (degrees, signed: + right, - left)
    acceleration: float          # effective acceleration (mm/s^2, normal mode)
    acceleration_stealth: float  # effective acceleration (mm/s^2, stealth mode)
    max_entry_speed: float       # junction-limited entry speed (mm/s, normal mode)
    max_entry_speed_stealth: float  # junction-limited entry speed (mm/s, stealth mode)

    # Fill region properties (from slicing geometry)
    region_area: float           # mm^2 (island boundary for perimeters, fill surface for infill, 0 if N/A)
    fill_pattern: str            # infill pattern name ("Rectilinear", "Gyroid", etc., "" for non-fill)


class Layer:
    """A group of moves sharing the same layer_id.

    Provides convenience accessors and the ability to inject G-code
    at layer boundaries.
    """

    id: int                      # layer number
    z: float                     # Z height of this layer (mm)
    height: float                # layer height (mm) - delta from previous
    moves: List[Move]            # all moves in this layer
    time: float                  # total layer time (seconds, normal mode)

    def prepend(self, gcode: str, comment: str = "") -> None:
        """Insert G-code lines before the first move in this layer."""
        ...

    def append(self, gcode: str, comment: str = "") -> None:
        """Insert G-code lines after the last move in this layer."""
        ...

    def moves_by_type(self, move_type: MoveType) -> List[Move]:
        """Filter moves by type."""
        ...

    def moves_by_role(self, role: ExtrusionRole) -> List[Move]:
        """Filter moves by extrusion role."""
        ...

    def extrusion_length(self) -> float:
        """Total filament extruded in this layer (mm)."""
        ...

    def travel_distance(self) -> float:
        """Total travel (non-extrusion) distance in this layer (mm)."""
        ...


class FilamentUsage:
    """Filament consumption stats for a role or extruder."""
    meters: float
    grams: float
    volume_mm3: float
    cost: float


class CustomEvent:
    """A custom G-code event (color change, pause, etc.) at a specific Z."""
    z: float
    type: int                    # CustomEventType value
    extruder: int
    color: str
    extra: str                   # custom G-code text


class GCode:
    """Top-level object representing the entire sliced G-code.

    This is the root object passed to process(). It wraps
    GCodeProcessorResult and provides structured access to
    everything GCodeProcessor parsed.
    """

    # Print geometry
    layers: List[Layer]
    moves: List[Move]            # flat access to all moves
    max_print_height: float      # mm
    z_offset: float              # mm
    bed_shape: List[Tuple[float, float]]
    spiral_vase_mode: bool

    # Extruder configuration
    extruder_count: int
    extruder_colors: List[str]   # hex colors (#FF8000)
    filament_diameters: List[float]
    filament_densities: List[float]

    # Timing
    time_estimate_normal: float  # total seconds (normal mode)
    time_estimate_stealth: float # total seconds (stealth mode)
    first_layer_time: float      # first layer time (seconds)

    # Cost
    filament_cost: List[float]   # cost per extruder
    time_cost: float             # machine time cost rate
    currency_symbol: str         # currency symbol (e.g. "$")

    # Active presets
    preset_print: str            # print profile name
    preset_filament: List[str]   # filament profile name(s)
    preset_printer: str          # printer profile name

    # Filament usage
    filament_by_role: Dict[ExtrusionRole, FilamentUsage]
    filament_by_extruder: Dict[int, FilamentUsage]
    filament_by_color_change: List[float]  # volumes (mm3) per color segment

    # Events
    custom_events: List[CustomEvent]

    # Performance metrics
    role_metrics: Dict[ExtrusionRole, dict]  # {role: {max_commands_per_sec, max_layer}}
    overall_metrics: dict        # {max_commands_per_sec, max_layer}
    conflict: Optional[dict]     # {object1, object2, height, layer} or None

    # Slicer settings (all print/printer/filament config as key-value strings)
    # See _settings.py for full list of available keys with types (auto-generated at build)
    settings: 'Settings'         # e.g. settings["layer_height"], settings["nozzle_diameter"]

    # Raw G-code access
    line_count: int              # total lines in the virtual G-code file

    # Global annotation (fallback for moves without per-move annotation)
    annotation: str

    def insert(self, line: int, gcode: str, position: str = "after", comment: str = "") -> None:
        """Insert raw G-code before or after the specified line number.

        position: "after" (default) or "before"
        comment: optional comment appended to the line (prefixed with "; ")
        """
        ...

    def get_line(self, line_id: int) -> str:
        """Read a raw G-code line from the virtual file (1-based line number)."""
        ...

    def rewrite(self, line_id: int, gcode: str, comment: str = "") -> None:
        """Replace a raw G-code line in the virtual file.

        comment: optional comment appended to the line (prefixed with "; ")
        """
        ...

    def find_line(self, text: str) -> int:
        """Find the first line containing text. Returns 1-based line_id, or 0 if not found."""
        ...

    def find_lines(self, text: str) -> List[int]:
        """Find all lines containing text. Returns list of 1-based line_ids."""
        ...

    def find_moves(self, type: MoveType = None, role: ExtrusionRole = None,
                   extruder: int = None, z_min: float = None,
                   z_max: float = None) -> List[Move]:
        """Query moves by criteria. All parameters are optional filters."""
        ...

    def remove_moves(self, predicate: Callable[[Move], bool]) -> int:
        """Remove all moves matching predicate. Returns count removed."""
        ...
