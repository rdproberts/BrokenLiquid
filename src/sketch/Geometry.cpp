#include "sketch/Geometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace bl::sketch {

namespace {
constexpr double kTwoPi = 2.0 * std::numbers::pi;

double wrap(double a) { // into [0, 2*pi)
    a = std::fmod(a, kTwoPi);
    return a < 0.0 ? a + kTwoPi : a;
}
} // namespace

double dot(P2 a, P2 b) { return a.x * b.x + a.y * b.y; }
double cross(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
double length(P2 a) { return std::hypot(a.x, a.y); }
P2 normalized(P2 a) {
    const double n = length(a);
    return n > 1e-300 ? P2{a.x / n, a.y / n} : P2{1.0, 0.0};
}
P2 perpendicular(P2 a) { return {-a.y, a.x}; }

P2 pointOf(const Sketch& s, Id point) {
    const Point* p = s.point(point);
    return p ? P2{p->x, p->y} : P2{};
}

P2 centerOf(const Sketch& s, const Curve& c) { return pointOf(s, c.center); }

double radiusOf(const Sketch& s, const Curve& c) {
    return c.type == CurveType::Circle ? c.radius : c.type == CurveType::Arc ? s.arcRadius(c) : 0.0;
}

double arcStart(const Sketch& s, const Curve& arc) {
    const P2 d = pointOf(s, arc.p1) - centerOf(s, arc);
    return std::atan2(d.y, d.x);
}

double arcSweep(const Sketch& s, const Curve& arc) {
    const P2 e = pointOf(s, arc.p2) - centerOf(s, arc);
    const double sweep = wrap(std::atan2(e.y, e.x) - arcStart(s, arc));
    return sweep < 1e-12 ? kTwoPi : sweep;
}

bool arcContains(const Sketch& s, const Curve& arc, double angle) {
    return wrap(angle - arcStart(s, arc)) <= arcSweep(s, arc) + 1e-12;
}

P2 closestOnCurve(const Sketch& s, const Curve& c, P2 p) {
    switch (c.type) {
    case CurveType::Line: {
        const P2 a = pointOf(s, c.p1), b = pointOf(s, c.p2);
        const P2 d = b - a;
        const double len2 = dot(d, d);
        const double t = len2 > 1e-300 ? std::clamp(dot(p - a, d) / len2, 0.0, 1.0) : 0.0;
        return a + d * t;
    }
    case CurveType::Circle: {
        const P2 ce = centerOf(s, c);
        return ce + normalized(p - ce) * c.radius;
    }
    case CurveType::Arc: {
        const P2 ce = centerOf(s, c);
        const P2 d = p - ce;
        if (arcContains(s, c, std::atan2(d.y, d.x))) {
            return ce + normalized(d) * radiusOf(s, c);
        }
        const P2 a = pointOf(s, c.p1), b = pointOf(s, c.p2);
        return length(p - a) <= length(p - b) ? a : b;
    }
    }
    return p;
}

double distanceToCurve(const Sketch& s, const Curve& c, P2 p) { return length(p - closestOnCurve(s, c, p)); }

P2 midpointOf(const Sketch& s, const Curve& c) {
    switch (c.type) {
    case CurveType::Line: return (pointOf(s, c.p1) + pointOf(s, c.p2)) * 0.5;
    case CurveType::Circle: return centerOf(s, c) + P2{c.radius, 0.0};
    case CurveType::Arc: {
        const double a = arcStart(s, c) + 0.5 * arcSweep(s, c);
        return centerOf(s, c) + P2{std::cos(a), std::sin(a)} * radiusOf(s, c);
    }
    }
    return {};
}

std::vector<P2> tessellate(const Sketch& s, const Curve& c, double step) {
    std::vector<P2> pts;
    if (c.type == CurveType::Line) {
        return {pointOf(s, c.p1), pointOf(s, c.p2)};
    }
    const P2 ce = centerOf(s, c);
    const double r = radiusOf(s, c);
    const double a0 = c.type == CurveType::Arc ? arcStart(s, c) : 0.0;
    const double sweep = c.type == CurveType::Arc ? arcSweep(s, c) : kTwoPi;
    const int n = std::max(2, static_cast<int>(std::ceil(sweep / std::max(step, 1e-3))));
    pts.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const double a = a0 + sweep * i / n;
        pts.push_back(ce + P2{std::cos(a), std::sin(a)} * r);
    }
    return pts;
}

std::optional<P2> exitDirection(const Sketch& s, const Curve& curve, Id point) {
    if (curve.type == CurveType::Line) {
        if (point == curve.p2) {
            return normalized(pointOf(s, curve.p2) - pointOf(s, curve.p1));
        }
        if (point == curve.p1) {
            return normalized(pointOf(s, curve.p1) - pointOf(s, curve.p2));
        }
    } else if (curve.type == CurveType::Arc) {
        const P2 radial = normalized(pointOf(s, point) - centerOf(s, curve));
        if (point == curve.p2) {
            return perpendicular(radial); // counter-clockwise onwards
        }
        if (point == curve.p1) {
            return perpendicular(radial) * -1.0; // back against the arc's direction
        }
    }
    return std::nullopt;
}

std::optional<TangentArc> tangentArc(P2 start, P2 direction, P2 end) {
    const P2 t = normalized(direction);
    const P2 n = perpendicular(t);
    const P2 chord = end - start;
    const double side = dot(chord, n);
    if (std::abs(side) < 1e-9 * std::max(1.0, length(chord))) {
        return std::nullopt;
    }
    const double k = dot(chord, chord) / (2.0 * side); // signed radius along the left normal
    return TangentArc{start + n * k, k > 0.0};
}

std::array<Id, 4> addRectangle(Sketch& s, P2 a, P2 c, bool construction) {
    const P2 corners[4] = {a, {c.x, a.y}, c, {a.x, c.y}};
    std::array<Id, 4> lines{};
    for (int i = 0; i < 4; ++i) {
        const P2 p = corners[i], q = corners[(i + 1) % 4];
        lines[i] = s.addLine(p.x, p.y, q.x, q.y);
        s.curve(lines[i])->construction = construction;
    }
    for (int i = 0; i < 4; ++i) {
        s.addRelation(ConstraintType::Coincident, {s.curve(lines[i])->p2, s.curve(lines[(i + 1) % 4])->p1});
    }
    s.addRelation(ConstraintType::Horizontal, {lines[0]});
    s.addRelation(ConstraintType::Horizontal, {lines[2]});
    s.addRelation(ConstraintType::Vertical, {lines[1]});
    s.addRelation(ConstraintType::Vertical, {lines[3]});
    return lines;
}

std::array<Id, 7> addCenterRectangle(Sketch& s, P2 center, P2 corner, bool construction) {
    const P2 d = corner - center;
    const std::array<Id, 4> sides = addRectangle(s, center - d, center + d, construction);
    std::array<Id, 7> out{};
    std::copy(sides.begin(), sides.end(), out.begin());
    for (int i = 0; i < 2; ++i) {
        const Id from = s.curve(sides[i])->p1;     // corners 0 and 1
        const Id to = s.curve(sides[i + 2])->p1;   // corners 2 and 3
        const P2 p = pointOf(s, from), q = pointOf(s, to);
        const Id diagonal = s.addLine(p.x, p.y, q.x, q.y);
        s.curve(diagonal)->construction = true;
        s.addRelation(ConstraintType::Coincident, {from, s.curve(diagonal)->p1});
        s.addRelation(ConstraintType::Coincident, {to, s.curve(diagonal)->p2});
        out[4 + i] = diagonal;
    }
    out[6] = s.addPoint(center.x, center.y);
    s.addRelation(ConstraintType::Midpoint, {out[6], out[4]});
    return out;
}

bool parallel(const Sketch& s, const Curve& a, const Curve& b, double tolerance) {
    if (a.type != CurveType::Line || b.type != CurveType::Line) {
        return false;
    }
    const P2 u = normalized(pointOf(s, a.p2) - pointOf(s, a.p1));
    const P2 w = normalized(pointOf(s, b.p2) - pointOf(s, b.p1));
    return std::abs(cross(u, w)) < tolerance;
}

std::optional<P2> lineIntersection(const Sketch& s, const Curve& a, const Curve& b) {
    const P2 p = pointOf(s, a.p1), r = pointOf(s, a.p2) - p;
    const P2 q = pointOf(s, b.p1), w = pointOf(s, b.p2) - q;
    const double den = cross(r, w);
    if (std::abs(den) < 1e-12 * std::max(1.0, length(r) * length(w))) {
        return std::nullopt;
    }
    return p + r * (cross(q - p, w) / den);
}

} // namespace bl::sketch
