#pragma once

// Geometry queries and multi-entity builders used by the sketch tools (picking, snapping, previews,
// rectangles, tangent arcs). Qt-free so they can be unit tested.

#include "sketch/Sketch.h"

#include <array>
#include <optional>
#include <vector>

namespace bl::sketch {

struct P2 {
    double x = 0.0, y = 0.0;
};

inline P2 operator+(P2 a, P2 b) { return {a.x + b.x, a.y + b.y}; }
inline P2 operator-(P2 a, P2 b) { return {a.x - b.x, a.y - b.y}; }
inline P2 operator*(P2 a, double k) { return {a.x * k, a.y * k}; }
double dot(P2 a, P2 b);
double cross(P2 a, P2 b);
double length(P2 a);
P2 normalized(P2 a);   // (1, 0) for a zero vector
P2 perpendicular(P2 a); // rotated +90 degrees

P2 pointOf(const Sketch& s, Id point);
P2 centerOf(const Sketch& s, const Curve& c);
double radiusOf(const Sketch& s, const Curve& c); // circle or arc

// Arc angles: start angle and counter-clockwise sweep in (0, 2*pi].
double arcStart(const Sketch& s, const Curve& arc);
double arcSweep(const Sketch& s, const Curve& arc);
bool arcContains(const Sketch& s, const Curve& arc, double angle);

P2 closestOnCurve(const Sketch& s, const Curve& c, P2 p);
double distanceToCurve(const Sketch& s, const Curve& c, P2 p);
P2 midpointOf(const Sketch& s, const Curve& c); // line middle, arc middle, circle's rightmost point

// Polyline through the curve, at most `step` radians per segment for circles and arcs.
std::vector<P2> tessellate(const Sketch& s, const Curve& c, double step = 0.06);

// Unit direction in which a path leaves `curve` at its end point `point` (the direction a tangent
// continuation starts in). Nothing for circles or points that are not an end of the curve.
std::optional<P2> exitDirection(const Sketch& s, const Curve& curve, Id point);

// The arc that starts at `start` heading in `direction` and ends at `end`: its centre, and whether
// it turns counter-clockwise. Nothing when the end lies (nearly) straight ahead.
struct TangentArc {
    P2 center;
    bool ccw = true;
};
std::optional<TangentArc> tangentArc(P2 start, P2 direction, P2 end);

// Builders. Each adds the entities with the relations a user expects from the tool and returns the
// new curves.
//
// Corner rectangle with opposite corners a and c: lines bottom, right, top, left (as seen when a is
// the lower left), corners coincident, two horizontal and two vertical relations.
std::array<Id, 4> addRectangle(Sketch& s, P2 a, P2 c, bool construction = false);
// Centre rectangle: the four lines, two construction diagonals and a centre point at the middle of
// the first diagonal. Returns {bottom, right, top, left, diagonal1, diagonal2, centre point}.
std::array<Id, 7> addCenterRectangle(Sketch& s, P2 center, P2 corner, bool construction = false);

// Is the pair of lines parallel (within `tolerance` radians)?
bool parallel(const Sketch& s, const Curve& a, const Curve& b, double tolerance = 1e-6);
// Intersection of the infinite lines through two line curves.
std::optional<P2> lineIntersection(const Sketch& s, const Curve& a, const Curve& b);

} // namespace bl::sketch
