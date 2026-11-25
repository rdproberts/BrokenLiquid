#include "sketch/Sketch.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace bl::sketch {

bool isDimension(ConstraintType type) {
    switch (type) {
    case ConstraintType::Distance:
    case ConstraintType::HorizontalDistance:
    case ConstraintType::VerticalDistance:
    case ConstraintType::Angle:
    case ConstraintType::Radius:
    case ConstraintType::Diameter: return true;
    default: return false;
    }
}

const char* typeName(ConstraintType type) {
    switch (type) {
    case ConstraintType::Coincident: return "Coincident";
    case ConstraintType::Horizontal: return "Horizontal";
    case ConstraintType::Vertical: return "Vertical";
    case ConstraintType::Parallel: return "Parallel";
    case ConstraintType::Perpendicular: return "Perpendicular";
    case ConstraintType::Collinear: return "Collinear";
    case ConstraintType::Equal: return "Equal";
    case ConstraintType::Tangent: return "Tangent";
    case ConstraintType::Concentric: return "Concentric";
    case ConstraintType::Midpoint: return "Midpoint";
    case ConstraintType::PointOnCurve: return "On Curve";
    case ConstraintType::Symmetric: return "Symmetric";
    case ConstraintType::Fix: return "Fix";
    case ConstraintType::Distance: return "Distance";
    case ConstraintType::HorizontalDistance: return "Horizontal Distance";
    case ConstraintType::VerticalDistance: return "Vertical Distance";
    case ConstraintType::Angle: return "Angle";
    case ConstraintType::Radius: return "Radius";
    case ConstraintType::Diameter: return "Diameter";
    }
    return "Relation";
}

namespace {
// Name counters: one per curve type, one for points, one for all dimensions, one per relation type.
constexpr int kPointKind = 100;
constexpr int kDimensionKind = 101;
int curveKind(CurveType type) { return 200 + static_cast<int>(type); }
} // namespace

Sketch::Sketch() {
    m_origin = addPoint(0.0, 0.0);
    m_points.back().fixed = true;
    m_points.back().number = 0;
    m_counters.clear();
}

Id Sketch::addPoint(double x, double y) {
    Point p;
    p.id = m_next++;
    p.number = nextNumber(kPointKind);
    p.x = x;
    p.y = y;
    m_points.push_back(p);
    return p.id;
}

Id Sketch::addLine(double x1, double y1, double x2, double y2) {
    Curve c;
    c.id = m_next++;
    c.type = CurveType::Line;
    c.number = nextNumber(curveKind(c.type));
    c.p1 = addPoint(x1, y1);
    c.p2 = addPoint(x2, y2);
    point(c.p1)->owner = c.id;
    point(c.p2)->owner = c.id;
    m_curves.push_back(c);
    return c.id;
}

Id Sketch::addCircle(double cx, double cy, double r) {
    Curve c;
    c.id = m_next++;
    c.type = CurveType::Circle;
    c.number = nextNumber(curveKind(c.type));
    c.center = addPoint(cx, cy);
    point(c.center)->owner = c.id;
    c.radius = r;
    m_curves.push_back(c);
    return c.id;
}

Id Sketch::addArc(double cx, double cy, double sx, double sy, double ex, double ey) {
    const double r = std::hypot(sx - cx, sy - cy);
    const double a = std::atan2(ey - cy, ex - cx);
    Curve c;
    c.id = m_next++;
    c.type = CurveType::Arc;
    c.number = nextNumber(curveKind(c.type));
    c.center = addPoint(cx, cy);
    c.p1 = addPoint(sx, sy);
    c.p2 = addPoint(cx + r * std::cos(a), cy + r * std::sin(a));
    for (Id p : {c.center, c.p1, c.p2}) {
        point(p)->owner = c.id;
    }
    m_curves.push_back(c);
    return c.id;
}

Id Sketch::addConstraint(Constraint c) {
    c.id = m_next++;
    c.number = nextNumber(isDimension(c.type) ? kDimensionKind : static_cast<int>(c.type));
    m_constraints.push_back(std::move(c));
    return m_constraints.back().id;
}

Id Sketch::addRelation(ConstraintType type, std::vector<Id> refs) {
    Constraint c;
    c.type = type;
    c.refs = std::move(refs);
    if (type == ConstraintType::Fix) {
        if (c.refs.size() != 1) {
            return 0;
        }
        if (const Point* p = point(c.refs[0])) {
            c.anchor = {p->x, p->y};
        } else if (const Curve* k = curve(c.refs[0])) {
            for (Id pid : {k->center, k->p1, k->p2}) {
                if (const Point* q = point(pid)) {
                    c.anchor.push_back(q->x);
                    c.anchor.push_back(q->y);
                }
            }
            if (k->type == CurveType::Circle) {
                c.anchor.push_back(k->radius);
            }
        } else {
            return 0;
        }
    } else if (type == ConstraintType::Tangent && c.refs.size() == 2) {
        const Curve* a = curve(c.refs[0]);
        const Curve* b = curve(c.refs[1]);
        if (!a || !b || (a->type == CurveType::Line && b->type == CurveType::Line)) {
            return 0;
        }
        if (a->type != CurveType::Line && b->type != CurveType::Line) {
            const Point* ca = point(a->center);
            const Point* cb = point(b->center);
            const double ra = a->type == CurveType::Circle ? a->radius : arcRadius(*a);
            const double rb = b->type == CurveType::Circle ? b->radius : arcRadius(*b);
            const double d = std::hypot(cb->x - ca->x, cb->y - ca->y);
            c.variant = std::abs(d - (ra + rb)) <= std::abs(d - std::abs(ra - rb)) ? 0 : 1;
        } else if (b->type == CurveType::Line) {
            std::swap(c.refs[0], c.refs[1]); // keep the line first
        }
    }
    return addConstraint(std::move(c));
}

Id Sketch::addDimension(ConstraintType type, std::vector<Id> refs, std::optional<double> value) {
    Constraint c;
    c.type = type;
    c.refs = std::move(refs);
    const std::optional<double> measured = measure(c);
    if (!value && !measured) {
        return 0;
    }
    c.value = value.value_or(*measured);
    return addConstraint(std::move(c));
}

void Sketch::renumber(Id id) {
    if (Curve* c = curve(id)) {
        c->number = nextNumber(curveKind(c->type));
    }
}

bool Sketch::remove(Id id) {
    if (id == m_origin) {
        return false;
    }
    std::vector<Id> gone{id};
    if (const Curve* c = curve(id)) {
        for (Id p : {c->p1, c->p2, c->center}) {
            if (p) {
                gone.push_back(p);
            }
        }
    } else if (const Point* p = point(id); p && p->owner) {
        return remove(p->owner); // a curve's own point goes with its curve
    } else if (!p && !constraint(id)) {
        return false;
    }
    auto isGone = [&gone](Id x) { return std::find(gone.begin(), gone.end(), x) != gone.end(); };
    std::erase_if(m_points, [&](const Point& p) { return isGone(p.id); });
    std::erase_if(m_curves, [&](const Curve& c) { return isGone(c.id); });
    std::erase_if(m_constraints, [&](const Constraint& c) {
        return isGone(c.id) || std::any_of(c.refs.begin(), c.refs.end(), isGone);
    });
    return true;
}

Point* Sketch::point(Id id) {
    auto it = std::find_if(m_points.begin(), m_points.end(), [id](const Point& p) { return p.id == id; });
    return it == m_points.end() ? nullptr : &*it;
}
const Point* Sketch::point(Id id) const { return const_cast<Sketch*>(this)->point(id); }

Curve* Sketch::curve(Id id) {
    auto it = std::find_if(m_curves.begin(), m_curves.end(), [id](const Curve& c) { return c.id == id; });
    return it == m_curves.end() ? nullptr : &*it;
}
const Curve* Sketch::curve(Id id) const { return const_cast<Sketch*>(this)->curve(id); }

Constraint* Sketch::constraint(Id id) {
    auto it = std::find_if(m_constraints.begin(), m_constraints.end(), [id](const Constraint& c) { return c.id == id; });
    return it == m_constraints.end() ? nullptr : &*it;
}
const Constraint* Sketch::constraint(Id id) const { return const_cast<Sketch*>(this)->constraint(id); }

double Sketch::arcRadius(const Curve& arc) const {
    const Point* c = point(arc.center);
    const Point* s = point(arc.p1);
    return c && s ? std::hypot(s->x - c->x, s->y - c->y) : 0.0;
}

std::optional<double> Sketch::measure(const Constraint& c) const {
    auto pt = [this](Id id) { return point(id); };
    auto len = [this](const Curve* l) {
        const Point* a = point(l->p1);
        const Point* b = point(l->p2);
        return std::hypot(b->x - a->x, b->y - a->y);
    };
    const std::vector<Id>& r = c.refs;
    switch (c.type) {
    case ConstraintType::Distance:
        if (r.size() == 1) {
            if (const Curve* l = curve(r[0]); l && l->type == CurveType::Line) {
                return len(l);
            }
        } else if (r.size() == 2) {
            const Point* a = pt(r[0]);
            const Point* b = pt(r[1]);
            if (a && b) {
                return std::hypot(b->x - a->x, b->y - a->y);
            }
            const Curve* l = curve(r[1]);
            if (!a && curve(r[0]) && curve(r[0])->type == CurveType::Line) {
                a = pt(curve(r[0])->p1); // parallel lines: from a point of the first
            }
            if (a && l && l->type == CurveType::Line) {
                const Point* p1 = pt(l->p1);
                const Point* p2 = pt(l->p2);
                const double dx = p2->x - p1->x, dy = p2->y - p1->y;
                const double n = std::hypot(dx, dy);
                return n > 0 ? std::abs(dx * (a->y - p1->y) - dy * (a->x - p1->x)) / n : 0.0;
            }
        }
        return std::nullopt;
    case ConstraintType::HorizontalDistance:
    case ConstraintType::VerticalDistance:
        if (r.size() == 2 && pt(r[0]) && pt(r[1])) {
            return c.type == ConstraintType::HorizontalDistance ? std::abs(pt(r[1])->x - pt(r[0])->x)
                                                                : std::abs(pt(r[1])->y - pt(r[0])->y);
        }
        return std::nullopt;
    case ConstraintType::Angle:
        if (r.size() == 2 && curve(r[0]) && curve(r[1])) {
            const Curve* a = curve(r[0]);
            const Curve* b = curve(r[1]);
            const double ax = pt(a->p2)->x - pt(a->p1)->x, ay = pt(a->p2)->y - pt(a->p1)->y;
            const double bx = pt(b->p2)->x - pt(b->p1)->x, by = pt(b->p2)->y - pt(b->p1)->y;
            const double angle = std::abs(std::atan2(ax * by - ay * bx, ax * bx + ay * by));
            return c.variant == 1 ? std::numbers::pi - angle : angle;
        }
        return std::nullopt;
    case ConstraintType::Radius:
    case ConstraintType::Diameter:
        if (r.size() == 1 && curve(r[0])) {
            const Curve* k = curve(r[0]);
            const double radius = k->type == CurveType::Circle ? k->radius : arcRadius(*k);
            return c.type == ConstraintType::Radius ? radius : 2.0 * radius;
        }
        return std::nullopt;
    default: return std::nullopt;
    }
}

std::string Sketch::name(Id id) const {
    if (const Curve* c = curve(id)) {
        switch (c->type) {
        case CurveType::Line: return "Line" + std::to_string(c->number);
        case CurveType::Circle: return "Circle" + std::to_string(c->number);
        case CurveType::Arc: return "Arc" + std::to_string(c->number);
        }
    }
    if (id == m_origin) {
        return "Origin";
    }
    if (const Point* p = point(id)) {
        return "Point" + std::to_string(p->number);
    }
    if (const Constraint* c = constraint(id)) {
        return (isDimension(c->type) ? std::string("D") : std::string(typeName(c->type))) + std::to_string(c->number);
    }
    return "Item" + std::to_string(id);
}

} // namespace bl::sketch
