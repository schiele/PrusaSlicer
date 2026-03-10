///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) Prusa Research 2016 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2016 Sakari Kapanen @Flannelhead
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2013 Mark Hindess
///|/ Copyright (c) 2011 Michael Moon
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/
#include <oneapi/tbb/scalable_allocator.h>
#include <boost/container/vector.hpp>
#include <memory>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>
#include <functional>
#include <cassert>
#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>

#include "../ClipperUtils.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"

#include "../Print.hpp"
#include "../PreciseWalls.hpp"
#include "../PrintConfig.hpp"
#include "../Surface.hpp"
// for Arachne based infills
#include "../PerimeterGenerator.hpp"
#include "../Athena/WallToolPaths.hpp"
#include "../Athena/utils/ExtrusionLine.hpp"
#include "../Athena/PerimeterOrder.hpp"
#include "FillBase.hpp"
#include "FillRectilinear.hpp"
#include "FillLightning.hpp"
#include "FillEnsuring.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/ShortestPath.hpp"

namespace Slic3r
{
namespace FillAdaptive
{
struct Octree;
} // namespace FillAdaptive
namespace FillLightning
{
class Generator;
} // namespace FillLightning

//static constexpr const float NarrowInfillAreaThresholdMM = 3.f;

struct SurfaceFillParams
{
    // Zero based extruder ID.
    unsigned int extruder = 0;
    // Infill pattern, adjusted for the density etc.
    InfillPattern pattern = InfillPattern(0);

    // FillBase
    // in unscaled coordinates
    coordf_t spacing = 0.;
    // infill / perimeter overlap, in unscaled coordinates
    coordf_t overlap = 0.;
    // Angle as provided by the region config, in radians.
    float angle = 0.f;
    // Is bridging used for this fill? Bridging parameters may be used even if this->flow.bridge() is not set.
    bool bridge;
    // Non-negative for a bridge.
    float bridge_angle = 0.f;

    // FillParams
    float density = 0.f;
    // Don't adjust spacing to fill the space evenly.
    //    bool        	dont_adjust = false;
    // Length of the infill anchor along the perimeter line.
    // 1000mm is roughly the maximum length line that fits into a 32bit coord_t.
    float anchor_length = 1000.f;
    float anchor_length_max = 1000.f;

    // width, height of extrusion, nozzle diameter, is bridge
    // For the output, for fill generator.
    Flow flow;

    // For the output
    ExtrusionRole extrusion_role{ExtrusionRole::None};

    // Various print settings?

    // Index of this entry in a linear vector.
    size_t idx = 0;

    bool operator<(const SurfaceFillParams &rhs) const
    {
#define RETURN_COMPARE_NON_EQUAL(KEY) \
    if (this->KEY < rhs.KEY)          \
        return true;                  \
    if (this->KEY > rhs.KEY)          \
        return false;
#define RETURN_COMPARE_NON_EQUAL_TYPED(TYPE, KEY) \
    if (TYPE(this->KEY) < TYPE(rhs.KEY))          \
        return true;                              \
    if (TYPE(this->KEY) > TYPE(rhs.KEY))          \
        return false;

        // Sort first by decreasing bridging angle, so that the bridges are processed with priority when trimming one layer by the other.
        if (this->bridge_angle > rhs.bridge_angle)
            return true;
        if (this->bridge_angle < rhs.bridge_angle)
            return false;

        // TopSolidInfill must be processed first so it claims its area, then all other surfaces
        // (SolidInfill, sparse infill, etc.) get trimmed to avoid overlap.
        if (this->extrusion_role == ExtrusionRole::TopSolidInfill &&
            rhs.extrusion_role != ExtrusionRole::TopSolidInfill)
            return true; // TopSolidInfill goes first before everything
        if (this->extrusion_role != ExtrusionRole::TopSolidInfill &&
            rhs.extrusion_role == ExtrusionRole::TopSolidInfill)
            return false; // Everything else goes after TopSolidInfill

        RETURN_COMPARE_NON_EQUAL(extruder);
        RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, pattern);
        RETURN_COMPARE_NON_EQUAL(spacing);
        RETURN_COMPARE_NON_EQUAL(overlap);
        RETURN_COMPARE_NON_EQUAL(angle);
        RETURN_COMPARE_NON_EQUAL(density);
        //		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, dont_adjust);
        RETURN_COMPARE_NON_EQUAL(anchor_length);
        RETURN_COMPARE_NON_EQUAL(anchor_length_max);
        RETURN_COMPARE_NON_EQUAL(flow.width());
        RETURN_COMPARE_NON_EQUAL(flow.height());
        RETURN_COMPARE_NON_EQUAL(flow.nozzle_diameter());
        RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, bridge);
        return this->extrusion_role.lower(rhs.extrusion_role);
    }

    bool operator==(const SurfaceFillParams &rhs) const
    {
        return this->extruder == rhs.extruder && this->pattern == rhs.pattern && this->spacing == rhs.spacing &&
               this->overlap == rhs.overlap && this->angle == rhs.angle && this->bridge == rhs.bridge &&
               //				this->bridge_angle 		== rhs.bridge_angle		&&
               this->density == rhs.density &&
               //				this->dont_adjust   	== rhs.dont_adjust 		&&
               this->anchor_length == rhs.anchor_length && this->anchor_length_max == rhs.anchor_length_max &&
               this->flow == rhs.flow && this->extrusion_role == rhs.extrusion_role;
    }
};

struct SurfaceFill
{
    SurfaceFill(const SurfaceFillParams &params) : region_id(size_t(-1)), surface(stCount, ExPolygon()), params(params)
    {
    }

    size_t region_id;
    Surface surface;
    ExPolygons expolygons;
    SurfaceFillParams params;
};

static inline bool fill_type_monotonic(InfillPattern pattern)
{
    return pattern == ipMonotonic || pattern == ipMonotonicLines;
}

// ===================== FILL DEBUG HELPERS =====================
// Set to true to enable fill pipeline debug output to stdout.
static constexpr bool FILL_DEBUG = false;

static const char *dbg_stype(SurfaceType t)
{
    switch (t)
    {
    case stTop:
        return "stTop";
    case stBottom:
        return "stBottom";
    case stBottomBridge:
        return "stBottomBridge";
    case stInternal:
        return "stInternal";
    case stInternalSolid:
        return "stInternalSolid";
    case stInternalBridge:
        return "stInternalBridge";
    case stInternalVoid:
        return "stInternalVoid";
    case stPerimeter:
        return "stPerimeter";
    case stSolidOverBridge:
        return "stSolidOverBridge";
    case stCount:
        return "stCount";
    default:
        return "UNKNOWN";
    }
}

static void dbg_fill_print(const char *fmt, ...)
{
    if (!FILL_DEBUG)
        return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

static void dbg_fill_input(const Layer &layer)
{
    if (!FILL_DEBUG)
        return;
    double z = layer.print_z;
    int lid = (int) layer.id();
    dbg_fill_print("z=%.3f [FILL] ========== INPUT SURFACES (layer %d, height=%.3f) ==========\n", z, lid,
                   layer.height);
    for (size_t region_id = 0; region_id < layer.regions().size(); ++region_id)
    {
        const LayerRegion &layerm = *layer.regions()[region_id];
        int idx = 0;
        double region_total = 0;
        for (const Surface &surface : layerm.fill_surfaces())
        {
            double a = std::abs(surface.expolygon.area()) * 1e-12;
            region_total += a;
            BoundingBox bb = get_extents(surface.expolygon);
            dbg_fill_print("z=%.3f [FILL] INPUT r=%zu i=%d type=%-18s area=%8.4fmm2 holes=%zu pts=%zu "
                           "bbox=(%.2f,%.2f)-(%.2f,%.2f) bridge_ang=%.1f\n",
                           z, region_id, idx, dbg_stype(surface.surface_type), a, surface.expolygon.holes.size(),
                           surface.expolygon.contour.points.size(), unscaled<double>(bb.min.x()),
                           unscaled<double>(bb.min.y()), unscaled<double>(bb.max.x()), unscaled<double>(bb.max.y()),
                           surface.bridge_angle);
            idx++;
        }
        dbg_fill_print("z=%.3f [FILL] INPUT r=%zu TOTAL: %d surfaces, %.4fmm2\n", z, region_id, idx, region_total);
    }
}

static void dbg_fill_phase(const char *phase, const Layer &layer, const std::vector<SurfaceFill> &fills)
{
    if (!FILL_DEBUG)
        return;
    double z = layer.print_z;
    int lid = (int) layer.id();
    int total_ep = 0;
    double total_area = 0;
    dbg_fill_print("z=%.3f [FILL] ========== %s (layer %d) ==========\n", z, phase, lid);
    for (size_t i = 0; i < fills.size(); i++)
    {
        const SurfaceFill &sf = fills[i];
        if (sf.expolygons.empty())
            continue;
        double sf_area = 0;
        for (const ExPolygon &ep : sf.expolygons)
            sf_area += std::abs(ep.area());
        double sf_area_mm2 = sf_area * 1e-12;
        total_area += sf_area_mm2;
        total_ep += (int) sf.expolygons.size();
        BoundingBox bb = get_extents(sf.expolygons);
        dbg_fill_print("z=%.3f [FILL] %s [%zu] type=%-18s r=%zu dens=%.1f%% ep=%zu area=%8.4fmm2 "
                       "bbox=(%.2f,%.2f)-(%.2f,%.2f)\n",
                       z, phase, i, dbg_stype(sf.surface.surface_type), sf.region_id, sf.params.density,
                       sf.expolygons.size(), sf_area_mm2, unscaled<double>(bb.min.x()), unscaled<double>(bb.min.y()),
                       unscaled<double>(bb.max.x()), unscaled<double>(bb.max.y()));
        for (size_t j = 0; j < sf.expolygons.size(); j++)
        {
            const ExPolygon &ep = sf.expolygons[j];
            double ep_area = std::abs(ep.area()) * 1e-12;
            BoundingBox epbb = get_extents(ep);
            dbg_fill_print("z=%.3f [FILL]   %s [%zu][%zu] area=%8.4fmm2 holes=%zu pts=%zu "
                           "bbox=(%.2f,%.2f)-(%.2f,%.2f)\n",
                           z, phase, i, j, ep_area, ep.holes.size(), ep.contour.points.size(),
                           unscaled<double>(epbb.min.x()), unscaled<double>(epbb.min.y()),
                           unscaled<double>(epbb.max.x()), unscaled<double>(epbb.max.y()));
        }
    }
    dbg_fill_print("z=%.3f [FILL] %s TOTAL: %d expolygons, %.4fmm2\n", z, phase, total_ep, total_area);
}
// ===================== END FILL DEBUG HELPERS =====================

std::vector<SurfaceFill> group_fills(const Layer &layer)
{
    std::vector<SurfaceFill> surface_fills;

    dbg_fill_input(layer);

    // First pass: Check if merge is enabled in config and collect top solid surface polygons per region.
    // We always collect top solid polygons (needed for both merge and concentric fallback).
    bool config_allows_merge = false;
    std::vector<Polygons> region_top_solid_polygons(layer.regions().size());

    for (size_t region_id = 0; region_id < layer.regions().size(); ++region_id)
    {
        const LayerRegion &layerm = *layer.regions()[region_id];

        // Check config (only need to check once, assume all regions have same setting)
        if (region_id == 0)
            config_allows_merge = layerm.region().config().merge_top_solid_infills;

        // Always collect top solid surface polygons for this region
        // (needed for merge check and for concentric fallback when merge is disabled)
        for (const Surface &surface : layerm.fill_surfaces())
        {
            if (surface.is_top())
            {
                Polygons polys = to_polygons(surface.expolygon);
                append(region_top_solid_polygons[region_id], polys);
            }
        }
    }

    // Check if an internal solid surface is spatially adjacent to (touching/overlapping) any top solid surface
    // in the same region. Returns true only when merge is enabled.
    auto is_surface_adjacent_to_top_solid = [&](const Surface &surface, size_t region_id) -> bool
    {
        if (!config_allows_merge || surface.surface_type != stInternalSolid)
            return false;

        const Polygons &top_solid_polys = region_top_solid_polygons[region_id];
        if (top_solid_polys.empty())
            return false;

        // Check if this internal solid surface intersects with any top solid surface
        Polygons internal_polys = to_polygons(surface.expolygon);
        Polygons intersection_result = intersection(internal_polys, top_solid_polys);
        return !intersection_result.empty();
    };

    // Used to determine if internal solid should use concentric pattern when merge is disabled
    auto is_internal_solid_touching_top = [&](const Surface &surface, size_t region_id) -> bool
    {
        if (surface.surface_type != stInternalSolid)
            return false;

        const Polygons &top_solid_polys = region_top_solid_polygons[region_id];
        if (top_solid_polys.empty())
            return false;

        Polygons internal_polys = to_polygons(surface.expolygon);
        Polygons intersection_result = intersection(internal_polys, top_solid_polys);
        return !intersection_result.empty();
    };

    // Check if a solid surface is too narrow for good rectilinear fill.
    // Uses medial axis to find the MAXIMUM width anywhere in the surface.
    // If max width < threshold, the surface is considered narrow.
    auto is_surface_narrow = [](const Surface &surface, const Flow &flow, float threshold_multiplier) -> bool
    {
        if (!surface.is_solid())
            return false;

        // Calculate the threshold width in scaled coordinates
        const coordf_t threshold_width = flow.scaled_width() * threshold_multiplier;

        // Use a small min_width and very large max_width to get the full medial axis
        const double min_width = flow.scaled_width() * 0.5; // Half extrusion width minimum
        const double max_width = 1e10;                      // Very large to capture all skeleton edges

        ThickPolylines polylines;
        surface.expolygon.medial_axis(min_width, max_width, &polylines);

        // If no medial axis, fall back to bounding box check.
        // Medial axis can fail for simple rectangular shapes due to Voronoi edge filtering.
        // Don't skip large surfaces just because medial axis computation failed.
        if (polylines.empty())
        {
            // Use bounding box minimum dimension as a simpler narrowness check
            const BoundingBox bbox = get_extents(surface.expolygon);
            const coordf_t min_dim = std::min(bbox.size().x(), bbox.size().y());
            // If the minimum dimension is larger than the threshold, it's not narrow
            return min_dim < threshold_width;
        }

        // Find the maximum width anywhere in the medial axis
        coordf_t max_found_width = 0;
        for (const ThickPolyline &tp : polylines)
        {
            for (coordf_t w : tp.width)
            {
                if (w > max_found_width)
                    max_found_width = w;
            }
        }

        // If the maximum width is less than threshold, the surface is narrow
        return max_found_width < threshold_width;
    };

    // Fill in a map of a region & surface to SurfaceFillParams.
    std::set<SurfaceFillParams> set_surface_params;
    std::vector<std::vector<const SurfaceFillParams *>> region_to_surface_params(
        layer.regions().size(), std::vector<const SurfaceFillParams *>());
    SurfaceFillParams params;
    bool has_internal_voids = false;
    for (size_t region_id = 0; region_id < layer.regions().size(); ++region_id)
    {
        const LayerRegion &layerm = *layer.regions()[region_id];
        region_to_surface_params[region_id].assign(layerm.fill_surfaces().size(), nullptr);
        for (const Surface &surface : layerm.fill_surfaces())
            if (surface.surface_type == stInternalVoid)
                has_internal_voids = true;
            else
            {
                const PrintRegionConfig &region_config = layerm.region().config();
                FlowRole extrusion_role = surface.is_top() ? frTopSolidInfill
                                                           : (surface.is_solid() ? frSolidInfill : frInfill);
                bool is_bridge = layer.id() > 0 && surface.is_bridge();
                params.extruder = layerm.region().extruder(extrusion_role);
                params.pattern = region_config.fill_pattern.value;
                params.density = float(region_config.fill_density);

                if (surface.is_solid())
                {
                    params.density = 100.f;
                    //FIXME for non-thick bridges, shall we allow a bottom surface pattern?
                    // Use top fill pattern for actual top surfaces and internal solid surfaces that
                    // are spatially adjacent to (touching) top solid surfaces
                    if (is_bridge)
                    {
                        params.pattern = ipMonotonic;
                    }
                    else if (surface.is_top() || is_surface_adjacent_to_top_solid(surface, region_id))
                    {
                        // Top surface, or internal solid adjacent to top when merge is enabled
                        params.pattern = region_config.top_fill_pattern.value;
                    }
                    else if (!config_allows_merge && is_internal_solid_touching_top(surface, region_id))
                    {
                        // When merge is disabled but internal solid is touching top solid,
                        // use concentric pattern for visual distinction
                        params.pattern = ipConcentric;
                    }
                    else if (surface.is_external())
                    {
                        // External bottom surface
                        params.pattern = surface.is_top() ? region_config.top_fill_pattern.value
                                                          : region_config.bottom_fill_pattern.value;
                    }
                    else
                    {
                        // Internal solid: use user-selected solid fill pattern
                        params.pattern = region_config.solid_fill_pattern.value;
                    }

                    // When Athena perimeters converge, a narrow sliver may be created that should be covered
                    // by perimeters but gets classified as a fill surface. Skip these very narrow surfaces
                    // for stTop and stBottom only (internal solid may legitimately need filling).
                    // Threshold: 1.5× extrusion width - if narrower, perimeters should cover it.
                    // This always applies regardless of narrow_solid_infill_concentric setting.
                    if (surface.surface_type == stTop || surface.surface_type == stBottom)
                    {
                        const Flow solid_flow = layerm.flow(frSolidInfill);
                        const float skip_threshold = 1.5f; // Very narrow - skip entirely

                        if (is_surface_narrow(surface, solid_flow, skip_threshold))
                        {
                            double a = std::abs(surface.expolygon.area()) * 1e-12;
                            dbg_fill_print("z=%.3f [FILL] SKIP_NARROW type=%-18s area=%8.4fmm2\n", layer.print_z,
                                           dbg_stype(surface.surface_type), a);
                            continue; // Skip this surface entirely - too narrow to fill
                        }
                    }

                    // After pattern is selected, check if this is a narrow solid surface
                    // that should use concentric instead of the configured pattern.
                    // Applies to stInternalSolid, stTop, and stBottom - does NOT affect bridge infill.
                    if ((surface.surface_type == stInternalSolid || surface.surface_type == stTop ||
                         surface.surface_type == stBottom) &&
                        params.pattern != ipConcentric && region_config.narrow_solid_infill_concentric.value)
                    {
                        const Flow solid_flow = layerm.flow(frSolidInfill);
                        const float threshold = float(region_config.narrow_solid_infill_threshold.value);

                        if (is_surface_narrow(surface, solid_flow, threshold))
                        {
                            params.pattern = ipConcentric;
                        }
                    }
                }
                else if (params.density <= 0)
                {
                    // Even at 0% infill density, we need stInternal surfaces to define the sparse boundaries
                    // where interlocking perimeters should be generated. Check if this region has interlocking enabled.
                    const bool has_interlocking = region_config.interlock_perimeters_enabled &&
                                                  region_config.interlock_perimeter_count > 0;

                    // Only process stInternal surfaces at 0% density if interlocking is active
                    if (!(has_interlocking && surface.surface_type == stInternal))
                    {
                        continue;
                    }
                    // If we reach here: interlocking is enabled, surface is stInternal, density is 0%
                    // Allow it through so interlocking code can find the sparse regions
                    // Set a tiny non-zero density to avoid division-by-zero in downstream calculations
                    // This surface will be removed after interlocking processing, so the tiny value won't affect output
                    params.density = 0.001f; // Effectively zero, but prevents div-by-zero
                }

                if (is_bridge)
                {
                    params.extrusion_role = ExtrusionRole::BridgeInfill;
                }
                else
                {
                    if (surface.is_solid())
                    {
                        if (surface.is_top())
                        {
                            params.extrusion_role = ExtrusionRole::TopSolidInfill;
                        }
                        else if (surface.surface_type == stSolidOverBridge)
                        {
                            params.extrusion_role = ExtrusionRole::InfillOverBridge;
                        }
                        else
                        {
                            // Only use TopSolidInfill role for internal solid surfaces that are
                            // spatially adjacent to (touching) actual top solid surfaces
                            if (is_surface_adjacent_to_top_solid(surface, region_id))
                            {
                                params.extrusion_role = ExtrusionRole::TopSolidInfill;
                            }
                            else
                            {
                                params.extrusion_role = ExtrusionRole::SolidInfill;
                            }
                        }
                    }
                    else
                    {
                        params.extrusion_role = ExtrusionRole::InternalInfill;
                    }
                }
                params.bridge_angle = float(surface.bridge_angle);
                params.angle = float(Geometry::deg2rad(region_config.fill_angle.value));
                // Angle alternation for all surfaces (including solid) is handled by
                // _layer_angle() in FillBase.cpp during fill generation. No manipulation here.

                // Calculate the actual flow we'll be using for this infill.
                params.bridge = is_bridge || Fill::use_bridge_flow(params.pattern);
                params.flow = params.bridge ?
                                            // Always enable thick bridges for internal bridges.
                                  layerm.bridging_flow(extrusion_role, surface.is_bridge() && !surface.is_external())
                                            : layerm.flow(extrusion_role,
                                                          (surface.thickness == -1) ? layer.height : surface.thickness);

                // Calculate flow spacing for infill pattern generation.
                // Treat near-solid density (>= 99.9999%) like solid for spacing purposes to avoid
                // underextrusion when fill surface thickness differs from layer height.
                if (surface.is_solid() || is_bridge || params.density >= 99.9999f)
                {
                    if (is_bridge)
                    {
                        float bridge_diameter = params.flow.width(); // For bridges, width == height == diameter

                        // Line-to-line spacing (bridge_infill_overlap setting)
                        float line_overlap_percent;
                        if (region_config.bridge_infill_overlap.percent)
                        {
                            line_overlap_percent = float(region_config.bridge_infill_overlap.value);
                        }
                        else
                        {
                            line_overlap_percent = float(region_config.bridge_infill_overlap.value) / bridge_diameter *
                                                   100.0f;
                        }
                        line_overlap_percent = std::clamp(line_overlap_percent, -100.0f, 80.0f);
                        params.spacing = bridge_diameter * (1.0f - line_overlap_percent / 100.0f);
                    }
                    else
                    {
                        params.spacing = params.flow.spacing();
                    }
                    // Only apply overlap and anchor settings for actual solid/bridge, not high-density sparse
                    if (surface.is_solid() || is_bridge)
                    {
                        // Overlap = 0 because bridge surface geometry is already adjusted in LayerRegion.cpp
                        // by expand_bridges_for_overlap() which runs AFTER the merge logic completes.
                        // This ensures: merge first, then expand for overlap on final geometry.
                        params.overlap = 0.0f;
                        // Don't limit anchor length for solid or bridging infill.
                        params.anchor_length = 1000.f;
                        params.anchor_length_max = 1000.f;
                    }
                }
                else
                {
                    // Internal infill. Calculating infill line spacing independent of the current layer height and 1st layer status,
                    // so that internall infill will be aligned over all layers of the current region.
                    params.spacing = layerm.region()
                                         .flow(*layer.object(), frInfill, layer.object()->config().layer_height, false)
                                         .spacing();
                    // When fill surface thickness differs from layer height, rescale width to maintain
                    // requested density with the rounded rectangle extrusion model.
                    params.flow = params.flow.with_spacing(params.spacing);

                    // When interlocking perimeters are enabled, infill anchors create overlaps and conflicts.
                    // Interlocking perimeters provide their own bonding to real perimeters via P/P overlap.
                    // Override anchor_length to 0 when interlocking is active (user's settings preserved in UI).
                    // IMPORTANT: We only set anchor_length to 0, NOT anchor_length_max. Setting anchor_length_max
                    // to 0 causes dont_connect() to return true, which disables zigzag patterns entirely.
                    // We want to disable perimeter anchoring but still allow infill-to-infill zigzag connections.
                    const bool has_interlocking = region_config.interlock_perimeters_enabled &&
                                                  layerm.num_interlocking_shells() > 0;

                    // Anchor a sparse infill to inner perimeters with the following anchor length:
                    params.anchor_length = has_interlocking ? 0.0f : float(region_config.infill_anchor);
                    if (!has_interlocking && region_config.infill_anchor.percent)
                        params.anchor_length = float(params.anchor_length * 0.01 * params.spacing);
                    params.anchor_length_max = float(region_config.infill_anchor_max);
                    if (region_config.infill_anchor_max.percent)
                        params.anchor_length_max = float(params.anchor_length_max * 0.01 * params.spacing);
                    params.anchor_length = std::min(params.anchor_length, params.anchor_length_max);
                }

                auto it_params = set_surface_params.find(params);
                if (it_params == set_surface_params.end())
                    it_params = set_surface_params.insert(it_params, params);
                region_to_surface_params[region_id][&surface - &layerm.fill_surfaces().surfaces.front()] = &(
                    *it_params);
            }
    }

    surface_fills.reserve(set_surface_params.size());
    for (const SurfaceFillParams &params : set_surface_params)
    {
        const_cast<SurfaceFillParams &>(params).idx = surface_fills.size();
        surface_fills.emplace_back(params);
    }

    for (size_t region_id = 0; region_id < layer.regions().size(); ++region_id)
    {
        const LayerRegion &layerm = *layer.regions()[region_id];
        for (const Surface &surface : layerm.fill_surfaces())
            if (surface.surface_type != stInternalVoid)
            {
                const SurfaceFillParams *params =
                    region_to_surface_params[region_id][&surface - &layerm.fill_surfaces().surfaces.front()];
                if (params != nullptr)
                {
                    SurfaceFill &fill = surface_fills[params->idx];

                    if (fill.region_id == size_t(-1))
                    {
                        fill.region_id = region_id;
                        fill.surface = surface;
                        fill.expolygons.emplace_back(std::move(fill.surface.expolygon));
                    }
                    else
                        fill.expolygons.emplace_back(surface.expolygon);
                }
            }
    }

    dbg_fill_phase("GROUPED", layer, surface_fills);

    {
        Polygons all_polygons;
        // preFlight: Track TopSolidInfill polygons separately so we can apply
        // clearance only between TopSolid and SolidInfill (not between bridge and solid).
        Polygons top_solid_polygons;
        for (SurfaceFill &fill : surface_fills)
            if (!fill.expolygons.empty())
            {
                if (fill.expolygons.size() > 1 || !all_polygons.empty())
                {
                    Polygons polys = to_polygons(std::move(fill.expolygons));
                    // Make a union of polygons, use a safety offset, subtract the preceding polygons.
                    // Bridges are processed first (see SurfaceFill::operator<())

                    // When trimming SolidInfill, add clearance only against TopSolidInfill regions
                    // to prevent overlap where both expand during fill generation. Don't apply
                    // clearance against bridge/InfillOverBridge - those should seamlessly abut
                    // with internal solid to avoid leaving unfilled holes.
                    Polygons trim_polygons = all_polygons;
                    if (!all_polygons.empty() && fill.params.extrusion_role == ExtrusionRole::SolidInfill &&
                        fill.params.density > 0.99f && !top_solid_polygons.empty())
                    {
                        const float clearance = float(fill.params.flow.width() * 0.25);
                        Polygons top_expanded = offset(top_solid_polygons, scale_(clearance));
                        // Combine: non-top fills at original size + top fills with clearance
                        Polygons non_top = diff(all_polygons, top_solid_polygons);
                        trim_polygons = union_(non_top, top_expanded);
                    }

                    fill.expolygons = all_polygons.empty() ? union_safety_offset_ex(polys)
                                                           : diff_ex(polys, trim_polygons, ApplySafetyOffset::Yes);
                    append(all_polygons, std::move(polys));
                }
                else if (&fill != &surface_fills.back())
                    append(all_polygons, to_polygons(fill.expolygons));

                // Track TopSolidInfill polygons for targeted clearance
                if (fill.params.extrusion_role == ExtrusionRole::TopSolidInfill)
                    append(top_solid_polygons, to_polygons(fill.expolygons));
            }
    }

    dbg_fill_phase("TRIMMED", layer, surface_fills);

    // preFlight: Compute the total fill boundary from the layer's fill_expolygons - the area
    // inside the innermost perimeters. This is the true boundary for all fills, unaffected by
    // inter-fill trimming (which introduces safety-offset micro-gaps). Grow/union/shrink can
    // push geometry beyond fill boundaries into perimeter territory, so we clip results back
    // to this boundary after each merge operation.
    Polygons total_fill_boundary;
    for (size_t region_id = 0; region_id < layer.regions().size(); ++region_id)
        append(total_fill_boundary, to_polygons(layer.regions()[region_id]->fill_expolygons()));
    total_fill_boundary = union_(total_fill_boundary);

    // preFlight: Compute the sparse fill threshold once - used for hole removal
    // and sparse absorption below. Based on the sparse fill's actual line spacing so it
    // adapts to different infill densities and nozzle sizes.
    double sparse_min_area = 0;
    float sparse_erode_radius = 0;
    for (const SurfaceFill &sf : surface_fills)
        if (sf.surface.surface_type == stInternal && !sf.expolygons.empty() && sf.params.density < 99.f)
        {
            const float line_spacing = float(scale_(sf.params.spacing)) / (sf.params.density / 100.f);
            sparse_min_area = double(line_spacing) * double(line_spacing) * 16.0;
            sparse_erode_radius = line_spacing * 0.75f;
            break;
        }

    // preFlight: Consolidate all stSolidOverBridge SurfaceFill entries into one.
    // mark_as_infill_above_bridge() assigns different bridge_angles to fragments,
    // causing group_fills to place them in separate SurfaceFill entries. Merge them
    // so all subsequent processing (hole removal, absorption, grow/union/shrink) operates
    // on a single unified stSolidOverBridge region.
    {
        SurfaceFill *primary_sob = nullptr;
        double primary_area = 0;
        for (SurfaceFill &sf : surface_fills)
        {
            if (sf.expolygons.empty() || sf.surface.surface_type != stSolidOverBridge)
                continue;
            double total_area = 0;
            for (const ExPolygon &ep : sf.expolygons)
                total_area += std::abs(ep.area());
            if (!primary_sob || total_area > primary_area)
            {
                primary_sob = &sf;
                primary_area = total_area;
            }
        }
        if (primary_sob)
        {
            for (SurfaceFill &sf : surface_fills)
            {
                if (&sf == primary_sob || sf.expolygons.empty() || sf.surface.surface_type != stSolidOverBridge)
                    continue;
                append(primary_sob->expolygons, std::move(sf.expolygons));
                sf.expolygons.clear();
            }
            primary_sob->expolygons = union_ex(primary_sob->expolygons);
        }
    }

    dbg_fill_phase("SOB_CONSOLIDATED", layer, surface_fills);

    // preFlight: Remove small holes from stInternalSolid ExPolygons.
    // These holes come from trimming against bridge/top fills but are too small for
    // those fills to generate meaningful lines, leaving dark unfilled gaps.
    // Only remove holes that aren't occupied by another fill (stTop, bridge, etc.).
    if (sparse_min_area > 0)
    {
        Polygons other_fill_polys;
        for (const SurfaceFill &sf : surface_fills)
            if (sf.surface.surface_type != stInternalSolid && sf.surface.surface_type != stInternal &&
                !sf.expolygons.empty())
                append(other_fill_polys, to_polygons(sf.expolygons));

        for (SurfaceFill &fill : surface_fills)
        {
            if (fill.expolygons.empty() || fill.surface.surface_type != stInternalSolid)
                continue;
            for (ExPolygon &ep : fill.expolygons)
                ep.holes.erase(
                    std::remove_if(ep.holes.begin(), ep.holes.end(),
                                   [sparse_min_area, &other_fill_polys](const Polygon &hole)
                                   {
                                       if (std::abs(hole.area()) >= sparse_min_area)
                                           return false;
                                       // Keep the hole if another fill occupies it
                                       Polygon contour = hole;
                                       contour.reverse();
                                       return intersection_ex(ExPolygons{ExPolygon(contour)}, other_fill_polys).empty();
                                   }),
                    ep.holes.end());
        }
    }

    dbg_fill_phase("HOLES_RM_SOLID", layer, surface_fills);

    // preFlight: Remove thin holes from stSolidOverBridge ExPolygons.
    // The surface classification creates stSolidOverBridge with holes for model features
    // (arcs, crescents, through-holes). Thin features like arcs and crescents are too
    // narrow for any fill to produce lines, leaving dark gaps. Remove holes that vanish
    // under erosion (too thin) while keeping thick ones (through-holes that bridge fills).
    if (sparse_erode_radius > 0)
        for (SurfaceFill &fill : surface_fills)
        {
            if (fill.surface.surface_type != stSolidOverBridge || fill.expolygons.empty())
                continue;
            for (ExPolygon &ep : fill.expolygons)
            {
                Polygons kept_holes;
                for (const Polygon &hole : ep.holes)
                {
                    // Reverse hole orientation (CW -> CCW) to create a testable ExPolygon.
                    Polygon contour = hole;
                    contour.reverse();
                    // Erosion test: if the hole shape vanishes, it's too thin to keep.
                    ExPolygons eroded = opening_ex(ExPolygons{ExPolygon(contour)}, sparse_erode_radius);
                    if (!eroded.empty())
                        kept_holes.push_back(hole);
                }
                ep.holes = std::move(kept_holes);
            }
        }

    dbg_fill_phase("HOLES_RM_SOB", layer, surface_fills);

    // preFlight: Transfer stInternalSolid that physically touches stSolidOverBridge into SOB.
    // Surface classification splits solid areas into SOB (above bridge) and InternalSolid (other).
    // When these are adjacent, filling them separately leaves thin gaps (e.g. arc-shaped voids
    // around hole features) that are too narrow for sparse fill. Merging adjacent pieces into
    // SOB lets the subsequent grow/union/shrink heal these gaps.
    // Only transfer InternalSolid that geometrically touches SOB - never reclassify
    // distant pieces that happen to share the same layer.
    {
        SurfaceFill *sob_fill = nullptr;
        for (SurfaceFill &sf : surface_fills)
            if (sf.surface.surface_type == stSolidOverBridge && !sf.expolygons.empty())
            {
                sob_fill = &sf;
                break;
            }
        if (sob_fill)
        {
            // Grow SOB slightly to detect touching/near-touching InternalSolid.
            // 0.1mm bridges classification micro-gaps without reaching distant pieces.
            Polygons sob_grown = offset(to_polygons(sob_fill->expolygons), scale_(0.1));

            bool merged = false;
            for (SurfaceFill &sf : surface_fills)
            {
                if (&sf == sob_fill || sf.surface.surface_type != stInternalSolid || sf.expolygons.empty())
                    continue;
                ExPolygons to_transfer;
                ExPolygons to_keep;
                for (const ExPolygon &ep : sf.expolygons)
                {
                    if (!intersection_ex(ExPolygons{ep}, sob_grown).empty())
                        to_transfer.push_back(ep);
                    else
                        to_keep.push_back(ep);
                }
                if (!to_transfer.empty())
                {
                    sf.expolygons = std::move(to_keep);
                    append(sob_fill->expolygons, std::move(to_transfer));
                    merged = true;
                }
            }
            if (merged)
                sob_fill->expolygons = union_ex(sob_fill->expolygons);
        }
    }

    dbg_fill_phase("ADJACENCY_XFER", layer, surface_fills);

    // preFlight: After stSolidOverBridge modifications (hole removal + thin region merge),
    // re-trim fills that were trimmed against the original stSolidOverBridge. The expanded
    // coverage now overlaps with remaining stInternalSolid and sparse fills.
    {
        Polygons sob_polys;
        for (const SurfaceFill &sf : surface_fills)
            if (sf.surface.surface_type == stSolidOverBridge && !sf.expolygons.empty())
                append(sob_polys, to_polygons(sf.expolygons));
        if (!sob_polys.empty())
            for (SurfaceFill &sf : surface_fills)
            {
                if (sf.expolygons.empty())
                    continue;
                if (sf.surface.surface_type == stInternalSolid || sf.surface.surface_type == stInternal)
                    sf.expolygons = diff_ex(sf.expolygons, sob_polys);
            }
    }

    dbg_fill_phase("SOB_RETRIM", layer, surface_fills);

    // preFlight: Absorb small sparse infill regions that are fully enclosed by solid infill.
    // These regions are too small for meaningful sparse fill lines and appear as unfilled holes
    // within solid infill areas. Only absorb regions entirely inside a solid contour - never
    // expand into external sparse areas at the solid boundary.
    for (SurfaceFill &solid_fill : surface_fills)
    {
        if (solid_fill.expolygons.empty())
            continue;
        if (solid_fill.surface.surface_type != stInternalSolid && solid_fill.surface.surface_type != stSolidOverBridge)
            continue;

        // Build the "filled" solid boundary from contours only (no holes).
        // For stInternalSolid: contours already encompass the sparse pockets (one big region
        // with holes carved out), so stripping holes and unioning is sufficient.
        // For stSolidOverBridge: mark_as_infill_above_bridge() fragments the solid into
        // disjoint pieces covering only areas above bridge extrusions. Sparse pockets sit
        // in gaps between fragments. Morphological closing (dilate + erode) bridges these
        // inter-fragment gaps to reconstruct the encompassing boundary.
        ExPolygons solid_filled;
        if (solid_fill.surface.surface_type == stSolidOverBridge && sparse_erode_radius > 0)
        {
            Polygons sob_contours;
            for (const ExPolygon &ep : solid_fill.expolygons)
                sob_contours.push_back(ep.contour);
            solid_filled = closing_ex(sob_contours, sparse_erode_radius);
        }
        else
        {
            solid_filled.reserve(solid_fill.expolygons.size());
            for (const ExPolygon &ep : solid_fill.expolygons)
                solid_filled.emplace_back(ep.contour);
            solid_filled = union_ex(solid_filled);
        }

        for (SurfaceFill &sparse_fill : surface_fills)
        {
            if (sparse_fill.surface.surface_type != stInternal || sparse_fill.expolygons.empty())
                continue;
            if (sparse_fill.params.density >= 99.f)
                continue;

            // Threshold: area that can't fit meaningful sparse fill.
            // line_spacing is the actual distance between sparse fill lines.
            // A region needs at least ~4x4 grid of lines to be useful.
            const float line_spacing = float(scale_(sparse_fill.params.spacing)) / (sparse_fill.params.density / 100.f);
            const double min_area = double(line_spacing) * double(line_spacing) * 16.0;

            ExPolygons to_absorb;
            ExPolygons to_keep;

            for (const ExPolygon &ep : sparse_fill.expolygons)
            {
                double area = std::abs(ep.area());
                if (area >= min_area)
                {
                    to_keep.push_back(ep);
                    continue;
                }

                // Check if this small sparse region is fully enclosed by this solid fill.
                // If the intersection with the solid contours covers >= 90% of the sparse
                // region's area, it's an internal pocket that should be filled solid.
                double contained_area = 0;
                for (const ExPolygon &c : intersection_ex(ExPolygons{ep}, solid_filled))
                    contained_area += std::abs(c.area());

                if (contained_area >= area * 0.9)
                    to_absorb.push_back(ep);
                else
                    to_keep.push_back(ep);
            }

            if (!to_absorb.empty())
            {
                sparse_fill.expolygons = std::move(to_keep);
                append(solid_fill.expolygons, std::move(to_absorb));
                solid_fill.expolygons = union_ex(solid_fill.expolygons);
            }
        }

        // preFlight: Merge nearby solid ExPolygons into a unified region. Grow/union/shrink
        // bridges micro-gaps between fragments that plain union can't bridge.
        // stSolidOverBridge uses sparse_erode_radius (gaps proportional to sparse spacing).
        // stInternalSolid uses 1x extrusion width (small classification gaps).
        if (solid_fill.expolygons.size() > 1)
        {
            const float merge_delta = (solid_fill.surface.surface_type == stSolidOverBridge && sparse_erode_radius > 0)
                                          ? sparse_erode_radius
                                          : float(scale_(solid_fill.params.flow.width()));
            Polygons grown;
            for (const ExPolygon &ep : solid_fill.expolygons)
                append(grown, offset(ep, merge_delta));
            solid_fill.expolygons = intersection_ex(offset_ex(union_(grown), -merge_delta), total_fill_boundary);
        }
    }

    dbg_fill_phase("ABSORBED_GROWN", layer, surface_fills);

    // preFlight: After grow/union/shrink, solid fills may have expanded into adjacent fills.
    // Re-trim to prevent overlaps: solid fills against each other (priority to earlier entries),
    // then sparse fills against all expanded solid fills.
    {
        Polygons processed_solid;
        for (SurfaceFill &sf : surface_fills)
        {
            if (sf.expolygons.empty())
                continue;
            if (sf.surface.surface_type != stInternalSolid && sf.surface.surface_type != stSolidOverBridge)
                continue;
            if (!processed_solid.empty())
                sf.expolygons = diff_ex(sf.expolygons, processed_solid);
            append(processed_solid, to_polygons(sf.expolygons));
        }
        if (!processed_solid.empty())
            for (SurfaceFill &sf : surface_fills)
                if (sf.surface.surface_type == stInternal && !sf.expolygons.empty())
                    sf.expolygons = diff_ex(sf.expolygons, processed_solid);
    }

    dbg_fill_phase("RETRIMMED", layer, surface_fills);

    // preFlight: Remove tiny stSolidOverBridge expolygons that are too small for meaningful
    // fill lines. The grow/union/shrink merge can leave behind small fragments near tight
    // features (screw holes, pegs) that overlap perimeters when filled.
    // Use solid fill spacing (not sparse) for the threshold - stSolidOverBridge is 100% density
    // so even small areas produce valid fill. The sparse_min_area threshold (~127mm2 at 16%
    // density) was wildly too large and deleted legitimate SOB regions.
    {
        double sob_min_area = 0;
        for (const SurfaceFill &sf : surface_fills)
            if (sf.surface.surface_type == stSolidOverBridge && !sf.expolygons.empty())
            {
                // Solid fill at 100% density: minimum useful area is a few line widths squared.
                // Use scale_() so area threshold is in nm^2 like ep.area().
                double solid_spacing = scale_(sf.params.spacing);
                sob_min_area = solid_spacing * solid_spacing * 4.0; // 2x2 line grid
                break;
            }
        if (sob_min_area > 0)
            for (SurfaceFill &sf : surface_fills)
                if (sf.surface.surface_type == stSolidOverBridge && !sf.expolygons.empty())
                    sf.expolygons.erase(std::remove_if(sf.expolygons.begin(), sf.expolygons.end(),
                                                       [sob_min_area](const ExPolygon &ep)
                                                       { return std::abs(ep.area()) < sob_min_area; }),
                                        sf.expolygons.end());
    }

    dbg_fill_phase("TINY_SOB_RM", layer, surface_fills);

    // preFlight: Merge fragmented bridge infill into a unified region.
    // Bridge detection creates separate ExPolygons for bridge-over-open-space (stBottomBridge)
    // and bridge-over-sparse (stInternalBridge) with different angles. Merge all bridge fills
    // into one SurfaceFill, preferring stBottomBridge's angle (optimized for anchoring over
    // open spans). Apply grow/union/shrink to bridge micro-gaps between fragments.
    if (sparse_erode_radius > 0)
    {
        // Find all bridge SurfaceFill entries and select the primary (winning) one.
        // Prefer stBottomBridge (bridge-over-open-space) with the largest total area.
        SurfaceFill *primary_bridge = nullptr;
        double primary_area = 0;
        for (SurfaceFill &sf : surface_fills)
        {
            if (sf.expolygons.empty() || !sf.surface.is_bridge())
                continue;
            double total_area = 0;
            for (const ExPolygon &ep : sf.expolygons)
                total_area += std::abs(ep.area());
            // stBottomBridge always wins over stInternalBridge; among same type, largest area wins
            bool dominated = primary_bridge && primary_bridge->surface.surface_type == stBottomBridge &&
                             sf.surface.surface_type != stBottomBridge;
            bool dominates = !primary_bridge ||
                             (sf.surface.surface_type == stBottomBridge &&
                              primary_bridge->surface.surface_type != stBottomBridge) ||
                             (sf.surface.surface_type == primary_bridge->surface.surface_type &&
                              total_area > primary_area);
            if (!dominated && dominates)
            {
                primary_bridge = &sf;
                primary_area = total_area;
            }
        }

        if (primary_bridge)
        {
            // Merge all other bridge ExPolygons into the primary
            bool merged = false;
            for (SurfaceFill &sf : surface_fills)
            {
                if (&sf == primary_bridge || sf.expolygons.empty() || !sf.surface.is_bridge())
                    continue;
                append(primary_bridge->expolygons, std::move(sf.expolygons));
                sf.expolygons.clear();
                merged = true;
            }

            // Grow/union/shrink to bridge micro-gaps between fragments
            if (primary_bridge->expolygons.size() > 1)
            {
                const float merge_delta = float(scale_(primary_bridge->params.flow.width()));
                Polygons grown;
                for (const ExPolygon &ep : primary_bridge->expolygons)
                    append(grown, offset(ep, merge_delta));
                primary_bridge->expolygons = intersection_ex(offset_ex(union_(grown), -merge_delta),
                                                             total_fill_boundary);
            }

            // Re-trim adjacent fills against the expanded bridge region to prevent overlap
            if (merged)
            {
                Polygons bridge_polys = to_polygons(primary_bridge->expolygons);
                if (!bridge_polys.empty())
                    for (SurfaceFill &sf : surface_fills)
                    {
                        if (&sf == primary_bridge || sf.expolygons.empty() || sf.surface.is_bridge())
                            continue;
                        sf.expolygons = diff_ex(sf.expolygons, bridge_polys);
                    }
            }
        }
    }

    dbg_fill_phase("BRIDGE_MERGED", layer, surface_fills);

    // we need to detect any narrow surfaces that might collapse
    // when adding spacing below
    // such narrow surfaces are often generated in sloping walls
    // by bridge_over_infill() and combine_infill() as a result of the
    // subtraction of the combinable area from the layer infill area,
    // which leaves small areas near the perimeters
    // we are going to grow such regions by overlapping them with the void (if any)
    // TODO: detect and investigate whether there could be narrow regions without
    // any void neighbors
    if (has_internal_voids)
    {
        // Internal voids are generated only if "infill_only_where_needed" or "infill_every_layers" are active.
        coord_t distance_between_surfaces = 0;
        Polygons surfaces_polygons;
        Polygons voids;
        int region_internal_infill = -1;
        int region_solid_infill = -1;
        int region_some_infill = -1;
        for (SurfaceFill &surface_fill : surface_fills)
            if (!surface_fill.expolygons.empty())
            {
                distance_between_surfaces = std::max(distance_between_surfaces,
                                                     surface_fill.params.flow.scaled_spacing());
                append((surface_fill.surface.surface_type == stInternalVoid) ? voids : surfaces_polygons,
                       to_polygons(surface_fill.expolygons));
                if (surface_fill.surface.surface_type == stInternalSolid)
                    region_internal_infill = (int) surface_fill.region_id;
                if (surface_fill.surface.is_solid())
                    region_solid_infill = (int) surface_fill.region_id;
                if (surface_fill.surface.surface_type != stInternalVoid)
                    region_some_infill = (int) surface_fill.region_id;
            }
        if (!voids.empty() && !surfaces_polygons.empty())
        {
            // First clip voids by the printing polygons, as the voids were ignored by the loop above during mutual clipping.
            voids = diff(voids, surfaces_polygons);
            // Corners of infill regions, which would not be filled with an extrusion path with a radius of distance_between_surfaces/2
            Polygons collapsed = diff(surfaces_polygons,
                                      opening(surfaces_polygons, float(distance_between_surfaces / 2),
                                              float(distance_between_surfaces / 2 + ClipperSafetyOffset)));
            //FIXME why the voids are added to collapsed here? First it is expensive, second the result may lead to some unwanted regions being
            // added if two offsetted void regions merge.
            // polygons_append(voids, collapsed);
            ExPolygons extensions = intersection_ex(expand(collapsed, float(distance_between_surfaces)), voids,
                                                    ApplySafetyOffset::Yes);
            // Now find an internal infill SurfaceFill to add these extrusions to.
            SurfaceFill *internal_solid_fill = nullptr;
            unsigned int region_id = 0;
            if (region_internal_infill != -1)
                region_id = region_internal_infill;
            else if (region_solid_infill != -1)
                region_id = region_solid_infill;
            else if (region_some_infill != -1)
                region_id = region_some_infill;
            const LayerRegion &layerm = *layer.regions()[region_id];
            for (SurfaceFill &surface_fill : surface_fills)
                if (surface_fill.surface.surface_type == stInternalSolid &&
                    std::abs(layer.height - surface_fill.params.flow.height()) < EPSILON)
                {
                    internal_solid_fill = &surface_fill;
                    break;
                }
            if (internal_solid_fill == nullptr)
            {
                // Produce another solid fill.
                params.extruder = layerm.region().extruder(frSolidInfill);
                params.pattern = layerm.region().config().solid_fill_pattern.value;
                params.density = 100.f;
                params.extrusion_role = ExtrusionRole::InternalInfill;
                params.angle = float(Geometry::deg2rad(layerm.region().config().fill_angle.value));
                // calculate the actual flow we'll be using for this infill
                params.flow = layerm.flow(frSolidInfill);
                params.spacing = params.flow.spacing();
                surface_fills.emplace_back(params);
                surface_fills.back().surface.surface_type = stInternalSolid;
                surface_fills.back().surface.thickness = layer.height;
                surface_fills.back().expolygons = std::move(extensions);
            }
            else
            {
                append(extensions, std::move(internal_solid_fill->expolygons));
                internal_solid_fill->expolygons = union_ex(extensions);
            }
        }
    }

    // This was forcing ALL stInternalSolid surfaces to use ipEnsuring (Athena-style fill),
    // overriding the user's top_fill_pattern selection (Monotonic, Rectilinear, etc.).
    // ipEnsuring uses WallToolPaths which doesn't adjust spacing like FillRectilinear does,
    // causing gaps when fill runs parallel to the area.
    // Comment out this forced override to respect the user's pattern selection.
    /*
    // Use ipEnsuring pattern for all internal Solids.
    {
        for (size_t surface_fill_id = 0; surface_fill_id < surface_fills.size(); ++surface_fill_id)
            if (SurfaceFill &fill = surface_fills[surface_fill_id];
                    fill.surface.surface_type == stInternalSolid
                    || fill.surface.surface_type == stSolidOverBridge) {
                fill.params.pattern = ipEnsuring;
            }
    }
    */

    dbg_fill_phase("FINAL", layer, surface_fills);

    return surface_fills;
}

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
void export_group_fills_to_svg(const char *path, const std::vector<SurfaceFill> &fills)
{
    BoundingBox bbox;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            bbox.merge(get_extents(expoly));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            svg.draw(expoly, surface_type_to_color_name(fill.surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}
#endif

// Infill is now generated and assigned directly to islands in make_fills().
// This function is no longer called and remains only for backward compatibility.
static void insert_fills_into_islands(Layer &layer, uint32_t fill_region_id, uint32_t fill_begin, uint32_t fill_end)
{
    // No-op: Infill assignment now happens during per-island generation
}

void Layer::clear_fills()
{
    for (LayerRegion *layerm : m_regions)
        layerm->m_fills.clear();
    for (LayerSlice &lslice : lslices_ex)
        for (LayerIsland &island : lslice.islands)
            island.fills.clear();

    // Interlocking perimeters are generated in make_fills() based on sparse infill boundaries.
    // When infill changes (perimeter count, solid layers, etc.), sparse boundaries change and
    // interlocking must be regenerated. Remove old interlocking to prevent duplicates on re-slice.
    //
    // Process each region separately (each has its own m_perimeters collection)
    for (size_t region_id = 0; region_id < m_regions.size(); ++region_id)
    {
        LayerRegion *layerm = m_regions[region_id];

        // Iterate islands in reverse order to safely remove without index invalidation issues
        for (LayerSlice &lslice : this->lslices_ex)
        {
            for (auto island_it = lslice.islands.rbegin(); island_it != lslice.islands.rend(); ++island_it)
            {
                LayerIsland &island = *island_it;

                // Only process islands in this region
                if (island.perimeters.region() != region_id)
                    continue;

                // Check if this island has interlocking by examining the last entity in its range
                uint32_t island_begin = *island.perimeters.begin();
                uint32_t island_end = *island.perimeters.end();

                if (island_end <= island_begin)
                    continue; // Empty range

                uint32_t last_index = island_end - 1;
                if (last_index >= layerm->m_perimeters.entities.size())
                    continue; // Invalid index

                ExtrusionEntity *last_entity = layerm->m_perimeters.entities[last_index];

                // Check if this is an interlocking collection
                bool is_interlocking = false;
                if (const ExtrusionEntityCollection *collection = dynamic_cast<const ExtrusionEntityCollection *>(
                        last_entity))
                {
                    if (!collection->entities.empty())
                    {
                        if (const ExtrusionLoop *loop = dynamic_cast<const ExtrusionLoop *>(
                                collection->entities.front()))
                        {
                            if (!loop->paths.empty() &&
                                loop->paths.front().role() == ExtrusionRole::InterlockingPerimeter)
                            {
                                is_interlocking = true;
                            }
                        }
                    }
                }

                if (!is_interlocking)
                    continue; // No interlocking in this island

                // Remove the interlocking collection
                delete last_entity;
                layerm->m_perimeters.entities.erase(layerm->m_perimeters.entities.begin() + last_index);

                // Shrink this island's range by 1 (remove the interlocking)
                island.perimeters = LayerExtrusionRange(region_id, ExtrusionRange(island_begin, island_end - 1));

                // Shift all other islands' ranges back by 1 (inverse of insertion shift)
                for (LayerSlice &other_lslice : this->lslices_ex)
                {
                    for (LayerIsland &other_island : other_lslice.islands)
                    {
                        if (&other_island == &island)
                            continue; // Skip this island
                        if (other_island.perimeters.region() != region_id)
                            continue; // Different region

                        uint32_t other_begin = *other_island.perimeters.begin();
                        uint32_t other_end = *other_island.perimeters.end();

                        // Only shift ranges that reference indices > last_index
                        if (other_begin > last_index)
                        {
                            other_island.perimeters = LayerExtrusionRange(region_id, ExtrusionRange(other_begin - 1,
                                                                                                    other_end - 1));
                        }
                        else if (other_end > last_index)
                        {
                            // Range spans the removed index - shouldn't happen, but handle gracefully
                            other_island.perimeters = LayerExtrusionRange(region_id,
                                                                          ExtrusionRange(other_begin, other_end - 1));
                        }
                    }
                }
            }
        }
    }
}

void Layer::make_fills(FillAdaptive::Octree *adaptive_fill_octree, FillAdaptive::Octree *support_fill_octree,
                       FillLightning::Generator *lightning_generator)
{
    this->clear_fills();

    std::vector<SurfaceFill> surface_fills = group_fills(*this);
    const Slic3r::BoundingBox bbox = this->object()->bounding_box();
    const auto resolution = this->object()->print()->config().gcode_resolution.value;
    const auto perimeter_generator = this->object()->config().perimeter_generator;

    // Process each region to add interlocking shells at sparse boundaries

    for (size_t region_id = 0; region_id < m_regions.size(); ++region_id)
    {
        LayerRegion *layerm = m_regions[region_id];

        if (!layerm->region().config().interlock_perimeters_enabled)
            continue;

        // Get the interlocking count directly - no percentage calculation needed!
        const int num_interlocking_shells = layerm->num_interlocking_shells();

        if (num_interlocking_shells <= 0)
            continue;

        // When interlocking is enabled, extract sparse infill and bridge infill for interlocking perimeters.
        //
        // ARCHITECTURAL NOTE: discover_vertical_shells() creates TWO types of stInternalSolid:
        // 1. Top/bottom solid support (expanding layers under stTop/above stBottom) - must PRESERVE
        // 2. Vertical shell rings (perimeter-to-sparse gap fill) - should EXTRACT for interlocking
        //
        // We use detection heuristics to distinguish these types:
        // - Proximity to stTop/stBottom surfaces (within N layers)
        // - Area change ratio between adjacent layers (expanding/contracting = solid support)
        // - Conservative fallback (when uncertain, preserve to avoid gaps in solid support)
        //
        // We extract:
        // - stInternal (sparse infill) - primary target for interlocking
        // - stInternalBridge/stBottomBridge - interlocking provides better support than bridge infill
        // - stInternalSolid (ONLY vertical shell rings detected via heuristics)
        //
        // We preserve:
        // - stInternalSolid (top/bottom solid support detected via heuristics)
        // - stTop/stBottom - external surfaces

        // Interlocking perimeters must be generated separately for each island to ensure proper island ordering
        // and prevent duplicate printing. This follows the same architectural pattern as regular perimeters and infill:
        // 1. Extract geometry per-island (using spatial intersection with island.boundary)
        // 2. Generate extrusions for each island independently
        // 3. Assign to islands directly (no global collections)
        //
        // This ensures each island prints completely (perimeters + interlocking + infill) before moving to the next,
        // maintaining proper print order and preventing the bug where all islands reference the same unified collection.

        // Iterate through all islands to generate interlocking for each independently
        int island_counter = 0; // Track island index for debug

        for (LayerSlice &lslice : this->lslices_ex)
        {
            for (LayerIsland &island : lslice.islands)
            {
                // Only process islands belonging to this region
                if (island.perimeters.region() != region_id)
                    continue;

                island_counter++; // Increment for each island processed

                // With the upstream fix in PrintObject.cpp (lines 1827-1841), vertical shell rings are no longer
                // created when interlocking is enabled. Therefore, any stInternalSolid that exists is legitimate
                // solid support (projected from stTop/stBottom surfaces) and should be preserved.
                //
                // We only need to extract:
                // - stInternal (sparse infill) → for interlocking perimeters
                // And preserve:
                // - stInternalSolid (all legitimate solid support)
                // - stInternalBridge/stBottomBridge (bridge infill - generates normally)
                // - stTop/stBottom (external surfaces)
                //
                // CRITICAL: Extract sparse regions for THIS ISLAND ONLY using spatial intersection with island.boundary.
                // This prevents mixing sparse regions from different islands into one unified collection.
                ExPolygons sparse_regions;
                ExPolygons solid_regions; // Collect all solid areas to subtract from sparse

                // Process each surface type - extract only regions that intersect with THIS island
                for (size_t sf_idx = 0; sf_idx < surface_fills.size(); sf_idx++)
                {
                    SurfaceFill &sf = surface_fills[sf_idx];

                    if (sf.region_id != region_id)
                        continue;

                    if (sf.surface.surface_type == stInternal)
                    {
                        // Extract sparse infill for THIS island using spatial intersection
                        ExPolygons island_sparse = intersection_ex(sf.expolygons, ExPolygons{island.boundary});
                        append(sparse_regions, island_sparse);
                    }
                    else if (sf.surface.surface_type == stInternalBridge || sf.surface.surface_type == stBottomBridge)
                    {
                        // Bridge infill (stInternalBridge, stBottomBridge) has specific properties that shouldn't be replaced.
                        // Collect bridge areas to subtract from interlocking sparse regions to avoid overlap.
                        // Extract only bridge areas that intersect with THIS island
                        ExPolygons island_bridge = intersection_ex(sf.expolygons, ExPolygons{island.boundary});
                        append(solid_regions, island_bridge);
                    }
                    else if (sf.surface.surface_type == stInternalSolid)
                    {
                        // Preserve all stInternalSolid - upstream fix prevents vertical shell rings
                        // Only legitimate solid support (projected from stTop/stBottom) and bridge anchors exist
                        // Collect solid areas to subtract from interlocking sparse regions to avoid overlap
                        // Extract only solid areas that intersect with THIS island
                        ExPolygons island_solid = intersection_ex(sf.expolygons, ExPolygons{island.boundary});
                        append(solid_regions, island_solid);
                        continue; // Keep as solid infill
                    }
                }

                // preFlight: Also collect stInternalVoid surfaces for interlocking on combined-infill layers.
                // combine_infill() converts stInternal to stInternalVoid on intermediate (void) layers,
                // but stInternalVoid is excluded from surface_fills (group_fills skips it). Without this,
                // void layers have no sparse_regions and generate no interlocking perimeters at all.
                // Interlocking perimeters are structural (like regular perimeters) and must be printed
                // on every layer regardless of infill combination.
                for (const Surface &surface : layerm->fill_surfaces())
                {
                    if (surface.surface_type == stInternalVoid)
                    {
                        ExPolygons island_void = intersection_ex(ExPolygons{surface.expolygon},
                                                                 ExPolygons{island.boundary});
                        append(sparse_regions, island_void);
                    }
                }

                // Bridge infill (stInternalBridge) should be subtracted from sparse to avoid overlaps.
                // Bridge support anchors (stInternalSolid created by bridge_over_infill) are NOT created when interlocking
                // is enabled, so we don't need to worry about them here.
                if (!solid_regions.empty())
                {
                    sparse_regions = diff_ex(sparse_regions, solid_regions);
                }

                // Surface extraction complete - sparse_regions contains stInternal + stInternalVoid (sparse infill) for THIS island
                // All other surface types (stInternalSolid, stInternalBridge, stTop, stBottom) are preserved

                // group_fills() can fragment sparse regions via diff_ex() when subtracting solid surfaces (e.g., top layer transition beads).
                // Fragmented regions create jagged boundaries with excess vertices, causing Athena to fragment toolpaths.
                // Merge fragments back into unified regions for clean, smooth interlocking perimeter generation.
                // NOTE: This now merges fragments WITHIN this island only, not across islands.
                if (sparse_regions.size() > 1)
                {
                    // Union all fragments with a small safety offset to merge adjacent pieces
                    sparse_regions = union_safety_offset_ex(to_polygons(sparse_regions));
                }

                // The INTERLOCK-SUPPRESS-BRIDGE-SOLID logic in PrintObject.cpp can create degenerate
                // polygons with zero area (collapsed lines/points). These cause Athena to generate
                // malformed toolpaths (single diagonal lines instead of proper shells).
                // Filter them out before interlocking generation.
                const double min_area_threshold = scale_(scale_(0.1)); // 0.1 mm² minimum
                size_t before_filter = sparse_regions.size();
                sparse_regions.erase(std::remove_if(sparse_regions.begin(), sparse_regions.end(),
                                                    [min_area_threshold](const ExPolygon &ep)
                                                    { return ep.area() < min_area_threshold; }),
                                     sparse_regions.end());

                // Skip this island if no sparse regions remain
                if (sparse_regions.empty())
                {
                    continue;
                }

                // Get flow parameters for shell width calculation
                const Flow perimeter_flow = layerm->flow(frPerimeter);
                const coord_t perimeter_scaled_width = perimeter_flow.scaled_width();

                // Check minimum area before processing (5mm² minimum)
                const coord_t min_area = scale_(scale_(5.0));
                double total_area = 0;
                for (const ExPolygon &ex : sparse_regions)
                {
                    total_area += ex.area();
                }

                if (total_area < min_area)
                {
                    continue;
                }

                // The sparse_regions boundary already has infill/perimeter overlap baked in from upstream
                // (PerimeterGenerator.cpp). When interlocking is enabled, we want P/P overlap to control
                // the overlap between innermost normal perimeter and outermost interlocking instead.
                // So we adjust by (pp_overlap - infill_overlap) to replace infill_overlap with pp_overlap.

                // Get P/P overlap amount (what we want for outer interlocking)
                // Must match what the perimeter generator actually uses:
                // - Arachne: hardcoded overlap of (1 - 0.25*PI) ≈ 21.46%
                // - Athena: uses perimeter_perimeter_overlap setting
                coord_t pp_overlap_amount = 0;
                if (perimeter_generator == PerimeterGeneratorType::Arachne)
                {
                    // Arachne uses hardcoded overlap: (1 - 0.25*PI) ≈ 21.46% of perimeter width
                    constexpr double arachne_overlap_percent = (1.0 - 0.25 * M_PI); // ≈ 0.2146
                    pp_overlap_amount = coord_t(perimeter_scaled_width * arachne_overlap_percent);
                }
                else
                {
                    // Athena uses the perimeter_perimeter_overlap setting
                    const ConfigOptionFloatOrPercent &pp_overlap = layerm->region().config().perimeter_perimeter_overlap;
                    if (pp_overlap.percent)
                    {
                        pp_overlap_amount = coord_t(perimeter_scaled_width * (pp_overlap.value / 100.0));
                    }
                    else
                    {
                        pp_overlap_amount = coord_t(scale_(pp_overlap.value));
                    }
                }

                // Get infill/perimeter overlap amount (what was already applied upstream)
                const ConfigOptionFloatOrPercent &infill_overlap = layerm->region().config().infill_overlap;
                coord_t infill_overlap_amount = 0;
                if (infill_overlap.percent)
                {
                    infill_overlap_amount = coord_t(perimeter_scaled_width * (infill_overlap.value / 100.0));
                }
                else
                {
                    infill_overlap_amount = coord_t(scale_(infill_overlap.value));
                }

                // Net adjustment: replace infill_overlap with pp_overlap
                // Positive = expand outward, negative = shrink inward
                coord_t overlap_adjustment = pp_overlap_amount - infill_overlap_amount;

                // Before expanding sparse for P/P overlap, extract any stTop surfaces to avoid conflicts.
                // If we expand into top areas, it creates a ring of incorrectly classified top surfaces
                // around the perimeter that shifts interlocking inward.
                // CRITICAL: Extract only top surfaces that intersect with THIS island
                ExPolygons top_surfaces;
                for (const SurfaceFill &sf : surface_fills)
                {
                    if (sf.region_id == region_id && sf.surface.surface_type == stTop)
                    {
                        ExPolygons island_tops = intersection_ex(sf.expolygons, ExPolygons{island.boundary});
                        append(top_surfaces, island_tops);
                    }
                }

                // Save original sparse_regions before adjustment (needed for consumed area calculation)
                // The original has infill_overlap boundary from PerimeterGenerator
                ExPolygons original_sparse_regions = sparse_regions;

                // Adjust sparse regions: replace infill_overlap with pp_overlap for outer interlocking
                ExPolygons expanded_sparse = offset_ex(sparse_regions, float(overlap_adjustment));

                // Ensure expanded sparse doesn't overlap with top surfaces
                if (!top_surfaces.empty())
                {
                    expanded_sparse = diff_ex(expanded_sparse, top_surfaces);
                }

                // Use expanded sparse regions for interlocking generation
                sparse_regions = expanded_sparse;

                if (sparse_regions.empty())
                {
                    continue;
                }

                // Define spacing for BOTH layer types upfront for symmetry
                const bool is_odd_layer = (this->id() % 2 == 1);

                // Flow-scaled bead widths. GCode.cpp applies sqrt(flow) width scaling,
                // so spacings must account for actual bead sizes, not nominal perimeter width.
                const coord_t base_w = perimeter_scaled_width;                      // 1.0x flow
                const coord_t main_w = coord_t(perimeter_scaled_width * sqrt(2.0)); // 2.0x flow

                // Boundary bead flow: (3 + 2*sqrt(2)) / 4 ~ 1.457x
                // Derived so that: (a) outer edge aligns with 100% bead's outer edge,
                // and (b) gap to first 200% bead equals the 200%<->200% gap.
                // This bead appears on shell 0 (even layers) and innermost (odd layers).
                constexpr double BOUNDARY_FLOW = (3.0 + 2.0 * 1.41421356) / 4.0;
                const coord_t boundary_w = coord_t(perimeter_scaled_width * sqrt(BOUNDARY_FLOW));
                const coord_t boundary_shift = (boundary_w - base_w) / 2;

                // Overlap reduces centerline spacing for bonding (same as perimeter/perimeter overlap)
                const auto &overlap_setting = layerm->region().config().interlock_perimeter_overlap;
                const coord_t overlap_reduction = preFlight::PreciseWalls::calculate_perimeter_spacing(perimeter_flow,
                                                                                                       overlap_setting);
                const coord_t overlap_amount = perimeter_scaled_width - overlap_reduction;

                // Adjacent: 100% and 200% beads just touch at 0% overlap
                // Gapped = 2 * adjacent (maintains 2:1 ratio for interleaving of inner shells)
                const coord_t adjacent = (base_w + main_w) / 2 - overlap_amount;
                const coord_t gapped = 2 * adjacent;

                // bead_width_0 = pw for BOTH layers so Athena's skeleton makes identical
                // decisions. Even layer shell 0 (~146% boundary bead) is generated separately
                // via polygon offset, not by Athena, so spacing_0 compensates for pw-based shell 0.
                const coord_t odd_external_spacing = adjacent;
                const coord_t even_external_spacing = gapped;

                const coord_t external_spacing = is_odd_layer ? odd_external_spacing : even_external_spacing;

                // Internal spacing (gapped): unchanged for perfect 200%<->200% interleaving
                const coord_t internal_spacing = gapped;

                // Innermost spacings equalize total span so innermost beads align between layers.
                // Odd innermost (~146% bead) gets polygon offset by boundary_shift after Athena.
                const coord_t odd_innermost_spacing = gapped - boundary_shift;
                const coord_t even_innermost_spacing = adjacent - boundary_shift;
                const coord_t innermost_spacing = is_odd_layer ? odd_innermost_spacing : even_innermost_spacing;

                // Handle edge case: reduce shells if space is too constrained
                BoundingBox sparse_bbox = get_extents(sparse_regions);
                const coord_t min_dimension = std::min(sparse_bbox.size().x(), sparse_bbox.size().y());
                const coord_t estimated_shell_width = num_interlocking_shells * perimeter_scaled_width * 2;

                int actual_shells = num_interlocking_shells;
                if (min_dimension < estimated_shell_width)
                {
                    actual_shells = min_dimension / (perimeter_scaled_width * 2);
                    if (actual_shells <= 0)
                        continue;
                }

                // Create an ExtrusionEntityCollection to hold all interlocking loops
                ExtrusionEntityCollection interlocking_collection;

                const coord_t half_width = perimeter_scaled_width / 2;
                const bool prefer_clockwise = this->object()->print()->config().prefer_clockwise_movements;

                // PROBLEM: Default generation order is depth-first:
                //   Shell 0: Contour A, Hole 1, Hole 2, Contour B, Hole 3...
                //   Shell 1: Contour A, Hole 1, Hole 2, Contour B, Hole 3...
                // This causes excessive travel between unrelated regions.
                //
                // SOLUTION: Collect all loops with metadata, build containment tree, traverse depth-first
                // so each region completes all its shells before moving to the next region:
                //   Contour A: Shell 0, Shell 1, Shell 2...
                //   Hole 1: Shell 0, Shell 1, Shell 2...
                //   etc.

                // Structure to hold loop with metadata
                struct LoopNode
                {
                    ExtrusionLoop loop;
                    Polygon polygon; // For containment testing
                    size_t shell_idx;
                    bool is_hole;
                    std::vector<size_t> children;
                    size_t parent = SIZE_MAX;
                };
                std::vector<LoopNode> all_loops;

                // Modified create_loop to collect instead of append
                auto collect_loop = [&](const Polygon &poly, double flow_ratio, size_t shell_idx, bool is_hole)
                {
                    if (poly.size() < 3)
                        return;

                    ExtrusionFlow shell_flow(perimeter_flow.mm3_per_mm() * flow_ratio, perimeter_flow.width(),
                                             perimeter_flow.height());
                    ExtrusionAttributes attribs(ExtrusionRole::InterlockingPerimeter, shell_flow);
                    attribs.perimeter_index = static_cast<uint16_t>(shell_idx);

                    ExtrusionPath path(attribs);
                    for (const Point &pt : poly.points)
                    {
                        path.polyline.append(pt);
                    }
                    if (path.polyline.first_point() != path.polyline.last_point())
                    {
                        path.polyline.append(path.polyline.first_point());
                    }

                    ExtrusionPaths paths;
                    paths.push_back(std::move(path));
                    ExtrusionLoop loop(std::move(paths));

                    // Orient based on user preference
                    const bool is_cw = loop.is_clockwise();
                    bool should_reverse;
                    if (is_hole)
                    {
                        should_reverse = prefer_clockwise ? is_cw : !is_cw;
                    }
                    else
                    {
                        should_reverse = prefer_clockwise ? !is_cw : is_cw;
                    }
                    if (should_reverse)
                    {
                        loop.reverse_loop();
                    }

                    LoopNode node;
                    node.loop = std::move(loop);
                    node.polygon = poly;
                    node.shell_idx = shell_idx;
                    node.is_hole = is_hole;
                    all_loops.push_back(std::move(node));
                };

                // Use Athena (skeletal trapezoidation) to generate interlocking shells.
                // Athena naturally handles narrow channels by computing how many beads
                // fit at each cross-section. No custom overlap detection needed.
                //
                // The spacing overrides control the interlocking pattern:
                //   external_spacing = adjacent or gapped (alternates by layer parity)
                //   internal_spacing = gapped (always)
                //   innermost_spacing = mirrors opposite layer's external spacing
                // Athena positions shell 0's center at bead_width_0/2 from the outline.
                // No pre-offset needed - sparse_regions IS the boundary.
                Polygons inset_outline = to_polygons(sparse_regions);
                if (inset_outline.empty())
                    continue;

                // bead_width_0 = pw for BOTH layers so Athena's skeleton makes identical
                // decisions. Even layer shell 0 is generated separately via polygon offset.
                const coord_t bead_width_0 = perimeter_scaled_width;
                Athena::WallToolPaths wall_tool_paths(
                    inset_outline,
                    bead_width_0,                      // bead_width_0: shell 0 position
                    internal_spacing,                  // bead_width_x: internal shell spacing
                    size_t(actual_shells),             // inset_count
                    0,                                 // wall_0_inset
                    layerm->layer()->height,           // layer_height
                    this->object()->config(),          // print_object_config
                    this->object()->print()->config(), // print_config
                    perimeter_scaled_width,            // fixed_width_0: all shells use perimeter width
                    perimeter_scaled_width,            // fixed_width_x: all shells use perimeter width
                    external_spacing,                  // spacing_0: gap between shell 0 and shell 1 (alternates)
                    internal_spacing,                  // spacing_x: internal spacing override
                    innermost_spacing,                 // spacing_innermost: innermost spacing override
                    (int) this->id(),                  // layer_id
                    1.0,                               // min_bead_width_factor: no compression for interlocking
                    10000);                            // thin_wall_snap_precision

                const std::vector<Athena::VariableWidthLines> &toolpaths = wall_tool_paths.generate();

                // Convert Athena's variable-width toolpaths to InterlockingPerimeter extrusions
                ExPolygons last_shell_area;

                // Even layer shell 0: generate ~146% boundary bead via polygon offset
                // instead of using Athena's shell 0 (which is at pw/2 for skeleton consistency).
                // This places the boundary bead at boundary_w/2 from the outline.
                if (!is_odd_layer)
                {
                    Polygons boundary_shell = offset(inset_outline, -float(boundary_w) / 2);
                    for (const Polygon &bp : boundary_shell)
                    {
                        if (bp.size() >= 3)
                            collect_loop(bp, 1.0, 0, false);
                    }
                }

                for (size_t inset_idx = 0; inset_idx < toolpaths.size(); ++inset_idx)
                {
                    // Cap at requested shell count - Athena may generate extra beads
                    if (inset_idx >= size_t(actual_shells))
                        break;

                    size_t shell_idx = inset_idx;

                    // Skip Athena's shell 0 on even layers - we generated it above
                    if (!is_odd_layer && inset_idx == 0)
                        continue;

                    const Athena::VariableWidthLines &lines = toolpaths[inset_idx];

                    for (const Athena::ExtrusionLine &line : lines)
                    {
                        if (line.empty() || line.size() < 2)
                            continue;

                        // Skip thin wall fills and open paths - interlocking only needs closed loops
                        if (line.is_odd || !line.is_closed)
                            continue;

                        // Convert Athena ExtrusionLine to a Polygon for collect_loop
                        Polygon poly;
                        for (const Athena::ExtrusionJunction &j : line.junctions)
                            poly.points.push_back(j.p);

                        if (poly.size() < 3)
                            continue;

                        // Odd layer innermost shell is the ~146% boundary bead.
                        // Shift it inward by boundary_shift so its outer edge aligns
                        // with the 100% bead's outer edge on the even layer.
                        if (is_odd_layer && shell_idx == size_t(actual_shells) - 1)
                        {
                            Polygons shifted = offset(Polygons{poly}, -float(boundary_shift));
                            if (!shifted.empty())
                                poly = shifted.front();
                        }

                        collect_loop(poly, 1.0, shell_idx, false);
                    }
                }

                // Compute infill boundary from actual innermost shell centerlines rather
                // than Athena's getInnerContour(), which doesn't account for boundary bead
                // repositioning (even shell 0 separate, odd innermost offset).
                {
                    Polygons innermost_shells;
                    for (const LoopNode &node : all_loops)
                        if (node.shell_idx == size_t(actual_shells) - 1)
                            innermost_shells.push_back(node.polygon);
                    if (!innermost_shells.empty())
                        last_shell_area = union_ex(innermost_shells);
                }

                // Build containment tree: for each loop, find smallest containing parent OF SAME TYPE
                // Critical: only match holes with holes, contours with contours
                // Otherwise outer boundary would be detected as parent of hole shells
                for (size_t i = 0; i < all_loops.size(); ++i)
                {
                    Point test_point = all_loops[i].polygon.points.empty() ? Point(0, 0)
                                                                           : all_loops[i].polygon.points.front();
                    double smallest_area = std::numeric_limits<double>::max();
                    size_t best_parent = SIZE_MAX;

                    for (size_t j = 0; j < all_loops.size(); ++j)
                    {
                        if (i == j)
                            continue;
                        // Only consider same-type loops as potential parents
                        if (all_loops[j].is_hole != all_loops[i].is_hole)
                            continue;
                        if (all_loops[j].polygon.contains(test_point))
                        {
                            double area = std::abs(all_loops[j].polygon.area());
                            if (area < smallest_area)
                            {
                                smallest_area = area;
                                best_parent = j;
                            }
                        }
                    }
                    all_loops[i].parent = best_parent;
                    if (best_parent != SIZE_MAX)
                        all_loops[best_parent].children.push_back(i);
                }

                // Find root nodes (no parent), separated by type
                // Contour roots first (outer boundary), then hole roots
                // This ensures interlocking starts at outer boundary where normal perimeters ended
                std::vector<size_t> contour_roots;
                std::vector<size_t> hole_roots;
                for (size_t i = 0; i < all_loops.size(); ++i)
                {
                    if (all_loops[i].parent == SIZE_MAX)
                    {
                        if (all_loops[i].is_hole)
                            hole_roots.push_back(i);
                        else
                            contour_roots.push_back(i);
                    }
                }

                // Get last position for nearest-neighbor ordering
                Point last_pos = Point::Zero();

                // Collect a subtree into a vector (for potential reversal)
                std::function<void(size_t, std::vector<size_t> &)> collect_subtree =
                    [&](size_t idx, std::vector<size_t> &collected)
                {
                    collected.push_back(idx);
                    for (size_t child_idx : all_loops[idx].children)
                    {
                        collect_subtree(child_idx, collected);
                    }
                };

                // Output a list of loop indices, updating last_pos
                auto output_loops = [&](const std::vector<size_t> &indices)
                {
                    for (size_t idx : indices)
                    {
                        interlocking_collection.append(std::move(all_loops[idx].loop));
                        if (!all_loops[idx].polygon.points.empty())
                            last_pos = all_loops[idx].polygon.points.back();
                    }
                };

                // Process a subtree: collect all nodes, reverse if hole region, then output
                // Contour regions: outside-in (shell 0 near boundary → shell N toward center)
                // Hole regions: inside-out (shell 0 near hole → shell N away from hole)
                // Since containment gives outside-in order, we reverse for holes
                auto process_subtree = [&](size_t root_idx)
                {
                    std::vector<size_t> subtree;
                    collect_subtree(root_idx, subtree);

                    // Check if this is a hole region (root node is a hole)
                    bool is_hole_region = all_loops[root_idx].is_hole;

                    if (is_hole_region)
                    {
                        // Reverse for inside-out order (closest to hole first)
                        std::reverse(subtree.begin(), subtree.end());
                    }

                    output_loops(subtree);
                };

                // Helper to process a list of roots by nearest neighbor
                auto process_roots_nearest_neighbor = [&](std::vector<size_t> &roots)
                {
                    std::vector<bool> root_used(roots.size(), false);
                    for (size_t n = 0; n < roots.size(); ++n)
                    {
                        double best_dist = std::numeric_limits<double>::max();
                        size_t best_idx = 0;
                        for (size_t r = 0; r < roots.size(); ++r)
                        {
                            if (root_used[r])
                                continue;
                            Point root_start = all_loops[roots[r]].polygon.points.empty()
                                                   ? Point(0, 0)
                                                   : all_loops[roots[r]].polygon.points.front();
                            double dist = (root_start - last_pos).cast<double>().squaredNorm();
                            if (dist < best_dist)
                            {
                                best_dist = dist;
                                best_idx = r;
                            }
                        }
                        root_used[best_idx] = true;
                        process_subtree(roots[best_idx]);
                    }
                };

                // Process contour roots first (outer boundary - nozzle is already there after normal perimeters)
                // Then process hole roots (nearest neighbor from where we ended)
                process_roots_nearest_neighbor(contour_roots);
                process_roots_nearest_neighbor(hole_roots);

                // Calculate inner_area from the last shell result (for infill boundary)
                // last_shell_area represents the area after the innermost shell center was placed.
                // Offset inward by (half_width - infill_overlap) to create the infill boundary:
                //   - At 0% overlap: offset = half_width (infill starts at inner edge of shell)
                //   - At 100% overlap: offset = half_width - width = -half_width (infill extends to outer edge of shell)
                // This controls overlap between innermost INTERLOCKING shell and sparse INFILL,
                // which is separate from the outer boundary overlap (perimeter-to-interlocking).
                const coord_t infill_boundary_offset = half_width - infill_overlap_amount;
                Polygons inner_area;
                for (const ExPolygon &last_shell : last_shell_area)
                {
                    // Offset contour inward to create sparse infill boundary
                    Polygons inward = offset(last_shell.contour, -float(infill_boundary_offset));
                    if (last_shell.holes.empty())
                    {
                        append(inner_area, inward);
                    }
                    else
                    {
                        // Offset holes outward to create exclusion zones around hole-interlocking perimeters
                        // Note: Holes have CW orientation, so NEGATIVE offset expands them into material area
                        Polygons expanded_holes = offset(last_shell.holes, -float(infill_boundary_offset));
                        // Subtract expanded holes from the inward-offset contour
                        Polygons result = diff(inward, expanded_holes);
                        append(inner_area, result);
                    }
                }

                // Interlocking perimeters are actual perimeters, not infill. They should be in m_perimeters
                // to enable proper Layer Context API queries and processing.
                // CRITICAL: Perimeters structure is Collection-of-Collections, must wrap in ExtrusionEntityCollection
                if (!interlocking_collection.empty())
                {
                    // Containment tree already ordered loops outside-in per region
                    // with nearest-neighbor between roots. Do NOT re-sort.

                    // Wrap interlocking perimeters in collection (perimeters require nested collections)
                    ExtrusionEntityCollection *perimeter_collection = new ExtrusionEntityCollection();
                    perimeter_collection->no_sort = true; // Preserve the chained order during GCode generation
                    perimeter_collection->entities = std::move(interlocking_collection.entities);

                    // CRITICAL: Islands have non-contiguous indices in m_perimeters. If we APPEND the interlocking
                    // collection to the end and extend this island's range, we'll include other islands' perimeters.
                    //
                    // Example WITHOUT interlocking:
                    //   Island A: range [0, 1), perimeters at index 0
                    //   Island B: range [1, 2), perimeters at index 1
                    //   Island C: range [2, 3), perimeters at index 2
                    //
                    // If we APPEND Island A's interlocking to end (index 3) and set range to [0, 4):
                    //   Island A: range [0, 4) ← WRONG! Includes indices 1, 2, 3 (other islands' perimeters)
                    //
                    // SOLUTION: INSERT at the correct position (right after this island's existing perimeters)
                    // and shift all subsequent island ranges.

                    uint32_t old_begin = *island.perimeters.begin();
                    uint32_t old_end = *island.perimeters.end(); // One past last index

                    // Insert the interlocking collection right after this island's existing perimeters
                    // This shifts all subsequent indices by 1
                    auto insert_pos = layerm->m_perimeters.entities.begin() + old_end;
                    layerm->m_perimeters.entities.insert(insert_pos, perimeter_collection);

                    // Update THIS island's range to include the newly inserted interlocking
                    island.perimeters = LayerExtrusionRange(region_id, ExtrusionRange(old_begin, old_end + 1));

                    // Update ALL other islands' ranges on this layer that reference indices >= old_end
                    // because the insertion shifted their indices by 1
                    for (LayerSlice &other_lslice : this->lslices_ex)
                    {
                        for (LayerIsland &other_island : other_lslice.islands)
                        {
                            // Skip this island (already updated above)
                            if (&other_island == &island)
                                continue;

                            // Only update islands in the same region (they share the same m_perimeters)
                            if (other_island.perimeters.region() != region_id)
                                continue;

                            // If this island's range starts at or after the insertion point, shift it by 1
                            uint32_t other_begin = *other_island.perimeters.begin();
                            uint32_t other_end = *other_island.perimeters.end();

                            if (other_begin >= old_end)
                            {
                                // Shift the entire range by 1
                                other_island.perimeters = LayerExtrusionRange(region_id, ExtrusionRange(other_begin + 1,
                                                                                                        other_end + 1));
                            }
                        }
                    }
                }

                // inner_area was calculated above from innermost shell centerlines.
                // Use union_ex() to properly pair holes with their parent contours.
                // When shell count was reduced (area too narrow for all requested shells),
                // consume the entire sparse region - no sparse infill in partially-filled zones.
                ExPolygons updated_sparse_regions;
                if (actual_shells >= num_interlocking_shells)
                    updated_sparse_regions = union_ex(inner_area);

                // The interlocking perimeters have consumed the outer area of sparse regions for THIS island.
                // Update the stInternal surface_fill entries to remove the consumed area.
                // CRITICAL: We must update the global surface_fills by subtracting THIS island's consumed area.
                // Each island's interlocking consumption must be subtracted from the global sparse regions.
                // We do NOT touch stInternalSolid - it's preserved for top/bottom solid layers.

                // Calculate the area consumed by interlocking for THIS island
                // Use ORIGINAL sparse regions (with infill_overlap boundary from PerimeterGenerator)
                // not the adjusted sparse_regions (with pp_overlap boundary).
                // This ensures we subtract the entire interlocking zone from infill, including
                // the gap between infill_overlap and pp_overlap boundaries.
                ExPolygons consumed_by_interlocking = diff_ex(original_sparse_regions, updated_sparse_regions);

                // Subtract the consumed area from the global sparse surface_fills
                // This updates the sparse infill boundaries for THIS island
                for (SurfaceFill &sf : surface_fills)
                {
                    if (sf.region_id == region_id && sf.surface.surface_type == stInternal)
                    {
                        // Subtract the area consumed by THIS island's interlocking
                        sf.expolygons = diff_ex(sf.expolygons, consumed_by_interlocking);
                    }
                    // Note: stInternalSolid is NOT modified - preserved for solid layer settings
                }
            } // End island loop
        } // End lslice loop
    }

    // After interlocking has consumed sparse regions, remove any stInternal surfaces with ~0% density (0.001%).
    // These were only needed for interlocking boundary detection and should not proceed to infill generation.
    surface_fills.erase(std::remove_if(surface_fills.begin(), surface_fills.end(), [](const SurfaceFill &sf)
                                       { return sf.surface.surface_type == stInternal && sf.params.density < 0.01f; }),
                        surface_fills.end());

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    {
        static int iRun = 0;
        export_group_fills_to_svg(debug_out_path("Layer-fill_surfaces-10_fill-final-%d.svg", iRun++).c_str(),
                                  surface_fills);
    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Debug: dump surface_fills after interlocking consumption (what actually gets filled)
    dbg_fill_phase("PRE_FILL", *this, surface_fills);

    size_t first_object_layer_id = this->object()->get_layer(0)->id();
    // Each island's infill is generated and filled completely before moving to the next island,
    // eliminating chaotic back-and-forth travel caused by global generation + spatial division.

    for (LayerSlice &lslice : this->lslices_ex)
    {
        for (LayerIsland &island : lslice.islands)
        {
            const uint32_t region_id = island.perimeters.region();
            LayerRegion *layerm = this->get_region(region_id);

            // Process each surface type for THIS island
            for (SurfaceFill &surface_fill : surface_fills)
            {
                // Only process surfaces that match this island's region
                if (surface_fill.region_id != region_id)
                    continue;

                // Intersect surface fill with island boundary
                ExPolygons island_expolygons = intersection_ex(surface_fill.expolygons, ExPolygons{island.boundary});

                if (island_expolygons.empty())
                    continue;

                if (FILL_DEBUG)
                {
                    double isl_area = 0;
                    for (const ExPolygon &ep : island_expolygons)
                        isl_area += std::abs(ep.area());
                    BoundingBox ibb = get_extents(island_expolygons);
                    dbg_fill_print("z=%.3f [FILL] ISLAND_FILL type=%-18s ep=%zu area=%8.4fmm2 "
                                   "bbox=(%.2f,%.2f)-(%.2f,%.2f)\n",
                                   this->print_z, dbg_stype(surface_fill.surface.surface_type),
                                   island_expolygons.size(), isl_area * 1e-12, unscaled<double>(ibb.min.x()),
                                   unscaled<double>(ibb.min.y()), unscaled<double>(ibb.max.x()),
                                   unscaled<double>(ibb.max.y()));
                }

                // Create the filler object for this surface type
                std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
                f->set_bounding_box(bbox);
                // Layer ID is used for orienting the infill in alternating directions.
                // Layer::id() returns layer ID including raft layers, subtract them to make the infill direction independent
                // from raft.
                f->layer_id = this->id() - first_object_layer_id;
                f->z = this->print_z;
                f->angle = surface_fill.params.angle;
                f->overlap = surface_fill.params.overlap;
                f->adapt_fill_octree = (surface_fill.params.pattern == ipSupportCubic) ? support_fill_octree
                                                                                       : adaptive_fill_octree;
                f->print_config = &this->object()->print()->config();
                f->print_object_config = &this->object()->config();

                if (surface_fill.params.pattern == ipLightning)
                    dynamic_cast<FillLightning::Filler *>(f.get())->generator = lightning_generator;

                if (surface_fill.params.pattern == ipEnsuring)
                {
                    auto *fill_ensuring = dynamic_cast<FillEnsuring *>(f.get());
                    assert(fill_ensuring != nullptr);
                    fill_ensuring->print_region_config = &m_regions[surface_fill.region_id]->region().config();
                }

                // calculate flow spacing for infill pattern generation
                bool using_internal_flow = !surface_fill.surface.is_solid() && !surface_fill.params.bridge;
                double link_max_length = 0.;
                if (!surface_fill.params.bridge)
                {
#if 0
                    link_max_length = layerm->region().config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//                    printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
                    if (surface_fill.params.density > 80.) // 80%
                        link_max_length = 3. * f->spacing;
#endif
                }

                // Maximum length of the perimeter segment linking two infill lines.
                f->link_max_length = (coord_t) scale_(link_max_length);
                // Used by the concentric infill pattern to clip the loops to create extrusion paths.
                f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) *
                                           LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

                // apply half spacing using this flow's own spacing and generate infill
                FillParams params;
                params.density = float(0.01 * surface_fill.params.density);

                // At exactly 50% density, distance = min_spacing / 0.5 = min_spacing * 2.0 (exact integer multiple)
                // This creates perfectly aligned coordinates that trigger geometric degeneracies in Clipper2.
                // Treat 50.0% as 49.9% to avoid the exact 2x multiplier.
                // Applies to: ipConcentric (9) and ipEnsuring/Athena (20) which use heavy Clipper2 operations.
                if ((surface_fill.params.pattern == ipConcentric || surface_fill.params.pattern == ipEnsuring) &&
                    std::abs(params.density - 0.5f) < 0.0001f)
                {
                    params.density = 0.499f;
                }

                params.dont_adjust = false; //  surface_fill.params.dont_adjust;
                params.anchor_length = surface_fill.params.anchor_length;
                params.anchor_length_max = surface_fill.params.anchor_length_max;
                params.resolution = resolution;
                params.use_advanced_perimeters = ((perimeter_generator == PerimeterGeneratorType::Arachne ||
                                                   perimeter_generator == PerimeterGeneratorType::Athena) &&
                                                  surface_fill.params.pattern == ipConcentric) ||
                                                 surface_fill.params.pattern == ipEnsuring;
                params.perimeter_generator = perimeter_generator;
                params.layer_height = layerm->layer()->height;
                params.prefer_clockwise_movements = this->object()->print()->config().prefer_clockwise_movements;

                // Track fill range for this island and surface type
                uint32_t fill_begin = uint32_t(layerm->m_fills.entities.size());

                // An island may have multiple disconnected sparse regions (e.g., separated by interlocking).
                // Fill each ExPolygon completely before moving to the next to prevent chaotic jumping.

                // Create ONE collection for all fills in this island
                ExtrusionEntityCollection *eec = new ExtrusionEntityCollection();

                // Initialize to perimeter endpoint - that's where the nozzle is when infill starts
                Point last_fill_pos = Point(0, 0);
                bool have_last_pos = false;

                // Get the last perimeter's endpoint as the initial starting position
                // For closed perimeter loops, first_point == last_point, which is where the nozzle
                // finishes after printing the perimeter (the seam position)
                if (!island.perimeters.empty())
                {
                    uint32_t last_perim_idx = *island.perimeters.end() - 1;
                    if (last_perim_idx < layerm->m_perimeters.entities.size())
                    {
                        const ExtrusionEntity *last_perim = layerm->m_perimeters.entities[last_perim_idx];
                        if (last_perim != nullptr)
                        {
                            last_fill_pos = last_perim->last_point();
                            have_last_pos = true;
                        }
                    }
                }

                // Monotonic fills must preserve their ant-colony sweep ordering within
                // each ExPolygon. Non-monotonic fills can be freely reordered across
                // ExPolygon boundaries for better travel optimization.
                const bool is_monotonic_fill = fill_type_monotonic(surface_fill.params.pattern);
                // Solid fills produce long connected zigzag polylines that must stay intact
                // per-ExPolygon; cross-fragment chaining corrupts traverse graph connections.
                const bool use_per_expolygon_path = is_monotonic_fill || surface_fill.params.density > 99.f;

                if (use_per_expolygon_path || params.use_advanced_perimeters)
                {
                    // MONOTONIC / ADVANCED PATH: per-ExPolygon entity creation preserves ordering
                    for (ExPolygon &expoly : island_expolygons)
                    {
                        f->spacing = surface_fill.params.spacing;
                        f->bounding_width = surface_fill.params.bridge ? surface_fill.params.flow.width()
                                                                       : surface_fill.params.spacing;
                        params.start_near = have_last_pos ? last_fill_pos : expoly.contour.centroid();

                        surface_fill.surface.expolygon = std::move(expoly);
                        Polylines polylines;
                        ThickPolylines thick_polylines;
                        try
                        {
                            if (params.use_advanced_perimeters)
                                thick_polylines = f->fill_surface_advanced(&surface_fill.surface, params);
                            else
                                polylines = f->fill_surface(&surface_fill.surface, params);
                        }
                        catch (InfillFailedException &)
                        {
                            dbg_fill_print("z=%.3f [FILL] FILL_EXCEPTION type=%-18s InfillFailedException!\n",
                                           this->print_z, dbg_stype(surface_fill.surface.surface_type));
                        }

                        if (!polylines.empty())
                        {
                            last_fill_pos = polylines.back().last_point();
                            have_last_pos = true;
                        }
                        else if (!thick_polylines.empty())
                        {
                            last_fill_pos = thick_polylines.back().last_point();
                            have_last_pos = true;
                        }

                        if (!polylines.empty() || !thick_polylines.empty())
                        {
                            double flow_mm3_per_mm = surface_fill.params.flow.mm3_per_mm();
                            double flow_width = surface_fill.params.flow.width();
                            if (using_internal_flow || surface_fill.params.bridge)
                            {
                            }
                            else
                            {
                                Flow new_flow = surface_fill.params.flow.with_spacing(float(f->spacing));
                                flow_mm3_per_mm = new_flow.mm3_per_mm();
                                flow_width = new_flow.width();
                            }

                            if (params.use_advanced_perimeters)
                            {
                                for (const ThickPolyline &thick_polyline : thick_polylines)
                                {
                                    Flow new_flow = surface_fill.params.bridge
                                                        ? surface_fill.params.flow
                                                        : surface_fill.params.flow.with_spacing(float(f->spacing));
                                    ExtrusionMultiPath multi_path = PerimeterGenerator::thick_polyline_to_multi_path(
                                        thick_polyline, surface_fill.params.extrusion_role, new_flow,
                                        scaled<float>(0.05), float(SCALED_EPSILON));
                                    if (!multi_path.empty())
                                    {
                                        if (multi_path.paths.front().first_point() ==
                                            multi_path.paths.back().last_point())
                                            eec->entities.emplace_back(new ExtrusionLoop(std::move(multi_path.paths)));
                                        else
                                            eec->entities.emplace_back(new ExtrusionMultiPath(std::move(multi_path)));
                                    }
                                }
                            }
                            else
                            {
                                extrusion_entities_append_paths(
                                    eec->entities, std::move(polylines),
                                    ExtrusionAttributes{surface_fill.params.extrusion_role,
                                                        ExtrusionFlow{flow_mm3_per_mm, float(flow_width),
                                                                      surface_fill.params.flow.height()},
                                                        f->is_self_crossing()},
                                    !params.prefer_clockwise_movements);
                            }
                        }
                    }
                    // Entity-level reorder (safe for monotonic - respects can_reverse)
                    if (eec->entities.size() > 1)
                    {
                        const Point *start = have_last_pos ? &last_fill_pos : nullptr;
                        chain_and_reorder_extrusion_entities(eec->entities, start);
                    }
                }
                else
                {
                    // NON-MONOTONIC PATH: batch polylines across fragments for cross-fragment chaining.
                    // Tag each polyline with its originating ExPolygon's flow for correct extrusion.
                    struct PolylineFlowTag
                    {
                        double mm3_per_mm;
                        float width;
                        bool self_crossing;
                    };
                    Polylines all_polylines;
                    std::vector<PolylineFlowTag> flow_tags;

                    for (ExPolygon &expoly : island_expolygons)
                    {
                        f->spacing = surface_fill.params.spacing;
                        f->bounding_width = surface_fill.params.bridge ? surface_fill.params.flow.width()
                                                                       : surface_fill.params.spacing;
                        params.start_near = have_last_pos ? last_fill_pos : expoly.contour.centroid();

                        surface_fill.surface.expolygon = std::move(expoly);
                        Polylines polylines;
                        try
                        {
                            polylines = f->fill_surface(&surface_fill.surface, params);
                        }
                        catch (InfillFailedException &)
                        {
                            dbg_fill_print("z=%.3f [FILL] FILL_EXCEPTION type=%-18s InfillFailedException!\n",
                                           this->print_z, dbg_stype(surface_fill.surface.surface_type));
                        }

                        if (!polylines.empty())
                        {
                            last_fill_pos = polylines.back().last_point();
                            have_last_pos = true;

                            // Compute this ExPolygon's adjusted flow
                            double ep_mm3 = surface_fill.params.flow.mm3_per_mm();
                            float ep_width = float(surface_fill.params.flow.width());
                            if (!using_internal_flow && !surface_fill.params.bridge)
                            {
                                Flow adj = surface_fill.params.flow.with_spacing(float(f->spacing));
                                ep_mm3 = adj.mm3_per_mm();
                                ep_width = float(adj.width());
                            }
                            bool ep_self_crossing = f->is_self_crossing();

                            // Tag each polyline with its origin ExPolygon's flow
                            for (size_t i = 0; i < polylines.size(); ++i)
                                flow_tags.push_back({ep_mm3, ep_width, ep_self_crossing});
                            append(all_polylines, std::move(polylines));
                        }
                    }

                    // Chain all polylines across ExPolygon fragment boundaries with 2-opt,
                    // using index tracking to preserve per-polyline flow tags.
                    if (!all_polylines.empty())
                    {
                        const Point *chain_start = have_last_pos ? &last_fill_pos : nullptr;
                        auto [chained, index_map] = chain_polylines_with_indices(std::move(all_polylines), chain_start);

                        // Reorder flow tags to match the chained polyline order
                        std::vector<PolylineFlowTag> reordered_tags;
                        reordered_tags.reserve(index_map.size());
                        for (size_t orig_idx : index_map)
                            reordered_tags.push_back(flow_tags[orig_idx]);

                        // Convert to extrusion entities with correct per-polyline flow
                        for (size_t i = 0; i < chained.size(); ++i)
                        {
                            if (chained[i].size() < 2)
                                continue;
                            const auto &tag = reordered_tags[i];
                            ExtrusionAttributes attrs{surface_fill.params.extrusion_role,
                                                      ExtrusionFlow{tag.mm3_per_mm, tag.width,
                                                                    surface_fill.params.flow.height()},
                                                      tag.self_crossing};
                            // !prefer_clockwise_movements -> can_reverse=true -> ExtrusionPath
                            // prefer_clockwise_movements -> can_reverse=false -> ExtrusionPathOriented
                            if (!params.prefer_clockwise_movements)
                                eec->entities.emplace_back(new ExtrusionPath(std::move(chained[i]), attrs));
                            else
                                eec->entities.emplace_back(new ExtrusionPathOriented(std::move(chained[i]), attrs));
                        }
                    }
                }

                // Add the collection to the layer (if it has any fills)
                if (!eec->empty())
                {
                    dbg_fill_print("z=%.3f [FILL] FILL_OK type=%-18s entities=%zu\n", this->print_z,
                                   dbg_stype(surface_fill.surface.surface_type), eec->entities.size());
                    layerm->m_fills.entities.push_back(eec);
                }
                else
                {
                    dbg_fill_print("z=%.3f [FILL] FILL_EMPTY type=%-18s (no extrusions generated)\n", this->print_z,
                                   dbg_stype(surface_fill.surface.surface_type));
                    delete eec;
                }

                uint32_t fill_end = uint32_t(layerm->m_fills.entities.size());

                // Direct assignment to THIS island (no spatial search needed)
                if (fill_end > fill_begin)
                {
                    island.add_fill_range(LayerExtrusionRange{region_id, {fill_begin, fill_end}});
                }
            }
        }
    }

    for (LayerSlice &lslice : this->lslices_ex)
        for (LayerIsland &island : lslice.islands)
        {
            if (!island.thin_fills.empty())
            {
                // Copy thin fills into fills packed as a collection.
                // Fills are always stored as collections, the rest of the pipeline (wipe into infill, G-code generator) relies on it.
                LayerRegion &layerm = *this->get_region(island.perimeters.region());
                ExtrusionEntityCollection &collection = *(new ExtrusionEntityCollection());
                layerm.m_fills.entities.push_back(&collection);
                collection.entities.reserve(island.thin_fills.size());
                for (uint32_t fill_id : island.thin_fills)
                    collection.entities.push_back(layerm.thin_fills().entities[fill_id]->clone());
                island.add_fill_range(
                    {island.perimeters.region(),
                     {uint32_t(layerm.m_fills.entities.size() - 1), uint32_t(layerm.m_fills.entities.size())}});
            }
            // Sort the fills by region ID.
            std::sort(island.fills.begin(), island.fills.end(), [](auto &l, auto &r)
                      { return l.region() < r.region() || (l.region() == r.region() && *l.begin() < *r.begin()); });
            // Compress continuous fill ranges of the same region.
            {
                size_t k = 0;
                for (size_t i = 0; i < island.fills.size();)
                {
                    uint32_t region_id = island.fills[i].region();
                    uint32_t begin = *island.fills[i].begin();
                    uint32_t end = *island.fills[i].end();
                    size_t j = i + 1;
                    for (; j < island.fills.size() && island.fills[j].region() == region_id &&
                           *island.fills[j].begin() == end;
                         ++j)
                        end = *island.fills[j].end();
                    island.fills[k++] = {region_id, {begin, end}};
                    i = j;
                }
                island.fills.erase(island.fills.begin() + k, island.fills.end());
            }
        }

#ifndef NDEBUG
    for (LayerRegion *layerm : m_regions)
        for (const ExtrusionEntity *e : layerm->fills())
            assert(dynamic_cast<const ExtrusionEntityCollection *>(e) != nullptr);
#endif
}

Polylines Layer::generate_sparse_infill_polylines_for_anchoring(FillAdaptive::Octree *adaptive_fill_octree,
                                                                FillAdaptive::Octree *support_fill_octree,
                                                                FillLightning::Generator *lightning_generator) const
{
    std::vector<SurfaceFill> surface_fills = group_fills(*this);
    const Slic3r::BoundingBox bbox = this->object()->bounding_box();
    const auto resolution = this->object()->print()->config().gcode_resolution.value;

    Polylines sparse_infill_polylines{};

    for (SurfaceFill &surface_fill : surface_fills)
    {
        if (surface_fill.surface.surface_type != stInternal)
        {
            continue;
        }

        switch (surface_fill.params.pattern)
        {
        case ipCount:
            continue;
            break;
        case ipSupportBase:
            continue;
            break;
        case ipEnsuring:
            continue;
            break;
        case ipLightning:
        case ipAdaptiveCubic:
        case ipSupportCubic:
        case ipRectilinear:
        case ipMonotonic:
        case ipMonotonicLines:
        case ipAlignedRectilinear:
        case ipGrid:
        case ipTriangles:
        case ipStars:
        case ipCubic:
        case ipLine:
        case ipConcentric:
        case ipHoneycomb:
        case ip3DHoneycomb:
        case ipGyroid:
        case ipHilbertCurve:
        case ipArchimedeanChords:
        case ipOctagramSpiral:
        case ipZigZag:
            break;
        }

        // Create the filler object.
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
        f->layer_id = this->id() - this->object()->get_layer(0)->id(); // We need to subtract raft layers.
        f->z = this->print_z;
        f->angle = surface_fill.params.angle;
        f->overlap = surface_fill.params.overlap;
        f->adapt_fill_octree = (surface_fill.params.pattern == ipSupportCubic) ? support_fill_octree
                                                                               : adaptive_fill_octree;
        f->print_config = &this->object()->print()->config();
        f->print_object_config = &this->object()->config();

        if (surface_fill.params.pattern == ipLightning)
            dynamic_cast<FillLightning::Filler *>(f.get())->generator = lightning_generator;

        // calculate flow spacing for infill pattern generation
        double link_max_length = 0.;
        if (!surface_fill.params.bridge)
        {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
            if (surface_fill.params.density > 80.) // 80%
                link_max_length = 3. * f->spacing;
#endif
        }

        // Maximum length of the perimeter segment linking two infill lines.
        f->link_max_length = (coord_t) scale_(link_max_length);
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) *
                                   LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

        LayerRegion &layerm = *m_regions[surface_fill.region_id];

        // apply half spacing using this flow's own spacing and generate infill
        FillParams params;
        params.density = float(0.01 * surface_fill.params.density);

        // At exactly 50% density, distance = min_spacing / 0.5 = min_spacing * 2.0 (exact integer multiple)
        // This creates perfectly aligned coordinates that trigger geometric degeneracies in Clipper2.
        // Treat 50.0% as 49.9% to avoid the exact 2x multiplier.
        // Applies to: ipConcentric (9) and ipEnsuring/Athena (20) which use heavy Clipper2 operations.
        if ((surface_fill.params.pattern == ipConcentric || surface_fill.params.pattern == ipEnsuring) &&
            std::abs(params.density - 0.5f) < 0.0001f)
        {
            params.density = 0.499f;
        }

        params.dont_adjust = false; //  surface_fill.params.dont_adjust;
        params.anchor_length = surface_fill.params.anchor_length;
        params.anchor_length_max = surface_fill.params.anchor_length_max;
        params.resolution = resolution;
        params.use_advanced_perimeters = false;
        params.layer_height = layerm.layer()->height;

        Point last_fill_pos = Point(0, 0);
        bool have_last_pos = false;

        for (ExPolygon &expoly : surface_fill.expolygons)
        {
            // Spacing is modified by the filler to indicate adjustments. Reset it for each expolygon.
            f->spacing = surface_fill.params.spacing;
            // For bridges: use original flow width so boundary offset is independent of line spacing
            // For non-bridges: use spacing (normal behavior)
            f->bounding_width = surface_fill.params.bridge ? surface_fill.params.flow.width()
                                                           : surface_fill.params.spacing;

            if (have_last_pos)
            {
                params.start_near = last_fill_pos;
            }
            else
            {
                params.start_near = expoly.contour.centroid();
            }

            surface_fill.surface.expolygon = std::move(expoly);
            try
            {
                Polylines polylines = f->fill_surface(&surface_fill.surface, params);
                if (!polylines.empty())
                {
                    last_fill_pos = polylines.back().last_point();
                    have_last_pos = true;
                }
                sparse_infill_polylines.insert(sparse_infill_polylines.end(), polylines.begin(), polylines.end());
            }
            catch (InfillFailedException &)
            {
            }
        }
    }

    return sparse_infill_polylines;
}

// Create ironing extrusions over top surfaces.
void Layer::make_ironing()
{
    // LayerRegion::slices contains surfaces marked with SurfaceType.
    // Here we want to collect top surfaces extruded with the same extruder.
    // A surface will be ironed with the same extruder to not contaminate the print with another material leaking from the nozzle.

    // First classify regions based on the extruder used.
    struct IroningParams
    {
        int extruder = -1;
        bool just_infill = false;
        // Spacing of the ironing lines, also to calculate the extrusion flow from.
        double line_spacing;
        // Height of the extrusion, to calculate the extrusion flow from.
        double height;
        double speed;
        double angle;

        bool operator<(const IroningParams &rhs) const
        {
            if (this->extruder < rhs.extruder)
                return true;
            if (this->extruder > rhs.extruder)
                return false;
            if (int(this->just_infill) < int(rhs.just_infill))
                return true;
            if (int(this->just_infill) > int(rhs.just_infill))
                return false;
            if (this->line_spacing < rhs.line_spacing)
                return true;
            if (this->line_spacing > rhs.line_spacing)
                return false;
            if (this->height < rhs.height)
                return true;
            if (this->height > rhs.height)
                return false;
            if (this->speed < rhs.speed)
                return true;
            if (this->speed > rhs.speed)
                return false;
            if (this->angle < rhs.angle)
                return true;
            if (this->angle > rhs.angle)
                return false;
            return false;
        }

        bool operator==(const IroningParams &rhs) const
        {
            return this->extruder == rhs.extruder && this->just_infill == rhs.just_infill &&
                   this->line_spacing == rhs.line_spacing && this->height == rhs.height && this->speed == rhs.speed &&
                   this->angle == rhs.angle;
        }

        LayerRegion *layerm;
        uint32_t region_id;

        // IdeaMaker: ironing
        // ironing flowrate (5% percent)
        // ironing speed (10 mm/sec)

        // Kisslicer:
        // iron off, Sweep, Group
        // ironing speed: 15 mm/sec

        // Cura:
        // Pattern (zig-zag / concentric)
        // line spacing (0.1mm)
        // flow: from normal layer height. 10%
        // speed: 20 mm/sec
    };

    std::vector<IroningParams> by_extruder;
    double default_layer_height = this->object()->config().layer_height;

    for (uint32_t region_id = 0; region_id < uint32_t(this->regions().size()); ++region_id)
        if (LayerRegion *layerm = this->get_region(region_id); !layerm->slices().empty())
        {
            IroningParams ironing_params;
            const PrintRegionConfig &config = layerm->region().config();
            if (config.ironing && (config.ironing_type == IroningType::AllSolid ||
                                   (config.top_solid_layers > 0 && (config.ironing_type == IroningType::TopSurfaces ||
                                                                    (config.ironing_type == IroningType::TopmostOnly &&
                                                                     layerm->layer()->upper_layer == nullptr)))))
            {
                if (config.perimeter_extruder == config.solid_infill_extruder || config.perimeters == 0)
                {
                    // Iron the whole face.
                    ironing_params.extruder = config.solid_infill_extruder;
                }
                else
                {
                    // Iron just the infill.
                    ironing_params.extruder = config.solid_infill_extruder;
                }
            }
            if (ironing_params.extruder != -1)
            {
                //TODO just_infill is currently not used.
                ironing_params.just_infill = false;
                ironing_params.line_spacing = config.ironing_spacing;
                ironing_params.height = default_layer_height * 0.01 * config.ironing_flowrate;
                ironing_params.speed = config.ironing_speed;
                ironing_params.angle = config.fill_angle * M_PI / 180.;
                ironing_params.layerm = layerm;
                ironing_params.region_id = region_id;
                by_extruder.emplace_back(ironing_params);
            }
        }
    std::sort(by_extruder.begin(), by_extruder.end());

    FillRectilinear fill;
    FillParams fill_params;
    fill.set_bounding_box(this->object()->bounding_box());
    // Layer ID is used for orienting the infill in alternating directions.
    // Layer::id() returns layer ID including raft layers, subtract them to make the infill direction independent
    // from raft.
    //FIXME ironing does not take fill angle into account. Shall it? Does it matter?
    fill.layer_id = this->id() - this->object()->get_layer(0)->id();
    fill.z = this->print_z;
    fill.overlap = 0;
    fill_params.density = 1.;
    fill_params.monotonic = true;

    for (size_t i = 0; i < by_extruder.size();)
    {
        // Find span of regions equivalent to the ironing operation.
        IroningParams &ironing_params = by_extruder[i];
        size_t j = i;
        for (++j; j < by_extruder.size() && ironing_params == by_extruder[j]; ++j)
            ;

        // Create the ironing extrusions for regions <i, j)
        ExPolygons ironing_areas;
        double nozzle_dmr = this->object()->print()->config().nozzle_diameter.values[ironing_params.extruder - 1];
        if (ironing_params.just_infill)
        {
            //TODO just_infill is currently not used.
            // Just infill.
        }
        else
        {
            // Infill and perimeter.
            // Merge top surfaces with the same ironing parameters.
            Polygons polys;
            Polygons infills;
            for (size_t k = i; k < j; ++k)
            {
                const IroningParams &ironing_params = by_extruder[k];
                const PrintRegionConfig &region_config = ironing_params.layerm->region().config();
                bool iron_everything = region_config.ironing_type == IroningType::AllSolid;
                bool iron_completely = iron_everything;
                if (iron_everything)
                {
                    // Check whether there is any non-solid hole in the regions.
                    bool internal_infill_solid = region_config.fill_density.value > 95.;
                    for (const Surface &surface : ironing_params.layerm->fill_surfaces())
                        if ((!internal_infill_solid && surface.surface_type == stInternal) ||
                            surface.surface_type == stInternalBridge || surface.surface_type == stInternalVoid)
                        {
                            // Some fill region is not quite solid. Don't iron over the whole surface.
                            iron_completely = false;
                            break;
                        }
                }
                if (iron_completely)
                {
                    // Iron everything. This is likely only good for solid transparent objects.
                    for (const Surface &surface : ironing_params.layerm->slices())
                        polygons_append(polys, surface.expolygon);
                }
                else
                {
                    for (const Surface &surface : ironing_params.layerm->slices())
                        if (surface.surface_type == stTop || (iron_everything && surface.surface_type == stBottom))
                            // stBottomBridge is not being ironed on purpose, as it would likely destroy the bridges.
                            polygons_append(polys, surface.expolygon);
                }
                if (iron_everything && !iron_completely)
                {
                    // Add solid fill surfaces. This may not be ideal, as one will not iron perimeters touching these
                    // solid fill surfaces, but it is likely better than nothing.
                    for (const Surface &surface : ironing_params.layerm->fill_surfaces())
                        if (surface.surface_type == stInternalSolid)
                            polygons_append(infills, surface.expolygon);
                }
            }

            if (!infills.empty() || j > i + 1)
            {
                // Ironing over more than a single region or over solid internal infill.
                if (!infills.empty())
                    // For IroningType::AllSolid only:
                    // Add solid infill areas for layers, that contain some non-ironable infil (sparse infill, bridge infill).
                    append(polys, std::move(infills));
                polys = union_safety_offset(polys);
            }
            // Trim the top surfaces with half the nozzle diameter.
            ironing_areas = intersection_ex(polys, offset(this->lslices, -float(scale_(0.5 * nozzle_dmr))));
        }

        // Create the filler object.
        fill.spacing = ironing_params.line_spacing;
        fill.angle = float(ironing_params.angle + 0.25 * M_PI);
        fill.link_max_length = (coord_t) scale_(3. * fill.spacing);
        double extrusion_height = ironing_params.height * fill.spacing / nozzle_dmr;
        float extrusion_width = Flow::rounded_rectangle_extrusion_width_from_spacing(float(nozzle_dmr),
                                                                                     float(extrusion_height));
        double flow_mm3_per_mm = nozzle_dmr * extrusion_height;
        Surface surface_fill(stTop, ExPolygon());
        for (ExPolygon &expoly : ironing_areas)
        {
            surface_fill.expolygon = std::move(expoly);
            Polylines polylines;
            try
            {
                assert(!fill_params.use_advanced_perimeters);
                polylines = fill.fill_surface(&surface_fill, fill_params);
            }
            catch (InfillFailedException &)
            {
            }
            if (!polylines.empty())
            {
                // Save into layer.
                auto fill_begin = uint32_t(ironing_params.layerm->fills().size());
                ExtrusionEntityCollection *eec = nullptr;
                ironing_params.layerm->m_fills.entities.push_back(eec = new ExtrusionEntityCollection());
                // Don't sort the ironing infill lines as they are monotonicly ordered.
                eec->no_sort = true;
                extrusion_entities_append_paths(eec->entities, std::move(polylines),
                                                ExtrusionAttributes{ExtrusionRole::Ironing,
                                                                    ExtrusionFlow{flow_mm3_per_mm, extrusion_width,
                                                                                  float(extrusion_height)}});
                insert_fills_into_islands(*this, ironing_params.region_id, fill_begin,
                                          uint32_t(ironing_params.layerm->fills().size()));
            }
        }

        // Regions up to j were processed.
        i = j;
    }
}

} // namespace Slic3r
