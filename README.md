<p align="center">
  <img src="resources/images/preFlight.png" alt="preFlight logo" width="600">
  <br><br>
  <a href="resources/images/gui.png"><img src="resources/images/gui.png" alt="preFlight interface" width="800"></a>
</p>

<p align="center">
  <a href="https://github.com/oozebot/preFlight/releases"><img src="https://img.shields.io/github/v/release/oozebot/preFlight?label=Latest%20Release" alt="Latest Release"></a>
  <a href="https://github.com/oozebot/preFlight/releases"><img src="https://img.shields.io/github/downloads/oozebot/preFlight/total?label=Downloads" alt="Downloads"></a>
  <a href="https://github.com/oozebot/preFlight/stargazers"><img src="https://img.shields.io/github/stars/oozebot/preFlight?style=flat&label=Stars" alt="GitHub Stars"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/oozebot/preFlight" alt="License"></a>
  <a href="https://donate.stripe.com/eVqfZbgoVf9y1c1aXe63K00"><img src="https://img.shields.io/badge/Donate-Support%20preFlight-blue?logo=stripe" alt="Donate"></a>
</p>

# preFlight

**The Engineer's Slicer**

preFlight is an advanced 3D printing slicer built for precision and performance. Building on the Slic3r legacy as a spiritual successor to PrusaSlicer, it offers exclusive features and a comprehensive under-the-hood overhaul, bringing the entire dependency stack up to modern standards. Given this massive modernization, preFlight has evolved beyond the constraints of the original codebase, making upstream merging irrelevant.

## oozeBot

Based in Georgia, USA, oozeBot is a small but ambitious team currently preparing for the take-off of our Elevate line of 3D printers. preFlight is the cornerstone of the ecosystem we are building - a genuinely new option in the 3D printing space designed to benefit all makers, regardless of the hardware they use.

## Discover preFlight
Want to see what makes preFlight special? Head over to our [Feature Showcase](https://github.com/oozebot/preFlight/discussions/categories/preflight-features) to view screenshots, learn more about unique features, and join the discussion.

## Donate

While preFlight is open-source and free for everyone, your support helps us maintain the infrastructure, fund R&D, and keep our team in orbit. If you find value in our tools, consider contributing to the mission.

[Support the preFlight Mission (via Stripe)](https://donate.stripe.com/eVqfZbgoVf9y1c1aXe63K00)

## Requirements

**Windows, Linux, macOS, and Raspberry Pi.**

**Windows:** Download the portable zip from [GitHub Releases](https://github.com/oozebot/preFlight/releases) and extract. Available for x64 and ARM64. Requires the [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) - install this first if preFlight won't launch.

**macOS:** Download the DMG from [GitHub Releases](https://github.com/oozebot/preFlight/releases). Requires macOS 11.0+ (Big Sur or later), Apple Silicon only. All builds are signed and notarized by Apple.

**Linux:** Download the AppImage from [GitHub Releases](https://github.com/oozebot/preFlight/releases), make it executable (`chmod +x`), and run. No installation required.

**Raspberry Pi:** RPi 5 running 64-bit Raspberry Pi OS (Bookworm or Trixie). Download the aarch64 .deb package from [GitHub Releases](https://github.com/oozebot/preFlight/releases).

## Security & Authenticity

To ensure the integrity of your installation and protect yourself, please follow these security guidelines:

* **Official Downloads:** Only download preFlight binaries directly from our [GitHub Releases](https://github.com/oozebot/preFlight/releases) page. We do not distribute preFlight through third-party mirror sites.
* **Verified Signature:** All official Windows binaries are digitally signed by **oozeBot, LLC** using an **Organization Validation (OV) Code Signing Certificate**. 
* **Verification:** Before running the installer, right-click the file, select **Properties**, and navigate to the **Digital Signatures** tab. Ensure the "Name of signer" is explicitly listed as **oozeBot, LLC**.
* **Safety First:** If you receive a "Windows protected your PC" (SmartScreen) warning on a file that is *not* signed by oozeBot, LLC, do not proceed with the installation and [report the issue](https://github.com/oozebot/preFlight/issues) immediately.
* **macOS Notarization:** All official macOS DMGs are signed with an **Apple Developer ID** certificate and notarized by Apple. macOS will verify the signature and notarization automatically on first launch. If Gatekeeper warns that the app is from an unidentified developer, the DMG is not an official release.
* **3MF Security:** Post-process scripts embedded in third-party 3MF files are suppressed on import (CVE-2023-47268). All 3MF extraction uses in-memory buffers, so preFlight is not affected by Zip Slip path traversal vulnerabilities.

## Why preFlight?

| What You Get | The Difference |
|--------------|----------------|
| **Athena Perimeter Generator** | Independent overlap control no other slicer offers |
| **Interlocking Perimeters** | Enhanced Z-bonding without added cost or complexity |
| **True 64-bit Architecture** | No coordinate overflow, no silent failures |
| **High Precision** | Clipper2 compiled with 10-decimal high precision |
| **In-Memory Processing** | No temp files, ~50% less RAM usage |
| **Modern Stack** | C++20, Clipper2, Boost 1.90, CGAL 6.1, OpenCASCADE 7.9, Eigen 5.0 |

---

## Flagship Features

### Athena Perimeter Generator

In Greek mythology, Athena defeated Arachne not through greater complexity, but through discipline and precision. We named our perimeter generator after her for the same reason.

**Why Athena Exists**

We forked Arachne to modernize it in several ways. Athena uses **fixed extrusion width** instead of variable and **independent overlap control** between perimeters. Arachne calculates overlap automatically. Athena lets you specify exactly how much perimeters overlap. It even enables negative overlap for creating gaps between perimeters.

#### Unique Controls

| Setting | What It Controls | Range |
|---------|------------------|-------|
| `Ext. perimeter/perimeter overlap` | Gap between external wall and first internal wall | -100% to +100% |
| `Perimeter/perimeter overlap` | Gap between all internal perimeters | -100% to +80% |
| `Perimeter compression` | How aggressively perimeters narrow in tight areas | |

- **Positive overlap**: Perimeters merge into each other (stronger bonding)
- **Zero overlap**: Perimeters just touch
- **Negative overlap**: Gap between perimeters (useful for flexible or soft materials)

#### Additional Characteristics

- Fixed extrusion widths with variation absorbed in spacing, not width
- Predictable wall shell thickness
- Full thin wall support
- Configurable thin wall snap grid (0.001 - 0.1mm) to control width oscillation on uniform thin walls

**When to Use Athena:** You need control over how perimeters bond, want consistent external perimeter width, or are tuning for strength/flex behavior.

**When to Use Arachne:** You prefer automatic overlap calculation or don't care about perimeter spacing.

### Interlocking Perimeters

A novel approach to layer bonding using **spacing variation and compression bonding** - fundamentally different from "brick layers".

**How it works:**
- Uses Athena's skeletal trapezoidation engine to generate interlocking shells - naturally handles narrow channels and bead count transitions
- Three bead width tiers that alternate between layers, keeping inter-shell gaps uniform
- Geometric centerline spacing for overlap bonding - no over-extrusion at shell boundaries
- All beads printed at **constant layer height** (no Z-axis manipulation)

**Key distinction from "brick layers":**
| Aspect | Brick Layers (others) | Interlocking (preFlight) |
|--------|----------------------|--------------------------|
| **Mechanism** | Height variation (Z-axis) | Spacing variation (X/Y axis) |
| **Bead heights** | Variable (half/full) | Constant |
| **Bonding type** | Geometric interlocking | Compression bonding |

**Benefits:**
- 5-15% strength increase (estimated)
- No material or time penalty at 100% strength
- Maintains dimensional accuracy (constant layer heights)

---

## Exclusive Features

### True 64-bit Architecture

**64-bit coordinate types throughout.**

```cpp
// PrusaSlicer/OrcaSlicer/SuperSlicer:
using coord_t = int32_t;  // Overflow risk

// preFlight:
using coord_t = int64_t;  // No overflow, native Clipper2
```

Why it matters:
- 32-bit coords overflow in cross products with large coordinates
- Clipper2 uses 64-bit internally - type mismatch causes bugs
- Large print volumes can exceed 32-bit range

### In-Memory G-code Processing

Zero temp files during slicing:
- No disk I/O during slicing
- ~50% less RAM (no per-line string overhead)
- Faster slicing (no file system operations)

### Multi-Type Support Painting

**Mixed support types on a single object.**

- Paint **Snug** (Blue) - Strong, close contact
- Paint **Grid** (Orange) - Balanced strength/removal
- Paint **Organic** (Green) - Easy removal, delicate areas

Strong supports under critical overhangs, easy-to-remove supports elsewhere. All on one print.

### Seam System

A comprehensive seam management system for hiding layer start/stop points.

**Nip and Tuck Seams:**

Seam shaping modes that push the external perimeter inward at the seam point, creating a V-shaped channel that absorbs start/stop blobs. The first inner perimeter is automatically trimmed to accommodate the disturbance.

| Mode | Behavior |
|------|----------|
| **Nip/Tuck** | Both start and end pushed inward - full V-notch |
| **Nip** | Only start pushed inward - conceals the start point |
| **Tuck** | Only end pushed inward - conceals the end point |
| **Alt. Nip/Tuck** | Alternates Nip and Tuck per layer - distributes disturbance across both sides |

Configurable notch width (1-3x extrusion width) and corner threshold angle. Automatically skipped at sharp corners where the geometry already hides the seam.

**Painted Seam Alignment:**

Bidirectional blending system for stable vertical and diagonal seam tracking on painted enforcer regions. Forward pass tracks diagonal seams while filtering vertex noise, backward pass straightens early-layer convergence lag.

**Paint-on Seams Line Drawing Mode:**
- Draw straight seam lines between points instead of painting freehand. Perfect for placing seams along edges or in straight grooves. Includes Z-axis snapping (within 5 degrees) for vertical lines.
- Minimum brush size reduced to 0.1mm

### 2-opt Travel Optimization

Intelligent perimeter ordering eliminates crossing travel paths:
- Centroid-based starting position for better initial grouping
- 2-opt optimization algorithm to eliminate crossing travel paths
- Adaptive iteration count scaling with group complexity

### Region-Aware Infill Ordering

Intelligent print ordering minimizes travel:
- Concentric fill uses depth-first traversal
- Sparse infill uses union-find clustering
- 30-50% reduction in travel distance for gyroid on multi-island layers

### RepRapFirmware Direct Integration

**Direct RRF integration for automatic machine limit configuration.**

One-click retrieval of all machine limits directly from Duet/RRF printers:
- M566 (Jerk), M201 (Acceleration), M203 (Feedrate)
- M204 (Print/travel accel), M207 (Firmware retract)

### Physics-Based Time Estimation

- Junction Deviation support (Marlin M205 J)
- RepRapFirmware acceleration model
- Print time estimates that actually match reality

### G-code Complexity Analysis

"Max cmd/s" column shows:
- Maximum commands per second for each extrusion role
- Which layer the maximum occurred on
- Identify bottlenecks before printing

### Enhanced G-code Viewer

Interactive G-code exploration for troubleshooting and analysis.

**Mouse Wheel Scrubbing:**
Hover over the G-code window and scroll to scrub through commands in real-time. The 3D preview updates as you scroll, letting you pinpoint exactly which command produces which movement. Right-click any G-code command to instantly copy to your clipboard.

**Additional Features:**
- Keyboard arrow navigation (command-by-command)
- Right-click to copy any command to clipboard
- Width matched to legend for clean UI

### Print Quality Enhancements

- **Top Surface Flow Reduction** - Reduce flow on top layers for smoother finish
- **Narrow to Athena** - Narrow solid surfaces use variable-width fill instead of rectilinear, eliminating zigzag and diamond artifacts at narrow transitions
- **Bridge Infill Overlap** - Independent control over overlap between bridge extrusions

### Support System Redesign

- **Always-Synced Support Layers** - Support layers perfectly align with object layers (no fractional heights)
- **Variable Interface Layer Heights** - Achieve desired gap through interface height adjustment, not Z-offset
- **Simplified Bottom Contact** - Clear options: No Gap, Half Layer, Full Layer (instead of arbitrary mm values)

### Cooling & Extrusion Control

- **Full Manual Fan Control** - Complete manual cooling control for each feature type
- **Fan Spin-Up Options** - Configure fan spin-up timing for precise cooling with overhang perimeters and bridge infill
- **Wipe Enhancements** - Improved wipe/retraction behavior

### OrcaSlicer Profile Import

Import printer, filament, and process profiles from `.orca_printer`, `.orca_filament`, and `.zip` bundles via File > Import > Import OrcaSlicer Bundle. Includes key mapping with value transforms, bed temperature plate selection, G-code macro translation, and a results dialog showing imported profiles, lossy mappings, dropped settings, and G-code warnings.

### Preview Clipping Plane

Right-click any object in sliced preview to activate an interactive cross-section plane that cuts through toolpaths and shell meshes. Useful for inspecting internal structure, verifying infill patterns, and diagnosing print issues before sending to the printer.

### Align to Face Gizmo

Precise object alignment using face selection. Pick a face on the build plate object, pick a face on the tool object, and snap them together. Includes flip, proportional scale/size, depth slider, position nudge, and Shift+drag snap-to-point with visual ring indicator. Boolean weld and subtract operations for combining or cutting objects in place.

### Relief Gizmo

Emboss images as 3D heightmaps onto mesh surfaces. Load any image and project it as a relief with smoothing, gamma correction, and minimum thickness controls. Real-time preview with CSG subtraction shown during the slicing shell animation.

### Counterbore Bridge Gizmo

Paint-on gizmo for controlling bridge layers above counterbore holes. Paint the regions that need bridging, set a per-hole bridge layer count, and the slicer generates bridge infill only within the painted area. Fill direction follows the corridor angle for each hole independently, and the transition extends upward through the specified bridge count.

### Customizable Sidebar

Two layout modes to suit your workflow:

- **Accordion** - Collapsible sections with inline settings editing. All groups visible in a single scrollable list.
- **Tabbed** - Traditional tabbed layout as an alternative. Toggle between modes via Preferences > GUI.

Per-option visibility checkboxes let you show or hide individual settings to keep the sidebar focused on what matters to you.

### Project Notes

Add notes to individual objects or the entire project. Notes are persisted in 3MF files with full undo/redo support.

### Additional Features

- **Layer 0 Preview** - See blank build plate before first extrusion
- **Preview Slider Accelerators** - Ctrl (2x), Shift (4x), Ctrl+Shift (8x) navigation
- **Settings Tab Auto-Commit** - Click dead space to commit fields
- **Progressive Slicing Feedback** - Visual feedback during slicing
- **Solid Fill Pattern Selection** - Choose pattern for internal solid layers

---

## Ported Features (with additional enhancements)

### Mouse Ear Brims
Ported from OrcaSlicer with enhancements:
- Per-ear overlap control (-100% to +100%)
- Always-visible rendering
- Full undo/redo support

### Paint-on Fuzzy Skin
Ported from OrcaSlicer with enhancements

**Noise Types:**
| Type | Effect |
|------|--------|
| **Classic** | Original random displacement |
| **Perlin** | Smooth, organic patterns |
| **Billow** | Cloud-like, puffy texture |
| **Ridged Multi** | Sharp, jagged features |
| **Voronoi** | Cell-based patchwork patterns |

**Advanced Controls:**
- **Visibility Detection** - Optional skip fuzzy on top-visible surfaces
- **Bottom Detection** - Optional skip fuzzy on bottom layers - always skip overhangs
- **Per-Perimeter Control** - Limit fuzzy to external perimeters only
- **Point placement** - Standard or Shape following
- **Fuzzy skin mode** - Displacement, Extrusion, Combined
- **Scale** - Feature size in mm
- **Octaves** - Detail levels (noise complexity)
- **Persistence** - How much each octave contributes

---

## Modern Infrastructure

### Core Libraries

| Library | Version | Notes |
|---------|---------|-------|
| **Clipper2** | 2.0.1 | Polygon clipping with native 64-bit support |
| **Boost** | 1.90.0 | Filesystem, logging, threading, geometry |
| **CGAL** | 6.1 | Computational geometry (Boost.Multiprecision backend) |
| **OpenCASCADE** | 7.9.3 | STEP/IGES CAD file import |
| **Eigen** | 5.0.0 | Linear algebra (vectors, matrices, transforms) |
| **TBB** | 2022.3.0 | Parallel task execution |
| **NLopt** | 2.10.0 | Nonlinear optimization |
| **GLAD** | 2.0.8 | OpenGL loader |

### Removed Legacy Dependencies
- **GMP/MPFR** - CGAL 6.x uses Boost.Multiprecision instead
- **OpenCSG** - Dead/unused code removed
- **GLEW** - Replaced with actively maintained GLAD

### Build Improvements
- C++20 compilation
- Memory leak fixes (gigabytes saved over long sessions)
- CMake 4.x ready

---

## Compatibility

preFlight works with any modern 3D printer accepting RepRap-flavored G-code:
- Marlin firmware
- Prusa firmware
- RepRapFirmware (Duet)
- Klipper
- Smoothieware

### Supported Formats
- **Input**: STL, OBJ, 3MF, AMF, STEP
- **Output**: G-code for FFF printers

---

## Building from Source

All platforms use a unified build system. Windows uses `.bat` wrappers that set up the MSVC environment, then delegate to the same underlying scripts.

### Windows

**Requires Visual Studio 2026** (VS 2022 is not supported)

```bash
# Build dependencies (first time only)
build_deps.bat

# Build release
build.bat

# Build debug (for development)
build.bat -debug
```

### Linux / macOS

```bash
# Prerequisites (macOS only)
brew install cmake ninja pkg-config

# Build dependencies (first time only)
./build_deps.sh

# Build release
./build.sh

# Build debug (for development)
./build.sh -debug
```

### Build Options

| Flag | Description |
|------|-------------|
| `-deps` | Build dependencies (alternative to running build_deps separately) |
| `-debug` | Build with debug symbols (RelWithDebInfo) |
| `-clean` | Remove build directory and rebuild from scratch |
| `-config` | Run CMake configure only, skip build |
| `-flush` | Force resource recompilation (Windows: icons, splash) |
| `-jobs N` | Number of parallel build jobs (default: auto-detect) |
| `-arch A` | Architecture override (macOS: `arm64` / `x86_64`) |

---

## Documentation

Coming soon.

---

## License

preFlight is licensed under the **GNU Affero General Public License, version 3**. See [LICENSE](LICENSE) for details.

preFlight is based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is based on [Slic3r](https://github.com/slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

---

## Support

- **Email:** [support@ooze.bot](mailto:support@ooze.bot)
- **GitHub Issues:** [github.com/oozebot/preFlight/issues](https://github.com/oozebot/preFlight/issues)

---

*preFlight - Precision where it matters.*
