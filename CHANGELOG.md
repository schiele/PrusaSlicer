# preFlight Changelog

## v0.9.11

### Preview / Legend
- Added Job Estimate section to G-code preview legend showing estimated print time and filament usage
- Persisted legend toggle button states across application restarts (seams, tool markers, etc)

### Align to Face Gizmo
- Fixed subtract operation not baking the CGAL boolean result into the mesh
- Fixed snap points invisible in orthographic camera mode

### Athena Perimeter Generation
- Fixed center bead split not producing correct source polygon tags
- Fixed Narrow to Athena not triggering on long narrow surfaces
- Fixed polyline stitcher rejecting valid connections across source polygons

### Infill / Fill
- Fixed monotonic fill line ordering scrambled by entity-level reorder
- Fixed infill overshoot beyond perimeters on certain geometries
- Fixed bridge anchor overlapping sparse infill and perimeters in certain geometry

### Cooling
- Fixed fan speed restoration after overhang/bridge regions

### Orca Import
- Fixed Orca import dropping acceleration values specified as percentages

### Bug Fixes
- Fixed painted brim ears not clipping against adjacent object instances
- Fixed negative volume visibility after Align gizmo operations
- Fixed black screen after drag-and-drop file load on all platforms

## v0.9.10

### Windows ARM
- Added Windows ARM64 native support for compatible hardware

### Counterbore Bridge Gizmo
- Added Counterbore Bridge paint-on gizmo for controlling bridge layers above counterbore holes
- Per-hole bridge layer count with smart fill that only bridges painted regions
- Fill direction follows corridor angle for each painted hole independently
- Transition starts at the painted layer and extends upward through the specified bridge count
- Activated through new gizmo menu item in the vertical toolbar

### Relief Gizmo
- Added Relief gizmo for embossing images as 3D heightmaps onto mesh surfaces
- Smoothing, gamma correction, and minimum thickness controls
- Real-time preview with CSG subtraction result shown in slicing shell animation
- Activated through right clicking on the platter

### Athena Perimeter Generation
- Allowed center bead split when wide enough for two beads
- Deduplicated overlapping center-pair perimeters in Athena toolpaths
- Fixed Athena center-pair overlap when perimeter overlap was active
- Fixed out-of-bounds in LimitedBeadingStrategy marker placement
- Enforced nozzle-based minimum bead width

### Interlocking Perimeters
- Interleaved interlocking with perimeters by structural feature ID for correct ordering
- Fixed interlocking inner contour missing corners, leaving infill voids
- Fixed interlocking shell spacing for even-layer boundary bead
- Fixed interlocking flow boundary detection to check both layers

### Overhang Perimeters
- Discarded extra overhang perimeters when oscillation was detected
- Fixed infinite loop in overhang perimeter generation

### Infill / Fill
- Merged stBottom with adjacent solid fill when bridge_no_gap was OFF
- Fixed bridge infill gaps when bridge_infill_overlap was above 0%
- Fixed crash in rectilinear fill when slicing dense relief meshes

### Seams
- Fixed seam notch taper creating tiny travel moves at element boundaries

### Security
- Suppressed post-process scripts in 3MF imports (CVE-2023-47268)
- Confirmed preFlight is not affected by the Zip Slip path traversal vulnerability - all 3MF extraction uses in-memory buffers

### G-code / Post-Processing
- Fixed G-code command window not displaying when binary G-code support was enabled
- Fixed post-processing scripts on exported G-code from virtual file
- Fixed several post-processing issues

### Orca Import
- Fixed Orca import silently destroying ConfigOptionStrings array values

### Platform / Build
- Added unified build script for all platforms
- Unified Windows deps build path to deps/build
- Renamed platform identifiers to win-amd64/linux-amd64
- Fixed deps build reliability on Windows

### UI / Bug Fixes
- Fixed bridge infill double extrusion along obstacle boundaries
- Fixed text emboss gizmo closing on keypress and restored font preview rendering
- Fixed negative volumes invisibility
- Fixed Slice Platter button getting stuck on Export G-code
- Fixed preset save dialog rejecting valid names with shared prefixes
- Guarded delete_preset against nonexistent preset names
- Rebuilt preset maps after deletion to prevent stale matches on re-import
- Suppressed all config validation dialogs during startup to prevent splash deadlock
- Renamed bridge_no_gap label for clarity

## v0.9.9

### Align to Face Gizmo
- Added Align to Face gizmo for precise object alignment using face selection, snap points, and boolean operations (weld and subtract)
  - Flip, scale/size with proportional lock, depth slider, position/nudge, Shift+drag snap-to-point with visual ring indicator

### Interlocking Perimeters
- Promoted interlocking perimeters to the true perimeter generator with full pipeline integration
- Coverage-walking visibility system with clipping and travel ordering
- Contour-ring feature ordering with seam joining
- Reduced interlocking flow to 100% on top layer
- New Solid layers above/below option to precisely start/stop interlocking inside the horizontal shells

### Painting / Splitting
- Preserved MMU painting, fuzzy skin, and seam data through Split to Parts and Split to Objects

### Bug Fixes / Improvements
- Fixed solid infill gaps between fill and innermost perimeters on bottom/top solid layers caused by progressive edge erosion during horizontal shell propagation
- Fixed solid infill overflowing into object through-holes when hole removal failed to recognize real object features vs trimming artifacts
- Fixed excessive solid infill consuming entire sparse regions on small objects at low infill densities - reduced absorption threshold from 16x to 4x line spacing squared
- Added erosion test to solid hole removal so thin projected features from geometry above are filled solid instead of left as unfillable sparse gaps
- Fixed large top/bottom surfaces being falsely classified as narrow, causing them to be skipped or have their fill pattern overridden
- Fixed greedy polyline chaining silently dropping disconnected segment clusters, which caused missing infill on some layers with Adaptive Cubic
- Fixed thin solid bridge anchor strips producing no fill by retrying with a smaller boundary offset
- Replaced Narrow to Concentric with Narrow to Athena - narrow solid surfaces now use variable-width fill instead of concentric, eliminating zigzag and diamond artifacts at narrow transitions
- Fixed small perimeter speed percentage resolving against internal perimeter speed for all perimeters - now correctly resolves against external perimeter speed for external perimeters
- Reordered speed settings to show Perimeters, External perimeters, then Small perimeters - clarifies that Small perimeters applies to both
- Fixed time-based legend showing false bands for near-identical layer times
- Fixed preview gap rendering when M207 Z retract lift equals layer height
- Fixed fill density field multiplying value by 100x when entered without a percent sign in the sidebar or loaded from configs without the % suffix
- Fixed preview clipping plane persisting during slicing shell animation after reslice
- Fixed slicing animation freezing when switching to another window during slice

## v0.9.8

### Interlocking Perimeters
- Replaced polygon-offset shell generation with Athena's skeletal trapezoidation engine for interlocking perimeters - naturally handles narrow channels and bead count transitions
- Redesigned interlocking bead geometry to use three flow-scaled tiers (100%, ~146%, 200%) - the ~146% boundary bead width is derived so outer edges align with the 100% boundary bead on alternate layers to keep inter-shell gaps uniform
- Replaced flow-rate overlap with geometric centerline spacing modification - overlap bonding now works the same way as perimeter-to-perimeter overlap, eliminating over-extrusion at shell boundaries

### Alternating Nip/Tuck Seams
- Added Alt. Nip/Tuck seam type - alternates between Nip on even layers and Tuck on odd layers to distribute seam disturbance across both sides of the junction

### Orca Import
- Added warning dialog before OrcaSlicer profile import informing users that imported profiles will need careful review due to differences between applications

### Print Host
- Fixed RRF standalone machine limits race condition where stale rr_reply queue entries caused values to be incorrectly applied to the wrong fields in stand-alone mode

### Preview/Legend
- Modified fan speed legend to always show 10 fixed bands (0-10%, 11-20%, ..., 91-100%) regardless of data distribution
- Capped all range-based legends to 10 bands maximum - values exceeding 10 distinct groups are merged via frequency-weighted quantiles

### Bug Fixes
- Fixed slicing animation not switching to Preview tab during slice
- Fixed accidental object deletion in Preview when pressing Delete/Backspace - objects are now deselected when slicing begins
- Fixed empty sidebar groups not hiding on GTK/macOS due to spacers being counted as visible rows (thanks topisani!)
- Accumulated partial scroll events on XWayland are no longer silently dropped (thanks topisani!)

### UI
- Added "View release page" hyperlink to the update available dialog


## v0.9.7

### Raspberry Pi
- Added RPi 5 support for 64-bit Raspberry Pi OS
- OpenGL 3.1 / GLSL 1.40 with flat shader fallback
- Now compatible with both Bookworm and Trixie

### macOS Support
- preFlight for macOS is now digitally signed and notarized

### New Features
- **Painted Seam Alignment**: Bidirectional blending system for stable vertical and diagonal seam tracking - forward pass tracks diagonal seams while filtering vertex noise, backward pass straightens early-layer convergence lag. Only activates for painted enforcers on smooth surfaces
- **Travel Optimization**: Replaced default extrusion ordering with nearest-neighbor chaining across the G-code pipeline - reduced unnecessary travel moves between islands, added 2-opt refinement for shorter travel paths, cross-fragment polyline chaining for connected infill lines
- **Nip and Tuck Seams**: Added two new seam types: Nip, and Tuck. Nip conceals the start point. Tuck conceals the end point.
- **Athena Thin Wall Width Precision**: Added user-configurable snap grid (0.001 - 0.1mm) under Print Settings > Advanced to control width oscillation on uniform thin walls
- **Legend-Specific Tooltips**: Legend-specific values now appear on G-code horizontal slider
- **3mf Warnings**: Added warning when opening 3mf files from other slicers about configuration differences

### Infill / Fill Improvements
- Absorbed small sparse infill gaps into adjacent solid fills - eliminated unfilled holes/gaps within solid infill on layers above bridges and internal solid floors
- Merged fragmented bridge infill regions into unified fills with correct bridge angle
- Fixed solid infill merge: boundary clipping, adjacency transfer, and hole safety to prevent overlap, flooding, and top-surface overwriting
- Fixed SOB/InternalSolid merge to use geometric adjacency instead of layer-wide thin heuristic, and corrected tiny-SOB removal threshold from sparse density to solid fill spacing
- Skipped bridge-over-infill for single-layer sparse gaps - prevented monotonic bridge pattern on isolated layers sandwiched between normal infill
- Optimized concentric fill: cluster spatially adjacent loops, rotate to nearest vertex

### G-code
- Eliminated redundant standalone `G1 F` lines
- Fixed manual fan controls producing no M106 for non-bridge features
- Stopped emitting machine envelope G-code for RRF/Rapid/Klipper firmware
- Fixed dynamic overhang speed bucket snapping with sane defaults

### Athena / Wall Generation
- Added Athena support for concentric infill when Athena is selected as the perimeter generator
- Fixed certain geometry by treating small polygons as thin walls
- Fixed thin-wall fragmentation under certain conditions

### Crash Fixes
- Fixed empty Preview after slicing caused by GL context loss during gcode loading
- Fixed Clipper2 stack overflow crash
- Fixed concentric fill hang
- Fixed int64 overflow in Clipper2 Z callback

### Bug Fixes
- Fixed painted mouse ears merging with overlapping merged ears
- Fixed painted mouse ears overlapping object under certain geometry
- Modified mouse ears to use Athena perimeter generator for better coverage
- Fixed preview rendering bug when retract_lift equals layer_height
- Auto-corrected Orca-format shrinkage compensation values on config load
- Fixed enforce_layers generating support when no auto or painted supports existed

### GPU / Rendering
- Rolled back over-zealous GPU power-saving event suppression that caused delayed context
- Scoped GPU power-saving to Preview tab only - Platter reverts to stock responsiveness

### UI Fixes/Changes
- Layer slider position remains on current layer after reslice
- Previous layers now darkened in G-code preview except during full render
- Fixed first mouse scroll over ImGui windows (e.g. G-code command legend) zooming the canvas instead of scrolling
- Fixed G-code command legend highlighted line not centered during scroll
- Fixed sidebar items not hiding when individually unticked
- Fixed object settings panel not expanding to fill available sidebar space
- Fixed macOS gizmo tooltips to show "Cmd" instead of "Ctrl"

### Packaging
- Fixed Linux build issues with CGAL GMP guard


## v0.9.6

### macOS Support (New)
- Added macOS 11.0+ support for Apple Silicon
- Dark mode and Retina display support
- ***preFlight for macOS is currently not digitally signed - this will be finalized soon and released in v0.9.7***

### New Features
- Allow Slice Platter from any tab (not just Prepare)
- Enabled background processing preference in settings so users can opt in to automatic slicing
- Added "Remember my choice" to upload overwrite dialog and "Reset Upload Preferences" button in Send G-Code dialog

### DPI / Multi-Monitor Fixes
- Fixed ImGui Legend sidebar rendering at wrong width after cross-monitor DPI change
- Fixed DPI scaling corruption when dragging window across monitors - full rescale now triggers on drag end
- Fixed sidebar preset combo box text vertical centering after DPI change

### GPU / Rendering
- Fixed GPU retention after interaction in Preview canvas

### Bug Fixes
- Fixed fan ramp segment split producing wrong E values in absolute E mode
- Fixed missing icons in Settings/Export dropdowns after DPI fix broke uncached bitmap items
- Fixed MsgDialog HTML content rendering with white background on Windows
- Fixed missing tree view icons in settings

### Linux
- Clipped popup menu background to rounded borders on GTK3


## v0.9.5

### Print Host Improvements
- Fixed host upload crash when sending large files to printer
- Added file overwrite protection - checks if the file already exists on the printer before uploading and prompts to overwrite or rename (Duet DSF/RRF, OctoPrint, LocalLink, Moonraker)
- Added post-upload prompt to switch to the Printer WebView tab (with "Remember my choice" option)
- Changed Duet connection order to try DSF before RRF - SBC-based printers no longer waste a failed RRF request on every connection

### Orca Import Improvements
- Resolved most `@System` filament inheritance - imported profiles now get correct values instead of falling back to defaults
- Added "Yes to All / No to All" buttons to overwrite and validation dialogs so large imports don't require clicking through every duplicate
- Auto-appended `[0]` index to vector variables during G-code placeholder translation to prevent post-import parsing errors
- Hardened import pipeline: per-profile error handling so one failure doesn't abort the batch, always show results dialog, reject empty/corrupt bundles with a clear message
- Added 27 new key mappings (acceleration, overhang speeds, bridge flow, line widths, infill anchors, resolution, wall distribution, and more) and registered 53 additional Orca-only keys so they are properly classified instead of falling through as unknown

### Preview/Legend Improvements
- Replaced linear color range with frequency-aware band system for the preview legend - outliers no longer compress useful data into a single color; bands are based on quantile splitting of actual value frequencies
- Enabled preview layer ruler by default

### Cooling
- Made "Enable manual fan speeds" and "Enable auto cooling" mutually exclusive to prevent auto cooling from overriding manual fan settings
- Updated cooling hint text to guide users toward manual controls

### Bug Fixes
- Fixed placeholder parsing inside G-code comments - variables after `;` no longer trigger parse errors
- Restored sidebar and allow reslice after a slicing error
- Fixed Stealth Mode column persisting in Machine Limits when toggled from sidebar
- Fixed stale lock icons when parent preset was temporarily null during load
- Fixed thin-walled geometry collapse in mesh slicer closing operation
- Fixed division by zero crash in rectilinear fill segment intersection
- Fixed interlocking perimeters missing on combined-infill void layers
- Fixed over-bridge speed having no effect on solid infill above bridges
- Fixed SSL certificate revocation check unavailable on Linux/macOS

### UI/Theme Improvements
- Replaced native scrollbars with custom themed scrollbars in all message dialogs for consistent dark mode appearance
- Improved text fields: tooltips dismiss upon typing and added right-click context menu (Undo/Cut/Copy/Paste/Delete)
- Settings description text now wraps dynamically to available panel width
- Eliminated expensive full-app rescale when dragging window between monitors - removes visible lag on multi-monitor setups
- Replaced fuzzy search in the settings search dialog with contiguous substring match for more predictable results
- Mouse wheel scrolling in multiline text areas now requires clicking inside the field first to prevent accidental changes

### Linux
- Fixed OCCTWrapper.so not found for STEP file import

## v0.9.4

### New Features
- **OrcaSlicer Bundle Import**: Import printer, filament, and process profiles from `.orca_printer`, `.orca_filament`, and `.zip` bundles via File > Import > Import OrcaSlicer Bundle — includes key mapping with value transforms, bed temperature plate selection, G-code macro translation, and a results dialog showing imported profiles, lossy mappings, dropped settings, and G-code warnings

### Bug Fixes
- Fix Nip/Tuck only processing the first external perimeter per island — now handles multiple external perimeters correctly
- Improve seam vertical alignment by increasing snap tolerance to eliminate zigzag drift from polygon vertex discretization
- Skip staggered seam on outermost inner perimeter when Nip/Tuck is enabled to keep the trimmed gap aligned with the V-notch
- Fix Printer Settings sections (Capabilities, Machine Limits, RRF M-codes) not hiding when unchecked in sidebar visibility toggles
- Fix native scrollbar bleed-through in multiline TextInput fields
- **Camera View Shortcuts**: Number key view shortcuts (1–6) now recenter on the build plate

### Linux
- Fix AppImage WebKit crash on Arch and non-Debian distros caused by patching order leaving hardcoded `/usr` paths in library copies
- Add EGL probe to prevent WebKit crash on VMs without working GPU — falls back to system browser when EGL initialization fails

## v0.9.3

### New Features
- **Nip/Tuck Seams**: V-notch on external perimeters to hide seams
- **Seam Vertical Alignment**: Stable reference-position tracking prevents seam drift between layers; painted enforcer regions auto-center the seam at the enforcer centroid
- **Preview Clipping Plane**: Right-click any object in sliced preview to activate an interactive cross-section plane that cuts through toolpaths and shell meshes for analysis
- **Tabbed Sidebar Layout**: Tabbed sidebar as an alternative to the accordion layout, toggled via Preferences > GUI
- **Search Settings**: New search dialog with dedicated button in tab bar

### Bug Fixes
- Fix printer host type dropdown using fragile index offsets, replaced with explicit enum mapping
- Add defensive HWND validity checks in dark mode title bar and explorer theme calls

### Linux
- Fix blank Object Manipulation panel and Info panel overlap on GTK3
- Fix Wayland negative-width assertion in sidebar custom controls
- Update install paths and desktop file branding for preFlight
- AppImage now uses pre-split libraries with system GPU drivers on modern distros and bundled fallback on older systems

## v0.9.2

### New Features
- **Linux Support**: preFlight now runs natively on Linux with the full preFlight experience and single-file AppImage packaging — download and run, no install needed
- **Responsive Tab Bar**: Settings buttons auto-collapse into a single "Settings" dropdown when the tab bar is narrow (e.g., long printer name, small window)
- **Continuous Scrollable Sidebar**: Flattened sub-tabs into a single scrollable list where all setting groups are visible simultaneously

### UI/Theme Improvements
- Smoother window dragging on high-DPI displays by pausing GL canvas rendering during drag
- Custom themed menus and tab bar on Linux (GTK3), matching the Windows experience

### Bug Fixes
- Fix use-after-free crash in sidebar dead-space click handler binding
- Fix standalone RRF (Duet) machine limits retrieval for non-SBC boards

### Known Limitations
- Linux build supports dark mode only — light mode is not yet available

## v0.9.1

### New Features
- **Printer Interface Tab**: Embedded webview showing printer's web interface with real-time connection status indicator
- **Project Notes**: Add notes to individual objects or entire project, persisted in 3MF files with undo/redo support
- **Custom Menu System**: Fully themed popup menus and menu bar
- **Accordion-Style Sidebar**: Collapsible sections with inline settings editing
- **Sidebar Visibility**: Per-option visibility checkboxes to customize sidebar
- **DPI Aware Improvements**: DPI aware improvements to all areas within the application

### UI/Theme Improvements
- Centralized UIColors system for consistent theming
- Midnight dark theme with cool blue-gray palette
- Windows 11 custom title bar colors
- Theme-aware bed/canvas, ImGui, ruler, legend, and sliders

### Bug Fixes
- Fix crash in monotonic region chaining when ant hits dependency dead-end
- Fix monotonic infill lines escaping boundary on complex multi-hole polygons
- Fix Voronoi "source index out of range" crash during slicing
- Fix physical printer selection not persisting across app restarts
- Fix brim settings crash and brim infinite loop
- Fix submenu items not responding to clicks in custom menus
- Fix config wizard broken on fresh installs
- Fix Athena thin wall width precision errors
- Fix mouse wheel gcode navigation on layer 0
- Fix double-delete crash in View3D/Preview destructors
- Disable mouse wheel on spin/combo inputs to prevent accidental changes

## v0.9.0

Initial release of preFlight, based on PrusaSlicer.
