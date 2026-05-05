///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#ifndef slic3r_EngineGuard_hpp_
#define slic3r_EngineGuard_hpp_

// Compile-time firewall: libslic3r must not include GUI framework headers.
// This catches violations in headers that are transitively included via the PCH.
// Per-.cpp violations are caught by the CMake engine_boundary_check target.

#ifdef SLIC3R_ENGINE_ONLY

#ifdef _WX_DEFS_H_
#error "wxWidgets header included in libslic3r - engine must not depend on GUI frameworks"
#endif

#ifdef IMGUI_VERSION
#error "imgui header included in libslic3r - engine must not depend on GUI frameworks"
#endif

#endif // SLIC3R_ENGINE_ONLY

#endif // slic3r_EngineGuard_hpp_
