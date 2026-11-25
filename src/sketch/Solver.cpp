#include "sketch/Solver.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <unordered_map>

namespace bl::sketch {

const char* statusName(SketchStatus status) {
    switch (status) {
    case SketchStatus::UnderDefined: return "Under Defined";
    case SketchStatus::FullyDefined: return "Fully Defined";
    case SketchStatus::OverDefined: return "Over Defined";
    }
    return "";
}

bool SolveResult::isDefined(Id id) const { return std::find(defined.begin(), defined.end(), id) != defined.end(); }

namespace {

using Vec = std::vector<double>;
using Fn = std::function<double(const Vec&)>;

struct Equation {
    Id constraint = 0;      // 0: internal (an arc keeps its end point on its circle)
    std::vector<int> slots; // values it reads
    Fn f;
};

// Signed distance of point p from the line through a and b (slots of x coordinates).
double lineDistance(const Vec& x, int a, int b, int p) {
    const double dx = x[b] - x[a], dy = x[b + 1] - x[a + 1];
    const double n = std::hypot(dx, dy);
    return n > 1e-300 ? (dx * (x[p + 1] - x[a + 1]) - dy * (x[p] - x[a])) / n : std::hypot(x[p] - x[a], x[p + 1] - x[a + 1]);
}

// The equations of one sketch, over a flat value vector x (point x, y pairs and circle radii).
class System {
public:
    System(const Sketch& sketch, Id skip) : m_sketch(sketch), m_skip(skip) {
        for (const Point& p : sketch.points()) {
            m_pointSlot[p.id] = static_cast<int>(x.size());
            x.push_back(p.x);
            x.push_back(p.y);
            column.push_back(p.fixed ? -1 : 0);
            column.push_back(p.fixed ? -1 : 0);
        }
        for (const Curve& c : sketch.curves()) {
            if (c.type == CurveType::Circle) {
                m_radiusSlot[c.id] = static_cast<int>(x.size());
                x.push_back(c.radius);
                column.push_back(0);
            }
        }
        for (int& c : column) {
            if (c == 0) {
                c = static_cast<int>(slotOf.size());
                slotOf.push_back(static_cast<int>(&c - column.data()));
            }
        }
        // Arcs keep both end points on one circle.
        for (const Curve& c : sketch.curves()) {
            if (c.type == CurveType::Arc) {
                const int ce = P(c.center), s = P(c.p1), e = P(c.p2);
                add(0, {ce, ce + 1, s, s + 1, e, e + 1}, [ce, s, e](const Vec& v) {
                    return std::hypot(v[e] - v[ce], v[e + 1] - v[ce + 1]) - std::hypot(v[s] - v[ce], v[s + 1] - v[ce + 1]);
                });
            }
        }
        for (const Constraint& c : sketch.constraints()) {
            if (c.id != skip && (c.driving || !isDimension(c.type))) {
                build(c);
            }
        }
    }

    int variables() const { return static_cast<int>(slotOf.size()); }

    Eigen::VectorXd residuals(const Vec& v) const {
        Eigen::VectorXd f(static_cast<Eigen::Index>(eqs.size()));
        for (size_t i = 0; i < eqs.size(); ++i) {
            f[static_cast<Eigen::Index>(i)] = eqs[i].f(v);
        }
        return f;
    }

    // Central differences, one equation at a time over the few values it reads.
    Eigen::SparseMatrix<double> jacobian(Vec& v) const {
        std::vector<Eigen::Triplet<double>> t;
        t.reserve(eqs.size() * 4);
        for (size_t i = 0; i < eqs.size(); ++i) {
            for (int s : eqs[i].slots) {
                if (column[s] < 0) {
                    continue;
                }
                const double keep = v[s];
                const double h = 1e-7 * std::max(1.0, std::abs(keep));
                v[s] = keep + h;
                const double fp = eqs[i].f(v);
                v[s] = keep - h;
                const double fm = eqs[i].f(v);
                v[s] = keep;
                const double d = (fp - fm) / (2.0 * h);
                if (d != 0.0) {
                    t.emplace_back(static_cast<int>(i), column[s], d);
                }
            }
        }
        Eigen::SparseMatrix<double> j(static_cast<Eigen::Index>(eqs.size()), variables());
        j.setFromTriplets(t.begin(), t.end());
        return j;
    }

    void writeBack(Sketch& sketch) const {
        for (Point& p : sketch.points()) {
            const int s = m_pointSlot.at(p.id);
            p.x = x[s];
            p.y = x[s + 1];
        }
        for (Curve& c : sketch.curves()) {
            if (c.type == CurveType::Circle) {
                c.radius = x[m_radiusSlot.at(c.id)];
            }
        }
    }

    int pointSlot(Id id) const { return m_pointSlot.at(id); }
    int radiusSlot(Id id) const {
        const auto it = m_radiusSlot.find(id);
        return it == m_radiusSlot.end() ? -1 : it->second;
    }

    Vec x;
    std::vector<int> column; // value slot -> Jacobian column (-1: fixed)
    std::vector<int> slotOf; // column -> value slot
    std::vector<Equation> eqs;

private:
    int P(Id id) const {
        const auto it = m_pointSlot.find(id);
        return it == m_pointSlot.end() ? -1 : it->second;
    }

    void add(Id constraint, std::vector<int> slots, Fn f) {
        // A value may be read twice (an arc's centre in both the tangent and the radius term); the
        // Jacobian must differentiate it once.
        std::sort(slots.begin(), slots.end());
        slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
        eqs.push_back({constraint, std::move(slots), std::move(f)});
    }

    // Radius of a circle or arc as an expression, plus the values it reads.
    bool radius(const Curve* c, Fn& f, std::vector<int>& slots) const {
        if (!c || c->type == CurveType::Line) {
            return false;
        }
        if (c->type == CurveType::Circle) {
            const int r = m_radiusSlot.at(c->id);
            f = [r](const Vec& v) { return v[r]; };
            slots = {r};
        } else {
            const int ce = P(c->center), s = P(c->p1);
            f = [ce, s](const Vec& v) { return std::hypot(v[s] - v[ce], v[s + 1] - v[ce + 1]); };
            slots = {ce, ce + 1, s, s + 1};
        }
        return true;
    }

    static std::vector<int> join(std::initializer_list<std::vector<int>> parts) {
        std::vector<int> all;
        for (const auto& p : parts) {
            all.insert(all.end(), p.begin(), p.end());
        }
        return all;
    }

    void build(const Constraint& c) {
        const Sketch& s = m_sketch;
        const std::vector<Id>& r = c.refs;
        const Id id = c.id;
        const double value = c.value;
        auto line = [&s](Id i) {
            const Curve* k = s.curve(i);
            return k && k->type == CurveType::Line ? k : nullptr;
        };
        auto round = [&s](Id i) {
            const Curve* k = s.curve(i);
            return k && k->type != CurveType::Line ? k : nullptr;
        };
        auto isPoint = [&s](Id i) { return s.point(i) != nullptr; };
        auto pts = [](int a, int b) { return std::vector<int>{a, a + 1, b, b + 1}; };

        switch (c.type) {
        case ConstraintType::Coincident:
        case ConstraintType::Concentric: {
            int a = -1, b = -1;
            if (c.type == ConstraintType::Coincident && r.size() == 2 && isPoint(r[0]) && isPoint(r[1])) {
                a = P(r[0]), b = P(r[1]);
            } else if (c.type == ConstraintType::Concentric && r.size() == 2 && round(r[0]) && round(r[1])) {
                a = P(round(r[0])->center), b = P(round(r[1])->center);
            }
            if (a >= 0 && b >= 0) {
                add(id, pts(a, b), [a, b](const Vec& v) { return v[a] - v[b]; });
                add(id, pts(a, b), [a, b](const Vec& v) { return v[a + 1] - v[b + 1]; });
            }
            break;
        }
        case ConstraintType::Horizontal:
        case ConstraintType::Vertical: {
            int a = -1, b = -1;
            if (r.size() == 1 && line(r[0])) {
                a = P(line(r[0])->p1), b = P(line(r[0])->p2);
            } else if (r.size() == 2 && isPoint(r[0]) && isPoint(r[1])) {
                a = P(r[0]), b = P(r[1]);
            }
            if (a >= 0) {
                const int o = c.type == ConstraintType::Horizontal ? 1 : 0;
                add(id, pts(a, b), [a, b, o](const Vec& v) { return v[a + o] - v[b + o]; });
            }
            break;
        }
        case ConstraintType::Parallel:
        case ConstraintType::Perpendicular:
        case ConstraintType::Angle: {
            if (r.size() != 2 || !line(r[0]) || !line(r[1])) {
                break;
            }
            const int a = P(line(r[0])->p1), b = P(line(r[0])->p2), p = P(line(r[1])->p1), q = P(line(r[1])->p2);
            const ConstraintType type = c.type;
            const bool supplement = c.variant == 1;
            add(id, join({pts(a, b), pts(p, q)}), [a, b, p, q, type, value, supplement](const Vec& v) {
                const double ux = v[b] - v[a], uy = v[b + 1] - v[a + 1];
                const double wx = v[q] - v[p], wy = v[q + 1] - v[p + 1];
                const double n = std::max(1e-300, std::hypot(ux, uy) * std::hypot(wx, wy));
                const double cross = (ux * wy - uy * wx) / n, dot = (ux * wx + uy * wy) / n;
                if (type == ConstraintType::Parallel) {
                    return cross;
                }
                if (type == ConstraintType::Perpendicular) {
                    return dot;
                }
                const double angle = std::abs(std::atan2(cross, dot));
                return (supplement ? std::numbers::pi - angle : angle) - value;
            });
            break;
        }
        case ConstraintType::Collinear: {
            if (r.size() != 2 || !line(r[0]) || !line(r[1])) {
                break;
            }
            const int a = P(line(r[0])->p1), b = P(line(r[0])->p2);
            for (Id pid : {line(r[1])->p1, line(r[1])->p2}) {
                const int p = P(pid);
                add(id, join({pts(a, b), {p, p + 1}}), [a, b, p](const Vec& v) { return lineDistance(v, a, b, p); });
            }
            break;
        }
        case ConstraintType::Equal: {
            if (r.size() != 2) {
                break;
            }
            if (line(r[0]) && line(r[1])) {
                const int a = P(line(r[0])->p1), b = P(line(r[0])->p2), p = P(line(r[1])->p1), q = P(line(r[1])->p2);
                add(id, join({pts(a, b), pts(p, q)}), [a, b, p, q](const Vec& v) {
                    return std::hypot(v[b] - v[a], v[b + 1] - v[a + 1]) - std::hypot(v[q] - v[p], v[q + 1] - v[p + 1]);
                });
            } else {
                Fn r1, r2;
                std::vector<int> s1, s2;
                if (radius(round(r[0]), r1, s1) && radius(round(r[1]), r2, s2)) {
                    add(id, join({s1, s2}), [r1, r2](const Vec& v) { return r1(v) - r2(v); });
                }
            }
            break;
        }
        case ConstraintType::Tangent: {
            if (r.size() != 2) {
                break;
            }
            const Curve* k0 = s.curve(r[0]);
            const Curve* k1 = s.curve(r[1]);
            if (const Id touch = k0 && k1 ? touchPoint(*k0, *k1) : 0) {
                const int t = P(touch);
                if (line(r[0]) && round(r[1])) {
                    const int a = P(k0->p1), b = P(k0->p2), ce = P(k1->center);
                    add(id, join({pts(a, b), pts(t, ce)}), [a, b, t, ce](const Vec& v) {
                        const double ux = v[b] - v[a], uy = v[b + 1] - v[a + 1];
                        const double rx = v[t] - v[ce], ry = v[t + 1] - v[ce + 1];
                        return (ux * rx + uy * ry) / std::max(1e-300, std::hypot(ux, uy) * std::hypot(rx, ry));
                    });
                } else if (round(r[0]) && round(r[1])) {
                    const int c1 = P(k0->center), c2 = P(k1->center);
                    add(id, join({pts(t, c1), {c2, c2 + 1}}), [t, c1, c2](const Vec& v) {
                        const double ax = v[t] - v[c1], ay = v[t + 1] - v[c1 + 1];
                        const double bx = v[t] - v[c2], by = v[t + 1] - v[c2 + 1];
                        return (ax * by - ay * bx) / std::max(1e-300, std::hypot(ax, ay) * std::hypot(bx, by));
                    });
                }
                break;
            }
            Fn r2;
            std::vector<int> s2;
            if (line(r[0]) && radius(round(r[1]), r2, s2)) {
                const int a = P(line(r[0])->p1), b = P(line(r[0])->p2), ce = P(round(r[1])->center);
                add(id, join({pts(a, b), {ce, ce + 1}, s2}),
                    [a, b, ce, r2](const Vec& v) { return std::abs(lineDistance(v, a, b, ce)) - r2(v); });
            } else {
                Fn r1;
                std::vector<int> s1;
                if (radius(round(r[0]), r1, s1) && radius(round(r[1]), r2, s2)) {
                    const int c1 = P(round(r[0])->center), c2 = P(round(r[1])->center);
                    const bool inside = c.variant == 1;
                    add(id, join({pts(c1, c2), s1, s2}), [c1, c2, r1, r2, inside](const Vec& v) {
                        const double d = std::hypot(v[c2] - v[c1], v[c2 + 1] - v[c1 + 1]);
                        return inside ? d - std::abs(r1(v) - r2(v)) : d - (r1(v) + r2(v));
                    });
                }
            }
            break;
        }
        case ConstraintType::Midpoint: {
            if (r.size() != 2 || !isPoint(r[0]) || !line(r[1])) {
                break;
            }
            const int p = P(r[0]), a = P(line(r[1])->p1), b = P(line(r[1])->p2);
            for (int o : {0, 1}) {
                add(id, join({{p, p + 1}, pts(a, b)}), [p, a, b, o](const Vec& v) { return v[p + o] - 0.5 * (v[a + o] + v[b + o]); });
            }
            break;
        }
        case ConstraintType::PointOnCurve: {
            if (r.size() != 2 || !isPoint(r[0])) {
                break;
            }
            const int p = P(r[0]);
            if (line(r[1])) {
                const int a = P(line(r[1])->p1), b = P(line(r[1])->p2);
                add(id, join({pts(a, b), {p, p + 1}}), [a, b, p](const Vec& v) { return lineDistance(v, a, b, p); });
            } else {
                Fn rr;
                std::vector<int> sr;
                if (radius(round(r[1]), rr, sr)) {
                    const int ce = P(round(r[1])->center);
                    add(id, join({pts(p, ce), sr}),
                        [p, ce, rr](const Vec& v) { return std::hypot(v[p] - v[ce], v[p + 1] - v[ce + 1]) - rr(v); });
                }
            }
            break;
        }
        case ConstraintType::Symmetric: {
            if (r.size() != 3 || !isPoint(r[0]) || !isPoint(r[1]) || !line(r[2])) {
                break;
            }
            const int p = P(r[0]), q = P(r[1]), a = P(line(r[2])->p1), b = P(line(r[2])->p2);
            const auto slots = join({pts(p, q), pts(a, b)});
            add(id, slots, [p, q, a, b](const Vec& v) { // the midpoint of p and q lies on the axis
                const double mx = 0.5 * (v[p] + v[q]), my = 0.5 * (v[p + 1] + v[q + 1]);
                const double dx = v[b] - v[a], dy = v[b + 1] - v[a + 1];
                const double n = std::max(1e-300, std::hypot(dx, dy));
                return (dx * (my - v[a + 1]) - dy * (mx - v[a])) / n;
            });
            add(id, slots, [p, q, a, b](const Vec& v) {
                const double dx = v[b] - v[a], dy = v[b + 1] - v[a + 1];
                const double n = std::max(1e-300, std::hypot(dx, dy));
                return ((v[q] - v[p]) * dx + (v[q + 1] - v[p + 1]) * dy) / n;
            });
            break;
        }
        case ConstraintType::Fix: {
            if (r.size() != 1) {
                break;
            }
            std::vector<int> slots;
            if (isPoint(r[0])) {
                slots = {P(r[0]), P(r[0]) + 1};
            } else if (const Curve* k = s.curve(r[0])) {
                for (Id pid : {k->center, k->p1, k->p2}) {
                    if (pid) {
                        slots.push_back(P(pid));
                        slots.push_back(P(pid) + 1);
                    }
                }
                if (k->type == CurveType::Circle) {
                    slots.push_back(m_radiusSlot.at(k->id));
                }
            }
            for (size_t i = 0; i < slots.size() && i < c.anchor.size(); ++i) {
                const int slot = slots[i];
                const double target = c.anchor[i];
                add(id, {slot}, [slot, target](const Vec& v) { return v[slot] - target; });
            }
            break;
        }
        case ConstraintType::Distance: {
            if (r.size() == 1 && line(r[0])) {
                const int a = P(line(r[0])->p1), b = P(line(r[0])->p2);
                add(id, pts(a, b), [a, b, value](const Vec& v) { return std::hypot(v[b] - v[a], v[b + 1] - v[a + 1]) - value; });
            } else if (r.size() == 2 && isPoint(r[0]) && isPoint(r[1])) {
                const int a = P(r[0]), b = P(r[1]);
                add(id, pts(a, b), [a, b, value](const Vec& v) { return std::hypot(v[b] - v[a], v[b + 1] - v[a + 1]) - value; });
            } else if (r.size() == 2 && (isPoint(r[0]) || line(r[0])) && line(r[1])) {
                const int p = isPoint(r[0]) ? P(r[0]) : P(line(r[0])->p1);
                const int a = P(line(r[1])->p1), b = P(line(r[1])->p2);
                add(id, join({pts(a, b), {p, p + 1}}),
                    [a, b, p, value](const Vec& v) { return std::abs(lineDistance(v, a, b, p)) - value; });
            }
            break;
        }
        case ConstraintType::HorizontalDistance:
        case ConstraintType::VerticalDistance: {
            if (r.size() != 2 || !isPoint(r[0]) || !isPoint(r[1])) {
                break;
            }
            const int a = P(r[0]), b = P(r[1]);
            const int o = c.type == ConstraintType::HorizontalDistance ? 0 : 1;
            add(id, pts(a, b), [a, b, o, value](const Vec& v) { return std::abs(v[b + o] - v[a + o]) - value; });
            break;
        }
        case ConstraintType::Radius:
        case ConstraintType::Diameter: {
            Fn rr;
            std::vector<int> sr;
            if (r.size() == 1 && radius(round(r[0]), rr, sr)) {
                const double k = c.type == ConstraintType::Diameter ? 2.0 : 1.0;
                add(id, sr, [rr, k, value](const Vec& v) { return k * rr(v) - value; });
            }
            break;
        }
        }
    }

    // A point the other constraints already put on both curves (a shared end point), or 0. Tangency
    // there must be written as "the radius is perpendicular to the line" (or "the centres and the
    // point are collinear"): distance-minus-radius has a zero gradient at such a point, which would
    // look like a missing equation to the solver and to the DOF count.
    Id touchPoint(const Curve& a, const Curve& b) const {
        auto ends = [](const Curve& c) {
            return c.type == CurveType::Circle ? std::vector<Id>{} : std::vector<Id>{c.p1, c.p2};
        };
        auto related = [this](ConstraintType type, Id x, Id y) {
            for (const Constraint& k : m_sketch.constraints()) {
                if (k.id != m_skip && k.type == type && k.refs.size() == 2 &&
                    ((k.refs[0] == x && k.refs[1] == y) ||
                     (type == ConstraintType::Coincident && k.refs[0] == y && k.refs[1] == x))) {
                    return true;
                }
            }
            return false;
        };
        for (Id pa : ends(a)) {
            for (Id pb : ends(b)) {
                if (related(ConstraintType::Coincident, pa, pb)) {
                    return b.type == CurveType::Line ? pa : pb; // a point of the round curve is on its circle
                }
            }
        }
        for (Id pa : ends(a)) {
            if (related(ConstraintType::PointOnCurve, pa, b.id)) {
                return pa;
            }
        }
        for (Id pb : ends(b)) {
            if (related(ConstraintType::PointOnCurve, pb, a.id)) {
                return pb;
            }
        }
        return 0;
    }

    const Sketch& m_sketch;
    Id m_skip = 0;
    std::unordered_map<Id, int> m_pointSlot;
    std::unordered_map<Id, int> m_radiusSlot;
};

double maxAbs(const Eigen::VectorXd& f) { return f.size() ? f.cwiseAbs().maxCoeff() : 0.0; }

// Damped, weighted minimum-norm Gauss-Newton. `invWeight` per Jacobian column: small values make a
// value expensive to move. Returns true when every residual is within tolerance.
bool iterate(System& sys, const Eigen::VectorXd& invWeight, int maxIterations, double tolerance, int& iterations,
             double& residual) {
    const auto m = static_cast<Eigen::Index>(sys.eqs.size());
    Eigen::VectorXd f = sys.residuals(sys.x);
    residual = maxAbs(f);
    iterations = 0;
    if (m == 0 || residual < tolerance) {
        return residual < tolerance;
    }
    double lambda = 1e-9;
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ldlt;
    Eigen::SparseMatrix<double> identity(m, m);
    identity.setIdentity();
    for (iterations = 1; iterations <= maxIterations; ++iterations) {
        const Eigen::SparseMatrix<double> j = sys.jacobian(sys.x);
        const Eigen::SparseMatrix<double> jw = j * invWeight.asDiagonal();
        const Eigen::SparseMatrix<double> a = jw * j.transpose();
        double scale = 1.0;
        for (Eigen::Index i = 0; i < m; ++i) {
            scale = std::max(scale, a.coeff(i, i));
        }
        const double before = f.squaredNorm();
        bool accepted = false;
        for (int attempt = 0; attempt < 12 && !accepted; ++attempt) {
            const Eigen::SparseMatrix<double> damped = a + (lambda * scale) * identity;
            ldlt.compute(damped);
            if (ldlt.info() != Eigen::Success) {
                lambda *= 10.0;
                continue;
            }
            const Eigen::VectorXd y = ldlt.solve(-f);
            const Eigen::VectorXd dx = jw.transpose() * y;
            Vec trial = sys.x;
            for (Eigen::Index c = 0; c < dx.size(); ++c) {
                trial[sys.slotOf[c]] += dx[c];
            }
            const Eigen::VectorXd ft = sys.residuals(trial);
            if (ft.allFinite() && ft.squaredNorm() < before) {
                sys.x = std::move(trial);
                f = ft;
                lambda = std::max(lambda * 0.1, 1e-12);
                accepted = true;
            } else {
                lambda *= 10.0;
            }
        }
        residual = maxAbs(f);
        if (residual < tolerance) {
            return true;
        }
        if (!accepted) {
            return false; // stalled: no step reduces the error
        }
    }
    return false;
}

struct Analysis {
    int rank = 0;
    std::vector<bool> free;      // per column
    std::vector<Id> redundant;
};

Analysis analyse(System& sys) {
    Analysis out;
    const int n = sys.variables();
    const auto m = static_cast<Eigen::Index>(sys.eqs.size());
    out.free.assign(static_cast<size_t>(n), true);
    if (n == 0) {
        return out;
    }
    if (m == 0) {
        return out;
    }
    const Eigen::MatrixXd j = Eigen::MatrixXd(sys.jacobian(sys.x));

    // Rank and row space from a pivoted QR of J^T; a value is free if it has a component outside
    // the row space (it can move without changing any equation, to first order).
    Eigen::ColPivHouseholderQR<Eigen::MatrixXd> qr(j.transpose());
    qr.setThreshold(1e-9);
    out.rank = static_cast<int>(qr.rank());
    const Eigen::MatrixXd q = Eigen::MatrixXd(qr.householderQ()).leftCols(out.rank);
    for (int c = 0; c < n; ++c) {
        out.free[static_cast<size_t>(c)] = 1.0 - q.row(c).squaredNorm() > 1e-7;
    }

    // Redundant constraints: greedy, in creation order, so the newest relation gets the blame.
    if (out.rank < m) {
        std::vector<Eigen::VectorXd> basis;
        for (Eigen::Index i = 0; i < m; ++i) {
            Eigen::VectorXd v = j.row(i).transpose();
            const double norm = v.norm();
            bool independent = false;
            if (norm > 1e-12) {
                v /= norm;
                for (int pass = 0; pass < 2; ++pass) {
                    for (const Eigen::VectorXd& b : basis) {
                        v -= v.dot(b) * b;
                    }
                }
                const double rest = v.norm();
                if (rest > 1e-6) {
                    basis.push_back(v / rest);
                    independent = true;
                }
            }
            const Id cid = sys.eqs[static_cast<size_t>(i)].constraint;
            if (!independent && cid != 0 && std::find(out.redundant.begin(), out.redundant.end(), cid) == out.redundant.end()) {
                out.redundant.push_back(cid);
            }
        }
    }
    return out;
}

} // namespace

SolveResult solve(Sketch& sketch, const SolveOptions& options) {
    SolveResult result;
    System sys(sketch, 0);

    Eigen::VectorXd invWeight = Eigen::VectorXd::Ones(sys.variables());
    std::vector<DragTarget> drags = options.moreDrags;
    if (options.dragPoint) {
        drags.push_back({options.dragPoint, options.dragX, options.dragY});
    }
    for (const DragTarget& d : drags) {
        if (const Point* p = sketch.point(d.point); p && !p->fixed) {
            const int s = sys.pointSlot(d.point);
            sys.x[s] = d.x;
            sys.x[s + 1] = d.y;
            invWeight[sys.column[s]] = 1e-6; // stay under the pointer if at all possible
            invWeight[sys.column[s + 1]] = 1e-6;
        }
    }

    result.converged = iterate(sys, invWeight, options.maxIterations, options.tolerance, result.iterations, result.residual);
    if (result.converged) {
        sys.writeBack(sketch);
    }
    if (!options.analyse) {
        return result;
    }

    if (!result.converged) {
        // Name the constraints that cannot all hold: removing any one of them makes it solvable.
        result.status = SketchStatus::OverDefined;
        for (const Constraint& c : sketch.constraints()) {
            if (!c.driving && isDimension(c.type)) {
                continue;
            }
            System trial(sketch, c.id);
            int it = 0;
            double res = 0.0;
            if (iterate(trial, Eigen::VectorXd::Ones(trial.variables()), 40, options.tolerance, it, res)) {
                result.conflicting.push_back(c.id);
            }
        }
        return result;
    }

    const Analysis a = analyse(sys);
    result.dof = sys.variables() - a.rank;
    result.redundant = a.redundant;

    auto pointDefined = [&](Id id) {
        const int s = sys.pointSlot(id);
        const int cx = sys.column[s], cy = sys.column[s + 1];
        return (cx < 0 || !a.free[static_cast<size_t>(cx)]) && (cy < 0 || !a.free[static_cast<size_t>(cy)]);
    };
    for (const Point& p : sketch.points()) {
        if (pointDefined(p.id)) {
            result.defined.push_back(p.id);
        }
    }
    for (const Curve& c : sketch.curves()) {
        bool ok = true;
        for (Id pid : {c.center, c.p1, c.p2}) {
            ok = ok && (pid == 0 || pointDefined(pid));
        }
        if (c.type == CurveType::Circle) {
            const int col = sys.column[sys.radiusSlot(c.id)];
            ok = ok && !a.free[static_cast<size_t>(col)];
        }
        if (ok) {
            result.defined.push_back(c.id);
        }
    }

    if (!result.redundant.empty()) {
        result.status = SketchStatus::OverDefined;
    } else {
        result.status = result.dof == 0 ? SketchStatus::FullyDefined : SketchStatus::UnderDefined;
    }
    return result;
}

} // namespace bl::sketch
