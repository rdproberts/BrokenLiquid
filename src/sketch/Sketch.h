#pragma once

// 2D sketch data model (PLAN.md 6.6). Plain value types so a whole sketch can be copied into an
// undo snapshot. Coordinates are in mm in the sketch plane; angles in radians.
//
// Geometry is point-based: a line owns two points, a circle owns its centre (plus a radius), an
// arc owns centre, start and end points (counter-clockwise from start to end; the radius is the
// start-centre distance and the solver keeps the end on the same circle). Relations between
// curves therefore mostly become relations between points, and "coincident" is just two points
// held together.

#include <nlohmann/json_fwd.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bl::sketch {

using Id = int; // unique within a sketch, never reused

enum class CurveType { Line, Circle, Arc };

struct Point {
    Id id = 0;
    double x = 0.0, y = 0.0;
    int number = 0;      // per-kind creation count for the display name ("Point4")
    Id owner = 0;        // curve that owns this point (0: a standalone sketch point)
    bool fixed = false;  // not moved by the solver (the sketch origin, converted model geometry)
    bool construction = false;

    bool operator==(const Point&) const = default;
};

struct Curve {
    Id id = 0;
    CurveType type = CurveType::Line;
    int number = 0;      // per-kind creation count ("Line3", "Arc1")
    Id p1 = 0, p2 = 0;   // line endpoints / arc start and end
    Id center = 0;       // circle and arc
    double radius = 0.0; // circle only; an arc's radius follows its points
    bool construction = false;

    bool operator==(const Curve&) const = default;
};

enum class ConstraintType {
    // Relations
    Coincident,    // point, point
    Horizontal,    // line | point, point
    Vertical,      // line | point, point
    Parallel,      // line, line
    Perpendicular, // line, line
    Collinear,     // line, line
    Equal,         // line, line (length) | circle/arc, circle/arc (radius)
    Tangent,       // line, circle/arc | circle/arc, circle/arc
    Concentric,    // circle/arc, circle/arc
    Midpoint,      // point, line
    PointOnCurve,  // point, line/circle/arc
    Symmetric,     // point, point, line (mirror axis)
    Fix,           // point (held at fx, fy) | curve (all its points; circle radius too)
    // Dimensions (value in mm, or radians for Angle)
    Distance,           // point, point | line (length) | point, line | line, line (parallel)
    HorizontalDistance, // point, point
    VerticalDistance,   // point, point
    Angle,              // line, line
    Radius,             // circle/arc
    Diameter,           // circle/arc
};

bool isDimension(ConstraintType type);
const char* typeName(ConstraintType type); // "Coincident", "Distance", ...

struct Constraint {
    Id id = 0;
    ConstraintType type = ConstraintType::Coincident;
    int number = 0;        // per-kind creation count ("D2" for dimensions, "Horizontal3")
    std::vector<Id> refs;  // points and curves, in the order listed above
    double value = 0.0;    // dimensions
    bool driving = true;   // false: a driven (reference) dimension that only reports a value
    int variant = 0;       // Tangent of two circles/arcs: 0 = outside, 1 = inside;
                           // Angle: 0 = between the line directions, 1 = its supplement;
                           // Symmetric: 1 = only "p-q at right angles to the axis" (a mirrored
                           // arc's end, which its circle already holds at the right distance)
    std::vector<double> anchor;         // Fix: the held coordinates (x, y per point; then radius)
    double labelX = 0.0, labelY = 0.0;  // dimension text position (sketch coordinates)

    bool operator==(const Constraint&) const = default;
};

class Sketch {
public:
    Sketch(); // creates the fixed origin point

    Id origin() const { return m_origin; }

    Id addPoint(double x, double y);
    Id addLine(double x1, double y1, double x2, double y2);
    Id addCircle(double cx, double cy, double r);
    // Counter-clockwise arc from start to end around the centre. The end point is moved onto the
    // circle through the start point if needed.
    Id addArc(double cx, double cy, double sx, double sy, double ex, double ey);
    Id addConstraint(Constraint c); // assigns the id
    // Convenience: a relation with its details filled in from the current geometry (Fix anchors,
    // tangent side). Returns 0 if the references do not suit the relation.
    Id addRelation(ConstraintType type, std::vector<Id> refs);
    // A dimension; without a value it takes the current measured value.
    Id addDimension(ConstraintType type, std::vector<Id> refs, std::optional<double> value = std::nullopt);

    // Removes a point, curve (and its own points) or constraint, plus every constraint that refers
    // to something removed. Returns false if the id is unknown or the origin.
    bool remove(Id id);

    Point* point(Id id);
    const Point* point(Id id) const;
    Curve* curve(Id id);
    const Curve* curve(Id id) const;
    Constraint* constraint(Id id);
    const Constraint* constraint(Id id) const;

    const std::vector<Point>& points() const { return m_points; }
    const std::vector<Curve>& curves() const { return m_curves; }
    const std::vector<Constraint>& constraints() const { return m_constraints; }
    std::vector<Point>& points() { return m_points; }
    std::vector<Curve>& curves() { return m_curves; }
    std::vector<Constraint>& constraints() { return m_constraints; }

    double arcRadius(const Curve& arc) const;
    // Current value of a dimension measured from the geometry (what a driven dimension shows).
    std::optional<double> measure(const Constraint& c) const;
    // Gives a curve whose type changed (a trimmed circle became an arc) the next number of its new
    // kind, so its name stays unique.
    void renumber(Id curve);
    // Stable display names numbered per kind in creation order: "Line3", "Point5", "D2"
    // (dimensions), "Coincident4". Deleting an entity does not renumber the others.
    std::string name(Id id) const;

    bool operator==(const Sketch&) const = default;

    // Everything, ids and numbering included (the .blpart file). fromJson leaves `out` untouched
    // and fills `error` when the data does not fit.
    nlohmann::json toJson() const;
    static bool fromJson(const nlohmann::json& j, Sketch& out, std::string* error = nullptr);

private:
    int nextNumber(int kind) { return ++m_counters[kind]; }

    Id m_origin = 0;
    Id m_next = 1;
    std::map<int, int> m_counters; // kind -> last number handed out
    std::vector<Point> m_points;
    std::vector<Curve> m_curves;
    std::vector<Constraint> m_constraints;
};

} // namespace bl::sketch
