#include "sketch/Operations.h"

#include "sketch/Solver.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <numeric>

namespace bl::sketch {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * kPi;

double wrap(double a) { // into [0, 2 pi)
    a = std::fmod(a, kTwoPi);
    return a < 0.0 ? a + kTwoPi : a;
}

double angleOf(P2 d) { return std::atan2(d.y, d.x); }

// Size of the sketch, for tolerances that scale with it.
double scaleOf(const Sketch& s) {
    double lo[2] = {0.0, 0.0}, hi[2] = {0.0, 0.0};
    for (const Point& p : s.points()) {
        lo[0] = std::min(lo[0], p.x);
        lo[1] = std::min(lo[1], p.y);
        hi[0] = std::max(hi[0], p.x);
        hi[1] = std::max(hi[1], p.y);
    }
    for (const Curve& c : s.curves()) {
        if (c.type == CurveType::Circle) {
            hi[0] = std::max(hi[0], pointOf(s, c.center).x + c.radius);
            hi[1] = std::max(hi[1], pointOf(s, c.center).y + c.radius);
        }
    }
    return std::max(1.0, std::hypot(hi[0] - lo[0], hi[1] - lo[1]));
}

double tolerance(const Sketch& s) { return 1e-7 * scaleOf(s); }

// The infinite line or full circle a curve lies on.
struct Carrier {
    bool line = true;
    P2 a, b; // two points on the line
    P2 c;    // circle centre
    double r = 0.0;
};

Carrier carrierOf(const Sketch& s, const Curve& k) {
    Carrier g;
    if (k.type == CurveType::Line) {
        g.a = pointOf(s, k.p1);
        g.b = pointOf(s, k.p2);
    } else {
        g.line = false;
        g.c = centerOf(s, k);
        g.r = radiusOf(s, k);
    }
    return g;
}

std::vector<P2> meet(const Carrier& g, const Carrier& h, double tol) {
    if (g.line && h.line) {
        const P2 r = g.b - g.a, w = h.b - h.a;
        const double den = cross(r, w);
        if (std::abs(den) < 1e-12 * std::max(1e-300, length(r) * length(w))) {
            return {};
        }
        return {g.a + r * (cross(h.a - g.a, w) / den)};
    }
    if (!g.line && h.line) {
        return meet(h, g, tol);
    }
    if (g.line) { // line and circle
        const P2 u = normalized(g.b - g.a);
        const P2 foot = g.a + u * dot(h.c - g.a, u);
        const double d = length(h.c - foot);
        if (d > h.r + tol) {
            return {};
        }
        if (d > h.r - tol) {
            return {foot + normalized(foot - h.c) * (h.r - d)}; // touching
        }
        const double half = std::sqrt(h.r * h.r - d * d);
        return {foot - u * half, foot + u * half};
    }
    // two circles
    const P2 dc = h.c - g.c;
    const double d = length(dc);
    if (d < tol || d > g.r + h.r + tol || d < std::abs(g.r - h.r) - tol) {
        return {};
    }
    const double along = (g.r * g.r - h.r * h.r + d * d) / (2.0 * d);
    const double h2 = g.r * g.r - along * along;
    const P2 u = dc * (1.0 / d);
    const P2 base = g.c + u * along;
    if (h2 <= tol * tol) {
        return {base};
    }
    const double off = std::sqrt(h2);
    return {base - perpendicular(u) * off, base + perpendicular(u) * off};
}

// Is a point on the carrier also within the curve's extent?
bool within(const Sketch& s, const Curve& k, P2 p, double tol) {
    switch (k.type) {
    case CurveType::Line: {
        const P2 a = pointOf(s, k.p1), b = pointOf(s, k.p2);
        const double len = std::max(1e-300, length(b - a));
        const double t = dot(p - a, b - a) / (len * len);
        return t >= -tol / len && t <= 1.0 + tol / len;
    }
    case CurveType::Arc: {
        const double r = std::max(1e-300, radiusOf(s, k));
        const double t = parameterOf(s, k, p);
        return t <= arcSweep(s, k) + tol / r || t >= kTwoPi - tol / r;
    }
    case CurveType::Circle: return true;
    }
    return false;
}

// An arc parameter just below 2 pi is really its start.
double normalizeArc(const Sketch& s, const Curve& k, double t) {
    if (k.type == CurveType::Arc && t > 0.5 * (arcSweep(s, k) + kTwoPi)) {
        return t - kTwoPi;
    }
    return t;
}

struct Crossing {
    double t = 0.0;
    P2 at;
    Id other = 0;
};

// Where other curves cross `k`, by parameter along `k`. `construction`: include construction curves.
std::vector<Crossing> crossingsOn(const Sketch& s, const Curve& k, bool construction = true) {
    std::vector<Crossing> out;
    for (const Curve& o : s.curves()) {
        if (o.id == k.id || (!construction && o.construction)) {
            continue;
        }
        for (const P2& p : intersections(s, k, o)) {
            out.push_back({normalizeArc(s, k, parameterOf(s, k, p)), p, o.id});
        }
    }
    std::sort(out.begin(), out.end(), [](const Crossing& a, const Crossing& b) { return a.t < b.t; });
    return out;
}

void movePoint(Sketch& s, Id id, P2 to) {
    if (Point* p = s.point(id)) {
        p->x = to.x;
        p->y = to.y;
    }
}

// Every constraint that refers to `from` refers to `to` instead.
void retarget(Sketch& s, Id from, Id to) {
    for (Constraint& c : s.constraints()) {
        std::replace(c.refs.begin(), c.refs.end(), from, to);
    }
}

template <typename Pred>
void dropConstraints(Sketch& s, Pred pred) {
    std::vector<Id> ids;
    for (const Constraint& c : s.constraints()) {
        if (pred(c)) {
            ids.push_back(c.id);
        }
    }
    for (Id id : ids) {
        s.remove(id);
    }
}

bool refersTo(const Constraint& c, Id id) { return std::find(c.refs.begin(), c.refs.end(), id) != c.refs.end(); }

// A point is about to move: the relations that held it where it was no longer apply.
void detachPoint(Sketch& s, Id point) {
    dropConstraints(s, [point](const Constraint& c) { return refersTo(c, point); });
}

// A curve's ends are about to change: drop what fixed its length (or its whole position).
void dropExtentConstraints(Sketch& s, Id curve) {
    const Curve* k = s.curve(curve);
    const bool line = k && k->type == CurveType::Line;
    dropConstraints(s, [&](const Constraint& c) {
        if (c.type == ConstraintType::Fix && refersTo(c, curve)) {
            return true;
        }
        if (!line) {
            return false;
        }
        return (c.type == ConstraintType::Distance && c.refs.size() == 1 && c.refs[0] == curve) ||
               (c.type == ConstraintType::Midpoint && refersTo(c, curve)) ||
               (c.type == ConstraintType::Equal && refersTo(c, curve));
    });
}

Id otherEnd(const Curve& k, Id end) { return end == k.p1 ? k.p2 : k.p1; }

P2 reflect(P2 p, P2 a, P2 b) {
    const P2 u = normalized(b - a);
    const P2 foot = a + u * dot(p - a, u);
    return foot * 2.0 - p;
}

double polygonArea(const std::vector<P2>& poly) {
    double a = 0.0;
    for (size_t i = 0; i < poly.size(); ++i) {
        a += cross(poly[i], poly[(i + 1) % poly.size()]);
    }
    return 0.5 * a;
}

bool pointInPolygon(P2 p, const std::vector<P2>& poly) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const P2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

// Points along a piece of a curve from t0 to t1 (either direction), ending at t1.
std::vector<P2> piecePoints(const Sketch& s, const Curve& k, double t0, double t1) {
    if (k.type == CurveType::Line) {
        return {pointAt(s, k, t0), pointAt(s, k, t1)};
    }
    const int n = std::max(2, static_cast<int>(std::ceil(std::abs(t1 - t0) / 0.05)));
    std::vector<P2> out;
    out.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        out.push_back(pointAt(s, k, t0 + (t1 - t0) * i / n));
    }
    return out;
}

} // namespace

// --- Intersections -------------------------------------------------------------------------------

double parameterOf(const Sketch& s, const Curve& c, P2 p) {
    switch (c.type) {
    case CurveType::Line: {
        const P2 a = pointOf(s, c.p1), d = pointOf(s, c.p2) - a;
        const double len2 = dot(d, d);
        return len2 > 1e-300 ? dot(p - a, d) / len2 : 0.0;
    }
    case CurveType::Arc: return wrap(angleOf(p - centerOf(s, c)) - arcStart(s, c));
    case CurveType::Circle: return wrap(angleOf(p - centerOf(s, c)));
    }
    return 0.0;
}

P2 pointAt(const Sketch& s, const Curve& c, double t) {
    switch (c.type) {
    case CurveType::Line: {
        const P2 a = pointOf(s, c.p1);
        return a + (pointOf(s, c.p2) - a) * t;
    }
    case CurveType::Arc:
    case CurveType::Circle: {
        const double angle = (c.type == CurveType::Arc ? arcStart(s, c) : 0.0) + t;
        return centerOf(s, c) + P2{std::cos(angle), std::sin(angle)} * radiusOf(s, c);
    }
    }
    return {};
}

double parameterEnd(const Sketch& s, const Curve& c) {
    switch (c.type) {
    case CurveType::Line: return 1.0;
    case CurveType::Arc: return arcSweep(s, c);
    case CurveType::Circle: return kTwoPi;
    }
    return 0.0;
}

std::vector<P2> intersections(const Sketch& s, const Curve& a, const Curve& b, bool unboundedA) {
    const double tol = tolerance(s);
    std::vector<P2> out;
    for (const P2& p : meet(carrierOf(s, a), carrierOf(s, b), tol)) {
        if (within(s, b, p, tol) && (unboundedA || within(s, a, p, tol))) {
            if (std::none_of(out.begin(), out.end(), [&](const P2& q) { return length(q - p) < tol; })) {
                out.push_back(p);
            }
        }
    }
    return out;
}

// --- Trim and extend -----------------------------------------------------------------------------

bool trim(Sketch& s, Id id, P2 pick) {
    const Curve* found = s.curve(id);
    if (!found) {
        return false;
    }
    const Curve c = *found;
    const double tol = tolerance(s);
    const double end = parameterEnd(s, c);
    const double tp = normalizeArc(s, c, parameterOf(s, c, pick));
    // Distinct crossings strictly inside the curve (circles: anywhere).
    std::vector<Crossing> cuts;
    for (const Crossing& x : crossingsOn(s, c)) {
        const bool inside = c.type == CurveType::Circle ||
                            (length(x.at - pointAt(s, c, 0.0)) > tol && length(x.at - pointAt(s, c, end)) > tol);
        if (inside && std::none_of(cuts.begin(), cuts.end(), [&](const Crossing& y) { return length(y.at - x.at) < tol; })) {
            cuts.push_back(x);
        }
    }

    if (c.type == CurveType::Circle) {
        if (cuts.size() < 2) {
            return s.remove(id);
        }
        // Keep the arc from the first cut after the pick round to the last cut before it.
        const auto after = std::find_if(cuts.begin(), cuts.end(), [tp](const Crossing& x) { return x.t > tp; });
        const Crossing hi = after == cuts.end() ? cuts.front() : *after;
        const Crossing lo = after == cuts.begin() ? cuts.back() : *(after == cuts.end() ? cuts.end() - 1 : after - 1);
        const Id p1 = s.addPoint(hi.at.x, hi.at.y);
        const Id p2 = s.addPoint(lo.at.x, lo.at.y);
        dropConstraints(s, [id](const Constraint& k) { return k.type == ConstraintType::Fix && refersTo(k, id); });
        Curve* k = s.curve(id);
        k->type = CurveType::Arc;
        k->p1 = p1;
        k->p2 = p2;
        k->radius = 0.0;
        s.point(p1)->owner = id;
        s.point(p2)->owner = id;
        s.renumber(id);
        s.addRelation(ConstraintType::PointOnCurve, {p1, hi.other});
        s.addRelation(ConstraintType::PointOnCurve, {p2, lo.other});
        return true;
    }

    const Crossing* lo = nullptr;
    const Crossing* hi = nullptr;
    for (const Crossing& x : cuts) {
        if (x.t < tp) {
            lo = &x;
        } else if (!hi) {
            hi = &x;
        }
    }
    if (!lo && !hi) {
        return s.remove(id);
    }
    if (!lo || !hi) {
        // Cut off the start (up to hi) or the end (from lo).
        const Crossing x = lo ? *lo : *hi;
        const Id moving = lo ? c.p2 : c.p1;
        detachPoint(s, moving);
        dropExtentConstraints(s, id);
        movePoint(s, moving, x.at);
        s.addRelation(ConstraintType::PointOnCurve, {moving, x.other});
        return true;
    }
    // Cut out the middle: the curve keeps [0, lo], a new one takes [hi, end].
    const Crossing a = *lo, b = *hi;
    dropExtentConstraints(s, id);
    const P2 tail = pointOf(s, c.p2);
    Id piece = 0;
    if (c.type == CurveType::Line) {
        piece = s.addLine(b.at.x, b.at.y, tail.x, tail.y);
    } else {
        const P2 ce = centerOf(s, c);
        piece = s.addArc(ce.x, ce.y, b.at.x, b.at.y, tail.x, tail.y);
    }
    const Curve n = *s.curve(piece);
    s.curve(piece)->construction = c.construction;
    retarget(s, c.p2, n.p2);
    movePoint(s, c.p2, a.at);
    s.addRelation(ConstraintType::PointOnCurve, {c.p2, a.other});
    s.addRelation(ConstraintType::PointOnCurve, {n.p1, b.other});
    if (c.type == CurveType::Line) {
        s.addRelation(ConstraintType::Collinear, {id, piece});
    } else {
        s.addRelation(ConstraintType::Coincident, {c.center, n.center});
        s.addRelation(ConstraintType::Equal, {id, piece});
    }
    return true;
}

bool extend(Sketch& s, Id id, P2 pick) {
    const Curve* found = s.curve(id);
    if (!found || found->type == CurveType::Circle) {
        return false;
    }
    const Curve c = *found;
    const double tol = tolerance(s);
    const bool atEnd = length(pick - pointOf(s, c.p2)) <= length(pick - pointOf(s, c.p1));
    const Id moving = atEnd ? c.p2 : c.p1;
    const P2 from = pointOf(s, moving);

    double best = 1e300;
    P2 target;
    Id hit = 0;
    for (const Curve& o : s.curves()) {
        if (o.id == id) {
            continue;
        }
        for (const P2& p : intersections(s, c, o, true)) {
            if (length(p - from) < tol) {
                continue;
            }
            double how = -1.0; // how far beyond the end, in the curve's own parameter
            if (c.type == CurveType::Line) {
                const double t = parameterOf(s, c, p);
                how = atEnd ? t - 1.0 : -t;
            } else {
                const P2 ce = centerOf(s, c);
                const double delta = atEnd ? wrap(angleOf(p - ce) - angleOf(from - ce)) : wrap(angleOf(from - ce) - angleOf(p - ce));
                how = arcSweep(s, c) + delta < kTwoPi - 1e-6 ? delta : -1.0;
            }
            if (how > 1e-12 && how < best) {
                best = how;
                target = p;
                hit = o.id;
            }
        }
    }
    if (!hit) {
        return false;
    }
    detachPoint(s, moving);
    dropExtentConstraints(s, id);
    movePoint(s, moving, target);
    s.addRelation(ConstraintType::PointOnCurve, {moving, hit});
    return true;
}

// --- Fillet, slot, polygon -----------------------------------------------------------------------

std::optional<Id> filletCorner(Sketch& s, Id corner, double radius, bool addDimension) {
    const Point* cp = s.point(corner);
    if (!cp || radius <= 0.0) {
        return std::nullopt;
    }
    const double tol = tolerance(s);
    const P2 v{cp->x, cp->y};
    std::vector<std::pair<Id, Id>> ends; // (line, its end at the corner)
    for (const Curve& k : s.curves()) {
        if (k.type != CurveType::Line) {
            continue;
        }
        for (Id e : {k.p1, k.p2}) {
            if (length(pointOf(s, e) - v) < tol) {
                ends.emplace_back(k.id, e);
            }
        }
    }
    if (ends.size() != 2 || ends[0].first == ends[1].first) {
        return std::nullopt;
    }
    const Id l1 = ends[0].first, e1 = ends[0].second;
    const Id l2 = ends[1].first, e2 = ends[1].second;
    const Id o1 = otherEnd(*s.curve(l1), e1), o2 = otherEnd(*s.curve(l2), e2);
    const P2 u1 = normalized(pointOf(s, o1) - v), u2 = normalized(pointOf(s, o2) - v);
    const double phi = std::acos(std::clamp(dot(u1, u2), -1.0, 1.0));
    if (phi < 1e-6 || phi > kPi - 1e-6) {
        return std::nullopt; // collinear: nothing to round
    }
    const double d = radius / std::tan(0.5 * phi);
    if (d >= length(pointOf(s, o1) - v) - tol || d >= length(pointOf(s, o2) - v) - tol) {
        return std::nullopt; // the radius does not fit on the lines
    }
    const P2 t1 = v + u1 * d, t2 = v + u2 * d;
    const P2 center = v + normalized(u1 + u2) * (radius / std::sin(0.5 * phi));
    const bool ccw = cross(t1 - center, t2 - center) > 0.0;
    const Id arc = ccw ? s.addArc(center.x, center.y, t1.x, t1.y, t2.x, t2.y) : s.addArc(center.x, center.y, t2.x, t2.y, t1.x, t1.y);
    const Curve a = *s.curve(arc);
    s.curve(arc)->construction = s.curve(l1)->construction && s.curve(l2)->construction;
    const Id at1 = ccw ? a.p1 : a.p2, at2 = ccw ? a.p2 : a.p1;

    // The virtual sharp takes over what held the corner; line lengths become sharp-to-end distances.
    const Id sharp = s.addPoint(v.x, v.y);
    s.point(sharp)->construction = true;
    for (Constraint& k : s.constraints()) {
        if (k.type == ConstraintType::Distance && k.refs.size() == 1 && (k.refs[0] == l1 || k.refs[0] == l2)) {
            k.refs = {sharp, k.refs[0] == l1 ? o1 : o2};
        }
    }
    dropConstraints(s, [&](const Constraint& k) {
        const bool lineRef = refersTo(k, l1) || refersTo(k, l2);
        return (lineRef && (k.type == ConstraintType::Midpoint || k.type == ConstraintType::Equal || k.type == ConstraintType::Fix));
    });
    retarget(s, e1, sharp);
    retarget(s, e2, sharp);
    dropConstraints(s, [](const Constraint& k) { // the corner's own coincidence, now sharp-to-sharp
        std::vector<Id> r = k.refs;
        std::sort(r.begin(), r.end());
        return std::adjacent_find(r.begin(), r.end()) != r.end();
    });
    movePoint(s, e1, t1);
    movePoint(s, e2, t2);
    s.addRelation(ConstraintType::PointOnCurve, {sharp, l1});
    s.addRelation(ConstraintType::PointOnCurve, {sharp, l2});
    s.addRelation(ConstraintType::Coincident, {e1, at1});
    s.addRelation(ConstraintType::Coincident, {e2, at2});
    s.addRelation(ConstraintType::Tangent, {l1, arc});
    s.addRelation(ConstraintType::Tangent, {l2, arc});
    if (addDimension) {
        if (Constraint* dim = s.constraint(s.addDimension(ConstraintType::Radius, {arc}, radius))) {
            const P2 label = v + normalized(v - center) * (0.8 * radius + 2.0);
            dim->labelX = label.x;
            dim->labelY = label.y;
        }
    }
    return arc;
}

std::array<Id, 5> addSlot(Sketch& s, P2 c1, P2 c2, double halfWidth) {
    const P2 u = normalized(c2 - c1);
    const P2 n = perpendicular(u) * halfWidth;
    const P2 a1 = c1 + n, b1 = c1 - n, a2 = c2 + n, b2 = c2 - n;
    const Id arc1 = s.addArc(c1.x, c1.y, a1.x, a1.y, b1.x, b1.y); // round the far side of c1
    const Id arc2 = s.addArc(c2.x, c2.y, b2.x, b2.y, a2.x, a2.y); // round the far side of c2
    const Id top = s.addLine(a2.x, a2.y, a1.x, a1.y);
    const Id bottom = s.addLine(b1.x, b1.y, b2.x, b2.y);
    const Id axis = s.addLine(c1.x, c1.y, c2.x, c2.y);
    s.curve(axis)->construction = true;
    const Curve k1 = *s.curve(arc1), k2 = *s.curve(arc2), t = *s.curve(top), b = *s.curve(bottom), x = *s.curve(axis);
    s.addRelation(ConstraintType::Coincident, {k2.p2, t.p1});
    s.addRelation(ConstraintType::Coincident, {t.p2, k1.p1});
    s.addRelation(ConstraintType::Coincident, {k1.p2, b.p1});
    s.addRelation(ConstraintType::Coincident, {b.p2, k2.p1});
    s.addRelation(ConstraintType::Coincident, {x.p1, k1.center});
    s.addRelation(ConstraintType::Coincident, {x.p2, k2.center});
    s.addRelation(ConstraintType::Tangent, {top, arc1});
    s.addRelation(ConstraintType::Tangent, {top, arc2});
    s.addRelation(ConstraintType::Tangent, {bottom, arc1});
    s.addRelation(ConstraintType::Tangent, {bottom, arc2});
    s.addRelation(ConstraintType::Equal, {arc1, arc2});
    return {arc1, arc2, top, bottom, axis};
}

std::vector<Id> addPolygon(Sketch& s, P2 center, P2 vertex, int sides) {
    if (sides < 3) {
        return {};
    }
    const double r = length(vertex - center);
    const double a0 = angleOf(vertex - center);
    std::vector<P2> corners;
    for (int i = 0; i < sides; ++i) {
        const double a = a0 + kTwoPi * i / sides;
        corners.push_back(center + P2{std::cos(a), std::sin(a)} * r);
    }
    std::vector<Id> out;
    for (int i = 0; i < sides; ++i) {
        const P2 p = corners[static_cast<size_t>(i)], q = corners[static_cast<size_t>((i + 1) % sides)];
        out.push_back(s.addLine(p.x, p.y, q.x, q.y));
    }
    const Id circle = s.addCircle(center.x, center.y, r);
    s.curve(circle)->construction = true;
    for (int i = 0; i < sides; ++i) {
        const Curve a = *s.curve(out[static_cast<size_t>(i)]);
        const Curve b = *s.curve(out[static_cast<size_t>((i + 1) % sides)]);
        s.addRelation(ConstraintType::Coincident, {a.p2, b.p1});
        s.addRelation(ConstraintType::PointOnCurve, {a.p1, circle});
        if (i > 0) {
            s.addRelation(ConstraintType::Equal, {out[0], out[static_cast<size_t>(i)]});
        }
    }
    out.push_back(circle);
    return out;
}

// --- Offset -------------------------------------------------------------------------------------

namespace {

struct Step { // one curve walked along a chain
    Id curve = 0;
    bool reversed = false; // walked from p2 to p1
};

Id walkStart(const Curve& k, bool reversed) { return reversed ? k.p2 : k.p1; }
Id walkEnd(const Curve& k, bool reversed) { return reversed ? k.p1 : k.p2; }

// Groups lines and arcs into chains joined end to end (by position).
std::vector<std::pair<std::vector<Step>, bool>> chainsOf(const Sketch& s, std::vector<Id> ids, double tol) {
    std::vector<std::pair<std::vector<Step>, bool>> out;
    auto ends = [&](Id id) {
        const Curve* k = s.curve(id);
        return std::pair<P2, P2>{pointOf(s, k->p1), pointOf(s, k->p2)};
    };
    auto degree = [&](P2 p) {
        int n = 0;
        for (Id id : ids) {
            const auto [a, b] = ends(id);
            n += (length(a - p) < tol) + (length(b - p) < tol);
        }
        return n;
    };
    while (!ids.empty()) {
        // Start an open chain at a free end if there is one.
        size_t seed = 0;
        bool reversed = false;
        bool open = false;
        for (size_t i = 0; i < ids.size() && !open; ++i) {
            const auto [a, b] = ends(ids[i]);
            if (degree(a) == 1) {
                seed = i, reversed = false, open = true;
            } else if (degree(b) == 1) {
                seed = i, reversed = true, open = true;
            }
        }
        std::vector<Step> chain{{ids[seed], reversed}};
        ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(seed));
        const P2 first = pointOf(s, walkStart(*s.curve(chain[0].curve), reversed));
        bool closed = false;
        while (true) {
            const P2 at = pointOf(s, walkEnd(*s.curve(chain.back().curve), chain.back().reversed));
            if (chain.size() > 1 && length(at - first) < tol) {
                closed = true;
                break;
            }
            auto next = std::find_if(ids.begin(), ids.end(), [&](Id id) {
                const auto [a, b] = ends(id);
                return length(a - at) < tol || length(b - at) < tol;
            });
            if (next == ids.end()) {
                if (chain.size() == 1 && length(at - first) < tol) {
                    closed = true; // a single closed curve
                }
                break;
            }
            chain.push_back({*next, length(ends(*next).first - at) >= tol});
            ids.erase(next);
        }
        out.emplace_back(std::move(chain), closed);
    }
    return out;
}

struct OffsetCurve {
    bool line = true;
    P2 a, b;       // walk start and end
    P2 c;          // arc centre
    double r = 0.0;
    bool ccw = true; // arcs: walked counter-clockwise
};

} // namespace

std::vector<Id> offset(Sketch& s, const std::vector<Id>& curves, double distance, bool addDimensions) {
    const double tol = tolerance(s);
    std::vector<Id> lineArcs, circles;
    for (Id id : curves) {
        if (const Curve* k = s.curve(id)) {
            (k->type == CurveType::Circle ? circles : lineArcs).push_back(id);
        }
    }
    std::vector<Id> created;
    for (Id id : circles) {
        const Curve k = *s.curve(id);
        const double r = k.radius + distance;
        if (r <= tol) {
            return {};
        }
        const P2 c = centerOf(s, k);
        const Id n = s.addCircle(c.x, c.y, r);
        s.curve(n)->construction = k.construction;
        s.addRelation(ConstraintType::Concentric, {id, n});
        if (addDimensions) {
            if (Constraint* dim = s.constraint(s.addDimension(ConstraintType::Diameter, {n}))) {
                dim->labelX = c.x + 0.8 * r;
                dim->labelY = c.y + 0.8 * r;
            }
        }
        created.push_back(n);
    }

    for (auto& [chain, closed] : chainsOf(s, lineArcs, tol)) {
        if (closed) { // run counter-clockwise so a positive distance goes outwards
            std::vector<P2> poly;
            for (const Step& st : chain) {
                const Curve& k = *s.curve(st.curve);
                const double end = parameterEnd(s, k);
                std::vector<P2> pts = piecePoints(s, k, st.reversed ? end : 0.0, st.reversed ? 0.0 : end);
                poly.insert(poly.end(), pts.begin(), pts.end() - 1);
            }
            if (polygonArea(poly) < 0.0) {
                std::reverse(chain.begin(), chain.end());
                for (Step& st : chain) {
                    st.reversed = !st.reversed;
                }
            }
        }
        // Each curve moved to its right (walking direction) by `distance`.
        std::vector<OffsetCurve> out;
        std::vector<P2> joints; // original joint positions, for picking among intersections
        for (const Step& st : chain) {
            const Curve& k = *s.curve(st.curve);
            const P2 a = pointOf(s, walkStart(k, st.reversed)), b = pointOf(s, walkEnd(k, st.reversed));
            OffsetCurve o;
            if (k.type == CurveType::Line) {
                const P2 t = normalized(b - a);
                const P2 right{t.y, -t.x};
                o.a = a + right * distance;
                o.b = b + right * distance;
            } else {
                o.line = false;
                o.c = centerOf(s, k);
                o.ccw = !st.reversed;
                o.r = radiusOf(s, k) + (o.ccw ? distance : -distance);
                if (o.r <= tol) {
                    return {};
                }
                o.a = o.c + normalized(a - o.c) * o.r;
                o.b = o.c + normalized(b - o.c) * o.r;
            }
            out.push_back(o);
            joints.push_back(b);
        }
        // Join neighbours where their offsets meet.
        const size_t n = out.size();
        std::vector<bool> joined(n, false), smooth(n, false); // joint after step i
        for (size_t i = 0; i < n; ++i) {
            if (i + 1 == n && !closed) {
                break;
            }
            OffsetCurve& e = out[i];
            OffsetCurve& f = out[(i + 1) % n];
            if (length(e.b - f.a) < tol * 100.0) {
                f.a = e.b;
                joined[i] = smooth[i] = true;
                continue;
            }
            Carrier g, h;
            g.line = e.line, g.a = e.a, g.b = e.b, g.c = e.c, g.r = e.r;
            h.line = f.line, h.a = f.a, h.b = f.b, h.c = f.c, h.r = f.r;
            const std::vector<P2> xs = meet(g, h, tol);
            if (xs.empty()) {
                continue;
            }
            const P2 x = *std::min_element(xs.begin(), xs.end(), [&](const P2& p, const P2& q) {
                return length(p - joints[i]) < length(q - joints[i]);
            });
            e.b = f.a = x;
            joined[i] = true;
        }
        // A line that turned round has collapsed: the distance is too big for this chain.
        for (size_t i = 0; i < n; ++i) {
            if (out[i].line) {
                const Curve& k = *s.curve(chain[i].curve);
                const P2 dir = pointOf(s, walkEnd(k, chain[i].reversed)) - pointOf(s, walkStart(k, chain[i].reversed));
                if (dot(out[i].b - out[i].a, dir) <= 0.0) {
                    return {};
                }
            }
        }
        // Build the curves.
        std::vector<Id> ids;
        std::vector<std::pair<Id, Id>> walkPoints; // (start, end) of each new curve
        for (size_t i = 0; i < n; ++i) {
            const OffsetCurve& o = out[i];
            Id id = 0;
            if (o.line) {
                id = s.addLine(o.a.x, o.a.y, o.b.x, o.b.y);
                walkPoints.emplace_back(s.curve(id)->p1, s.curve(id)->p2);
            } else {
                id = o.ccw ? s.addArc(o.c.x, o.c.y, o.a.x, o.a.y, o.b.x, o.b.y) : s.addArc(o.c.x, o.c.y, o.b.x, o.b.y, o.a.x, o.a.y);
                const Curve& k = *s.curve(id);
                walkPoints.emplace_back(o.ccw ? k.p1 : k.p2, o.ccw ? k.p2 : k.p1);
            }
            s.curve(id)->construction = s.curve(chain[i].curve)->construction;
            ids.push_back(id);
        }
        for (size_t i = 0; i < n; ++i) {
            if (joined[i]) {
                s.addRelation(ConstraintType::Coincident, {walkPoints[i].second, walkPoints[(i + 1) % n].first});
            }
        }
        for (size_t i = 0; i < n; ++i) {
            const Id source = chain[i].curve;
            if (out[i].line) {
                s.addRelation(ConstraintType::Parallel, {source, ids[i]});
                if (addDimensions) {
                    if (Constraint* dim = s.constraint(s.addDimension(ConstraintType::Distance, {source, ids[i]}))) {
                        const P2 a = pointOf(s, s.curve(source)->p1);
                        const P2 mid = (out[i].a + out[i].b) * 0.5;
                        const P2 label = (a + mid) * 0.5;
                        dim->labelX = label.x;
                        dim->labelY = label.y;
                    }
                }
                continue;
            }
            // An arc between two lines it was tangent to stays tangent to both and keeps its
            // radius (concentric as well would say the same thing twice); next to one such line it
            // is concentric and tangent to that line; otherwise concentric with its radius held.
            const size_t prev = (i + n - 1) % n;
            const bool prevJoint = (i > 0 || closed) && smooth[prev] && out[prev].line;
            const bool nextJoint = (i + 1 < n || closed) && smooth[i] && out[(i + 1) % n].line;
            if (!(prevJoint && nextJoint)) {
                s.addRelation(ConstraintType::Concentric, {source, ids[i]});
            }
            if (prevJoint) {
                s.addRelation(ConstraintType::Tangent, {ids[prev], ids[i]});
            }
            if (nextJoint) {
                s.addRelation(ConstraintType::Tangent, {ids[(i + 1) % n], ids[i]});
            }
            if ((prevJoint != nextJoint) || !addDimensions) {
                continue;
            }
            {
                if (Constraint* dim = s.constraint(s.addDimension(ConstraintType::Radius, {ids[i]}))) {
                    const P2 label = out[i].c + normalized((out[i].a + out[i].b) * 0.5 - out[i].c) * (out[i].r * 1.3);
                    dim->labelX = label.x;
                    dim->labelY = label.y;
                }
            }
        }
        created.insert(created.end(), ids.begin(), ids.end());
    }
    return created;
}

// --- Mirror -------------------------------------------------------------------------------------

std::vector<Id> mirror(Sketch& s, const std::vector<Id>& items, Id axisId) {
    const Curve* axisCurve = s.curve(axisId);
    if (!axisCurve || axisCurve->type != CurveType::Line) {
        return {};
    }
    const double tol = tolerance(s);
    const P2 a = pointOf(s, axisCurve->p1), b = pointOf(s, axisCurve->p2);
    auto m = [&](P2 p) { return reflect(p, a, b); };
    auto onAxis = [&](P2 p) { return length(m(p) - p) < tol; };
    // Hold a copied point to its original: symmetric about the axis, or the same point on it.
    auto hold = [&](Id original, Id copy) {
        if (onAxis(pointOf(s, original))) {
            s.addRelation(ConstraintType::Coincident, {original, copy});
        } else {
            s.addRelation(ConstraintType::Symmetric, {original, copy, axisId});
        }
    };
    std::vector<Id> out;
    for (Id id : items) {
        if (id == axisId) {
            continue;
        }
        if (const Point* p = s.point(id); p && p->owner == 0 && id != s.origin()) {
            const P2 q = m({p->x, p->y});
            const Id copy = s.addPoint(q.x, q.y);
            s.point(copy)->construction = s.point(id)->construction;
            hold(id, copy);
            out.push_back(copy);
            continue;
        }
        const Curve* found = s.curve(id);
        if (!found) {
            continue;
        }
        const Curve k = *found;
        Id copy = 0;
        switch (k.type) {
        case CurveType::Line: {
            const P2 p = m(pointOf(s, k.p1)), q = m(pointOf(s, k.p2));
            copy = s.addLine(p.x, p.y, q.x, q.y);
            hold(k.p1, s.curve(copy)->p1);
            hold(k.p2, s.curve(copy)->p2);
            break;
        }
        case CurveType::Circle: {
            const P2 c = m(centerOf(s, k));
            copy = s.addCircle(c.x, c.y, k.radius);
            hold(k.center, s.curve(copy)->center);
            s.addRelation(ConstraintType::Equal, {id, copy});
            break;
        }
        case CurveType::Arc: {
            // Mirroring reverses the direction: the copy runs from the image of the end to the
            // image of the start.
            const P2 c = m(centerOf(s, k)), p = m(pointOf(s, k.p2)), q = m(pointOf(s, k.p1));
            copy = s.addArc(c.x, c.y, p.x, p.y, q.x, q.y);
            const Curve n = *s.curve(copy);
            hold(k.center, n.center);
            hold(k.p2, n.p1);
            // The copy's end already lies on its circle; only its side of the axis is left to hold.
            Constraint side;
            side.type = ConstraintType::Symmetric;
            side.refs = {k.p1, n.p2, axisId};
            side.variant = 1;
            s.addConstraint(side);
            break;
        }
        }
        s.curve(copy)->construction = k.construction;
        out.push_back(copy);
    }
    return out;
}

// --- Regions -------------------------------------------------------------------------------------

Contours findRegions(const Sketch& s) {
    const double tol = tolerance(s);
    Contours result;

    struct Edge {
        Piece piece;
        int from = -1, to = -1;
        bool alive = true;
    };
    std::vector<P2> nodes;
    auto nodeOf = [&](P2 p) {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (length(nodes[i] - p) < tol * 10.0) {
                return static_cast<int>(i);
            }
        }
        nodes.push_back(p);
        return static_cast<int>(nodes.size()) - 1;
    };
    std::vector<Edge> edges;
    std::vector<Loop> loneCircles; // circles crossing nothing: closed by themselves

    for (const Curve& k : s.curves()) {
        if (k.construction) {
            continue;
        }
        std::vector<double> ts;
        for (const Crossing& x : crossingsOn(s, k, false)) {
            ts.push_back(x.t);
        }
        const double end = parameterEnd(s, k);
        if (k.type != CurveType::Circle) {
            ts.push_back(0.0);
            ts.push_back(end);
        }
        for (double& t : ts) {
            t = k.type == CurveType::Circle ? t : std::clamp(t, 0.0, end);
        }
        std::sort(ts.begin(), ts.end());
        std::vector<double> cuts;
        for (double t : ts) {
            if (cuts.empty() || length(pointAt(s, k, t) - pointAt(s, k, cuts.back())) > tol) {
                cuts.push_back(t);
            }
        }
        if (k.type == CurveType::Circle) {
            if (cuts.empty()) {
                Loop loop;
                loop.pieces.push_back({k.id, 0.0, kTwoPi, pointAt(s, k, 0.0), pointAt(s, k, 0.0)});
                loop.polygon = piecePoints(s, k, 0.0, kTwoPi);
                loop.polygon.pop_back();
                loop.area = polygonArea(loop.polygon);
                loneCircles.push_back(std::move(loop));
                continue;
            }
            if (cuts.size() > 1 && length(pointAt(s, k, cuts.front()) - pointAt(s, k, cuts.back())) <= tol) {
                cuts.pop_back();
            }
            cuts.push_back(cuts.front() + kTwoPi);
        }
        for (size_t i = 0; i + 1 < cuts.size(); ++i) {
            Edge e;
            e.piece = {k.id, cuts[i], cuts[i + 1], pointAt(s, k, cuts[i]), pointAt(s, k, cuts[i + 1])};
            e.from = nodeOf(e.piece.start);
            e.to = nodeOf(e.piece.end);
            edges.push_back(e);
        }
    }

    // Dangling pieces cannot bound anything: remove them, remembering the open ends.
    std::vector<int> degree(nodes.size(), 0);
    for (const Edge& e : edges) {
        ++degree[static_cast<size_t>(e.from)];
        ++degree[static_cast<size_t>(e.to)];
    }
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (degree[i] == 1) {
            result.openEnds.push_back(nodes[i]);
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (Edge& e : edges) {
            if (e.alive && e.from != e.to && (degree[static_cast<size_t>(e.from)] == 1 || degree[static_cast<size_t>(e.to)] == 1)) {
                e.alive = false;
                --degree[static_cast<size_t>(e.from)];
                --degree[static_cast<size_t>(e.to)];
                changed = true;
            }
        }
    }

    // Half-edges 2i (forward) and 2i+1 (backward), sorted counter-clockwise around each node by the
    // direction they leave in (a point a little way along, so tangent curves still sort apart).
    const size_t halfCount = edges.size() * 2;
    auto halfFrom = [&](size_t h) { return h % 2 == 0 ? edges[h / 2].from : edges[h / 2].to; };
    auto halfTo = [&](size_t h) { return h % 2 == 0 ? edges[h / 2].to : edges[h / 2].from; };
    auto halfPoints = [&](size_t h) {
        const Piece& p = edges[h / 2].piece;
        const Curve& k = *s.curve(p.curve);
        return h % 2 == 0 ? piecePoints(s, k, p.t0, p.t1) : piecePoints(s, k, p.t1, p.t0);
    };
    auto leaving = [&](size_t h) {
        const Piece& p = edges[h / 2].piece;
        const Curve& k = *s.curve(p.curve);
        const double t0 = h % 2 == 0 ? p.t0 : p.t1, t1 = h % 2 == 0 ? p.t1 : p.t0;
        const P2 from = pointAt(s, k, t0);
        const double step = k.type == CurveType::Line ? 0.25 : std::min(0.05, 0.25 * std::abs(t1 - t0)) / std::abs(t1 - t0);
        const P2 ahead = pointAt(s, k, t0 + (t1 - t0) * step);
        return angleOf(ahead - from);
    };
    std::vector<std::vector<size_t>> around(nodes.size());
    for (size_t h = 0; h < halfCount; ++h) {
        if (edges[h / 2].alive) {
            around[static_cast<size_t>(halfFrom(h))].push_back(h);
        }
    }
    std::vector<double> angle(halfCount, 0.0);
    for (size_t h = 0; h < halfCount; ++h) {
        if (edges[h / 2].alive) {
            angle[h] = leaving(h);
        }
    }
    for (auto& list : around) {
        std::sort(list.begin(), list.end(), [&](size_t a, size_t b) { return angle[a] < angle[b]; });
    }

    // Components (for telling islands from the regions around them).
    std::vector<int> parent(nodes.size());
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> rootOf = [&](int i) { return parent[static_cast<size_t>(i)] == i ? i : parent[static_cast<size_t>(i)] = rootOf(parent[static_cast<size_t>(i)]); };
    for (const Edge& e : edges) {
        if (e.alive) {
            parent[static_cast<size_t>(rootOf(e.from))] = rootOf(e.to);
        }
    }

    // Walk faces: keep the face on the left, taking the next edge clockwise at each node.
    std::vector<bool> used(halfCount, false);
    std::vector<std::pair<Loop, int>> loops; // loop, component
    for (size_t start = 0; start < halfCount; ++start) {
        if (used[start] || !edges[start / 2].alive) {
            continue;
        }
        Loop loop;
        size_t h = start;
        for (size_t guard = 0; guard <= halfCount && !used[h]; ++guard) {
            used[h] = true;
            const Edge& e = edges[h / 2];
            Piece piece = e.piece;
            if (h % 2 == 1) {
                std::swap(piece.t0, piece.t1);
                std::swap(piece.start, piece.end);
            }
            loop.pieces.push_back(piece);
            std::vector<P2> pts = halfPoints(h);
            loop.polygon.insert(loop.polygon.end(), pts.begin(), pts.end() - 1);
            const auto& list = around[static_cast<size_t>(halfTo(h))];
            const size_t twin = h ^ 1u;
            const auto it = std::find(list.begin(), list.end(), twin);
            if (it == list.end()) {
                break;
            }
            const size_t index = static_cast<size_t>(it - list.begin());
            h = list[(index + list.size() - 1) % list.size()];
        }
        loop.area = polygonArea(loop.polygon);
        loops.emplace_back(std::move(loop), rootOf(halfFrom(start)));
    }
    int nextComponent = static_cast<int>(nodes.size());
    for (Loop& c : loneCircles) {
        Loop outside = c;
        std::reverse(outside.polygon.begin(), outside.polygon.end());
        outside.area = -c.area;
        for (Piece& p : outside.pieces) {
            std::swap(p.t0, p.t1);
        }
        loops.emplace_back(std::move(c), nextComponent);
        loops.emplace_back(std::move(outside), nextComponent);
        ++nextComponent;
    }

    // Bounded faces are counter-clockwise; each component's outline is clockwise and becomes a hole
    // of the smallest face of another component that contains it.
    std::vector<size_t> faces;
    for (size_t i = 0; i < loops.size(); ++i) {
        if (loops[i].first.area > tol * tol) {
            faces.push_back(i);
        }
    }
    std::vector<std::vector<size_t>> holes(loops.size());
    for (size_t i = 0; i < loops.size(); ++i) {
        const auto& [loop, component] = loops[i];
        if (loop.area >= -tol * tol || loop.polygon.empty()) {
            continue;
        }
        size_t best = loops.size();
        for (size_t f : faces) {
            if (loops[f].second != component && pointInPolygon(loop.polygon.front(), loops[f].first.polygon) &&
                (best == loops.size() || loops[f].first.area < loops[best].first.area)) {
                best = f;
            }
        }
        if (best < loops.size()) {
            holes[best].push_back(i);
        }
    }
    for (size_t f : faces) {
        Region r;
        r.outer = loops[f].first;
        r.area = r.outer.area;
        for (size_t h : holes[f]) {
            r.holes.push_back(loops[h].first);
            r.area += loops[h].first.area; // negative
        }
        result.regions.push_back(std::move(r));
    }
    std::sort(result.regions.begin(), result.regions.end(), [](const Region& a, const Region& b) { return a.area > b.area; });
    return result;
}

bool regionContains(const Region& r, P2 p) {
    if (!pointInPolygon(p, r.outer.polygon)) {
        return false;
    }
    return std::none_of(r.holes.begin(), r.holes.end(), [p](const Loop& h) { return pointInPolygon(p, h.polygon); });
}

// --- Sketch Doctor -------------------------------------------------------------------------------

const char* issueName(IssueKind kind) {
    switch (kind) {
    case IssueKind::Gap: return "Gap between ends";
    case IssueKind::OpenEnd: return "Open end";
    case IssueKind::ZeroLength: return "Zero-length entity";
    case IssueKind::Duplicate: return "Duplicate entity";
    case IssueKind::Overlap: return "Overlapping lines";
    case IssueKind::Crossing: return "Contours cross";
    case IssueKind::OverDefined: return "Over defined";
    }
    return "Issue";
}

std::vector<Issue> diagnose(const Sketch& s, double gap) {
    const double tol = tolerance(s);
    std::vector<Issue> out;
    std::vector<const Curve*> real;
    for (const Curve& k : s.curves()) {
        if (!k.construction) {
            real.push_back(&k);
        }
    }

    // Tiny curves.
    std::vector<Id> tiny;
    for (const Curve* k : real) {
        const double size = k->type == CurveType::Line ? length(pointOf(s, k->p2) - pointOf(s, k->p1))
                            : k->type == CurveType::Arc ? radiusOf(s, *k) * arcSweep(s, *k)
                                                         : k->radius;
        if (size < 1e3 * tol) {
            out.push_back({IssueKind::ZeroLength, {k->id}, midpointOf(s, *k), true});
            tiny.push_back(k->id);
        }
    }
    auto isTiny = [&tiny](Id id) { return std::find(tiny.begin(), tiny.end(), id) != tiny.end(); };

    // Duplicates and overlaps.
    std::vector<Id> duplicates;
    for (size_t i = 0; i < real.size(); ++i) {
        for (size_t j = i + 1; j < real.size(); ++j) {
            const Curve& a = *real[i];
            const Curve& b = *real[j];
            if (a.type != b.type || isTiny(a.id) || isTiny(b.id)) {
                continue;
            }
            auto same = [&](Id p, Id q) { return length(pointOf(s, p) - pointOf(s, q)) < 10.0 * tol; };
            bool duplicate = false;
            if (a.type == CurveType::Line) {
                duplicate = (same(a.p1, b.p1) && same(a.p2, b.p2)) || (same(a.p1, b.p2) && same(a.p2, b.p1));
                if (!duplicate && parallel(s, a, b, 1e-9) && distanceToCurve(s, b, pointOf(s, a.p1)) < 1e3 * tol) {
                    // Collinear: do they share more than a point?
                    const P2 o = pointOf(s, a.p1), u = normalized(pointOf(s, a.p2) - o);
                    const double a0 = 0.0, a1 = dot(pointOf(s, a.p2) - o, u);
                    const double b0 = dot(pointOf(s, b.p1) - o, u), b1 = dot(pointOf(s, b.p2) - o, u);
                    const double overlap = std::min(a1, std::max(b0, b1)) - std::max(a0, std::min(b0, b1));
                    // Only lines lying on the same carrier (not merely parallel nearby).
                    const bool sameCarrier = std::abs(cross(pointOf(s, b.p1) - o, u)) < 10.0 * tol;
                    if (sameCarrier && overlap > 10.0 * tol) {
                        out.push_back({IssueKind::Overlap, {a.id, b.id}, o + u * (std::max(a0, std::min(b0, b1)) + 0.5 * overlap), false});
                    }
                }
            } else if (a.type == CurveType::Circle) {
                duplicate = same(a.center, b.center) && std::abs(a.radius - b.radius) < 10.0 * tol;
            } else {
                duplicate = same(a.center, b.center) && same(a.p1, b.p1) && same(a.p2, b.p2);
            }
            if (duplicate && std::find(duplicates.begin(), duplicates.end(), b.id) == duplicates.end()) {
                duplicates.push_back(b.id);
                out.push_back({IssueKind::Duplicate, {b.id, a.id}, midpointOf(s, b), true});
            }
        }
    }

    auto skipped = [&](Id id) {
        return isTiny(id) || std::find(duplicates.begin(), duplicates.end(), id) != duplicates.end();
    };
    // Ends that join nothing (another end, or a point along another curve), and gaps between them.
    struct End {
        Id point;
        P2 at;
    };
    std::vector<End> open;
    for (const Curve* k : real) {
        if (k->type == CurveType::Circle || skipped(k->id)) {
            continue;
        }
        for (Id e : {k->p1, k->p2}) {
            const P2 at = pointOf(s, e);
            const bool joined = std::any_of(real.begin(), real.end(), [&](const Curve* o) {
                if (o == k || skipped(o->id)) {
                    return false;
                }
                return distanceToCurve(s, *o, at) < 10.0 * tol;
            });
            if (!joined) {
                open.push_back({e, at});
            }
        }
    }
    std::vector<bool> paired(open.size(), false);
    for (size_t i = 0; i < open.size(); ++i) {
        if (paired[i]) {
            continue;
        }
        size_t best = open.size();
        double bestDistance = gap;
        for (size_t j = 0; j < open.size(); ++j) {
            if (j == i || paired[j] || s.point(open[j].point)->owner == s.point(open[i].point)->owner) {
                continue;
            }
            const double d = length(open[j].at - open[i].at);
            if (d <= bestDistance) {
                bestDistance = d;
                best = j;
            }
        }
        if (best < open.size()) {
            paired[i] = paired[best] = true;
            out.push_back({IssueKind::Gap, {open[i].point, open[best].point}, (open[i].at + open[best].at) * 0.5, true});
        }
    }
    for (size_t i = 0; i < open.size(); ++i) {
        if (!paired[i]) {
            out.push_back({IssueKind::OpenEnd, {open[i].point}, open[i].at, false});
        }
    }

    // Contours crossing each other away from their ends.
    for (size_t i = 0; i < real.size(); ++i) {
        for (size_t j = i + 1; j < real.size(); ++j) {
            if (std::find(duplicates.begin(), duplicates.end(), real[j]->id) != duplicates.end()) {
                continue;
            }
            for (const P2& p : intersections(s, *real[i], *real[j])) {
                auto atEnd = [&](const Curve* k) {
                    return k->type != CurveType::Circle &&
                           (length(pointOf(s, k->p1) - p) < 10.0 * tol || length(pointOf(s, k->p2) - p) < 10.0 * tol);
                };
                if (!atEnd(real[i]) && !atEnd(real[j])) {
                    out.push_back({IssueKind::Crossing, {real[i]->id, real[j]->id}, p, false});
                }
            }
        }
    }

    // Relations that cannot all hold, or say the same thing twice.
    Sketch trial = s;
    const SolveResult r = solve(trial);
    if (!r.conflicting.empty() || !r.redundant.empty()) {
        Issue issue;
        issue.kind = IssueKind::OverDefined;
        issue.ids = r.converged ? r.redundant : r.conflicting;
        issue.repairable = r.converged && !r.redundant.empty();
        if (!issue.ids.empty()) {
            if (const Constraint* c = s.constraint(issue.ids.front()); c && !c->refs.empty()) {
                const Id first = c->refs.front();
                issue.at = s.point(first) ? pointOf(s, first) : s.curve(first) ? midpointOf(s, *s.curve(first)) : P2{};
            }
        }
        out.push_back(issue);
    }
    return out;
}

bool repair(Sketch& s, const Issue& issue) {
    switch (issue.kind) {
    case IssueKind::Gap:
        return issue.ids.size() == 2 && s.addRelation(ConstraintType::Coincident, issue.ids) != 0;
    case IssueKind::ZeroLength:
    case IssueKind::Duplicate: return !issue.ids.empty() && s.remove(issue.ids.front());
    case IssueKind::OverDefined: {
        if (!issue.repairable) {
            return false;
        }
        bool any = false;
        for (Id id : issue.ids) {
            any = s.remove(id) || any;
        }
        return any;
    }
    case IssueKind::OpenEnd:
    case IssueKind::Overlap:
    case IssueKind::Crossing: return false;
    }
    return false;
}

} // namespace bl::sketch
