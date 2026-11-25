#pragma once

// Sketch editing operations behind the sketch tools (PLAN.md 6.6): intersections, trim, extend,
// sketch fillet, slot, polygon, offset, mirror; the region finder that turns a sketch into closed
// profiles for features; and the Sketch Doctor checks. All work on the plain sketch model, so they
// can be tested without a viewer. Operations that cannot apply return false / nothing and leave the
// sketch as it was.

#include "sketch/Geometry.h"

#include <optional>
#include <string>
#include <vector>

namespace bl::sketch {

// --- Intersections ------------------------------------------------------------------------------

// Position along a curve: lines 0..1 from p1 to p2 (unclamped), arcs the angle from the start
// (0..sweep), circles the angle from +x (0..2 pi).
double parameterOf(const Sketch& s, const Curve& c, P2 p);
P2 pointAt(const Sketch& s, const Curve& c, double t);
// Length of the parameter range: 1 for lines, the sweep for arcs, 2 pi for circles.
double parameterEnd(const Sketch& s, const Curve& c);

// Points where two curves cross or touch, within both curves' extents. `unboundedA`: treat `a` as
// an infinite line / full circle (for Extend).
std::vector<P2> intersections(const Sketch& s, const Curve& a, const Curve& b, bool unboundedA = false);

// --- Editing --------------------------------------------------------------------------------------

// Power trim: removes the piece of `curve` around `pick` up to the nearest crossings with other
// curves, or the whole curve when nothing crosses it. A line or arc cut in its middle is split in
// two (held collinear / concentric); a circle becomes an arc. New ends get On Curve relations.
bool trim(Sketch& s, Id curve, P2 pick);

// Extends the end of a line or arc nearest `pick` to the next curve it would meet.
bool extend(Sketch& s, Id curve, P2 pick);

// Rounds the corner where two line ends meet (`corner` is either end point) with a tangent arc.
// The sharp corner is kept as a construction "virtual sharp" point that takes over the corner's
// relations and dimensions, so a fully defined sketch stays fully defined. Returns the arc.
std::optional<Id> filletCorner(Sketch& s, Id corner, double radius, bool addDimension = true);

// Straight slot between two centre points, `halfWidth` either side: two arcs, two tangent lines and
// a construction centerline. Returns {arc1, arc2, line1, line2, centerline}.
std::array<Id, 5> addSlot(Sketch& s, P2 center1, P2 center2, double halfWidth);

// Regular polygon with `sides` sides inscribed in a construction circle through `vertex`. Returns
// the sides followed by the circle.
std::vector<Id> addPolygon(Sketch& s, P2 center, P2 vertex, int sides);

// Copies of the curves `distance` away, joined where the originals were joined. Connected curves
// are offset as chains; a closed chain goes outwards for a positive distance. Each new line is held
// parallel to its original (with a distance dimension if `addDimensions`), each arc concentric.
// Returns the new curves (empty if the offset collapses).
std::vector<Id> offset(Sketch& s, const std::vector<Id>& curves, double distance, bool addDimensions = true);

// Mirrored copies of the curves (and standalone points) about the line `axis`, held symmetric.
std::vector<Id> mirror(Sketch& s, const std::vector<Id>& items, Id axis);

// --- Regions (profiles for features) -------------------------------------------------------------

// Part of a curve between two crossings, in the direction it is walked along a loop.
struct Piece {
    Id curve = 0;
    double t0 = 0.0, t1 = 0.0; // curve parameters at the start and end of the walk
    P2 start, end;
};
struct Loop {
    std::vector<Piece> pieces;
    std::vector<P2> polygon; // closed outline (last point not repeated), for area and drawing
    double area = 0.0;       // signed: positive counter-clockwise
};
// A closed area bounded by the sketch's non-construction curves: one outer loop (counter-
// clockwise) and the loops of islands inside it (clockwise). Overlapping contours are split into
// separate regions, as when picking a region for a feature.
struct Region {
    Loop outer;
    std::vector<Loop> holes;
    double area = 0.0;
};
struct Contours {
    std::vector<Region> regions;
    std::vector<P2> openEnds; // curve ends that do not join anything
};
Contours findRegions(const Sketch& s);
// Is `p` inside the region (inside the outer loop and outside every hole)?
bool regionContains(const Region& r, P2 p);

// --- Sketch Doctor -------------------------------------------------------------------------------

enum class IssueKind {
    Gap,         // two curve ends almost touching: repair joins them
    OpenEnd,     // a curve end that joins nothing
    ZeroLength,  // a line or arc too small to see: repair deletes it
    Duplicate,   // a curve drawn twice: repair deletes the copy
    Overlap,     // collinear lines lying partly on top of each other
    Crossing,    // contours crossing each other (features then need a picked region)
    OverDefined, // redundant or conflicting relations: repair deletes redundant ones
};
struct Issue {
    IssueKind kind = IssueKind::OpenEnd;
    std::vector<Id> ids; // entities involved (Gap: the two points; OverDefined: the constraints)
    P2 at;               // where to show it
    bool repairable = false;
};
const char* issueName(IssueKind kind);
// `gap`: largest distance between two curve ends still reported as a gap rather than open ends.
std::vector<Issue> diagnose(const Sketch& s, double gap);
bool repair(Sketch& s, const Issue& issue);

} // namespace bl::sketch
