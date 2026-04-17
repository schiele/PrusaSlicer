///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/ Copyright (c) 2022 Ultimaker B.V. - CuraEngine
///|/
///|/ preFlight is based on PrusaSlicer and released under AGPLv3 or higher
///|/

#include "PolylineStitcher.hpp"

#include "ExtrusionLine.hpp"
#include "libslic3r/Athena/utils/PolygonsPointIndex.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r
{
namespace Athena
{
struct ExtrusionJunction;
} // namespace Athena
} // namespace Slic3r

namespace Slic3r::Athena
{

template<>
bool PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::canReverse(
    const PathsPointIndex<VariableWidthLines> &ppi)
{
    if ((*ppi.polygons)[ppi.poly_idx].is_odd)
        return true;
    else
        return false;
}

template<>
bool PolylineStitcher<Polygons, Polygon, Point>::canReverse(const PathsPointIndex<Polygons> &)
{
    return true;
}

template<>
bool PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::canConnect(const ExtrusionLine &a,
                                                                                        const ExtrusionLine &b)
{
    if (a.is_odd != b.is_odd)
        return false;

    // Don't stitch a 0-width marker line to a real-bead line. LimitedBeadingStrategy
    // inserts 0-width "fake" walls as infill-boundary markers that must remain pure
    // so separateOutInnerContour can identify them as contours. Mixing markers with
    // real beads produces a closed loop whose non-zero junctions get emitted as
    // extrusion across empty space (issue #105).
    auto is_marker = [](const ExtrusionLine &line) -> bool
    {
        if (line.junctions.empty())
            return false;
        for (const ExtrusionJunction &j : line.junctions)
            if (j.w != 0)
                return false;
        return true;
    };
    if (is_marker(a) != is_marker(b))
        return false;

    // source_poly_id is tagged on ExtrusionLines but NOT gated here. Gating on
    // source_poly_id breaks interlocking perimeters and gap-fill that must stitch
    // across polygon boundaries. Future use requires exempting those cases.
    return true;
}

template<>
bool PolylineStitcher<Polygons, Polygon, Point>::canConnect(const Polygon &, const Polygon &)
{
    return true;
}

template<>
bool PolylineStitcher<VariableWidthLines, ExtrusionLine, ExtrusionJunction>::isOdd(const ExtrusionLine &line)
{
    return line.is_odd;
}

template<>
bool PolylineStitcher<Polygons, Polygon, Point>::isOdd(const Polygon &)
{
    return false;
}

} // namespace Slic3r::Athena
