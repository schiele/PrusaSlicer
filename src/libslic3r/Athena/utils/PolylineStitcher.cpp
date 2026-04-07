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
    // source_poly_id is tagged on ExtrusionLines but NOT gated here.
    // Gating on source_poly_id breaks 0-width contour markers, interlocking
    // perimeters, and gap-fill that must stitch across polygon boundaries.
    // Future use requires exempting markers and interlocking paths.
    return a.is_odd == b.is_odd;
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
