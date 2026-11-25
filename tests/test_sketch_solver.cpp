#include "sketch/Geometry.h"
#include "sketch/Sketch.h"
#include "sketch/Solver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace bl::sketch;

namespace {

constexpr double kTol = 1e-6;

bool contains(const std::vector<Id>& ids, Id id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

// An L-shaped bracket with a fillet in the inside corner and a hole in each flange, drawn a little
// off (as a user would sketch it) and then fully defined:
//
//   (0,40) +--+ (10,40)
//          |  |            L5 on top, L6 down the left, L4 up the inner face
//          |o | C2 (5,30)
//          |  |
//          |  ) arc, centre (15,15), r 5, tangent to L4 and L3
//          |   `-----------+ (60,10)
//          |   o C1 (40,5) |  L2
//   (0,0)  +---------------+ (60,0)    L1 along the bottom from the origin
struct Bracket {
    Sketch s;
    Id l1, l2, l3, l4, l5, l6, arc, c1, c2;

    Point& p(Id id) { return *s.point(id); }
    const Curve& c(Id id) { return *s.curve(id); }

    Bracket() {
        l1 = s.addLine(0.4, -0.3, 58.0, 1.0);
        l2 = s.addLine(58.5, 0.5, 61.0, 11.0);
        l3 = s.addLine(60.5, 11.5, 16.0, 9.0);
        arc = s.addArc(14.0, 16.0, 9.5, 16.5, 16.0, 9.5); // CCW from the L4 end round to the L3 end
        l4 = s.addLine(9.0, 16.0, 11.0, 39.0);
        l5 = s.addLine(11.5, 41.0, 1.0, 39.0);
        l6 = s.addLine(-1.0, 41.0, 0.5, 0.5);
        c1 = s.addCircle(38.0, 6.0, 3.0);
        c2 = s.addCircle(6.0, 28.0, 2.0);

        auto join = [this](Id a, Id b) { s.addRelation(ConstraintType::Coincident, {a, b}); };
        join(s.origin(), c(l1).p1);
        join(c(l1).p2, c(l2).p1);
        join(c(l2).p2, c(l3).p1);
        join(c(l3).p2, c(arc).p2);
        join(c(arc).p1, c(l4).p1);
        join(c(l4).p2, c(l5).p1);
        join(c(l5).p2, c(l6).p1);
        join(c(l6).p2, c(l1).p1);
        for (Id l : {l1, l3, l5}) {
            s.addRelation(ConstraintType::Horizontal, {l});
        }
        for (Id l : {l2, l4, l6}) {
            s.addRelation(ConstraintType::Vertical, {l});
        }
        s.addRelation(ConstraintType::Tangent, {l3, arc});
        s.addRelation(ConstraintType::Tangent, {l4, arc});
        s.addDimension(ConstraintType::Distance, {l1}, 60.0);
        s.addDimension(ConstraintType::Distance, {l2}, 10.0);
        s.addDimension(ConstraintType::Distance, {l5}, 10.0);
        s.addDimension(ConstraintType::Distance, {l6}, 40.0);
        s.addDimension(ConstraintType::Radius, {arc}, 5.0);
        s.addDimension(ConstraintType::Diameter, {c1}, 5.0);
        s.addDimension(ConstraintType::Diameter, {c2}, 5.0);
        s.addDimension(ConstraintType::HorizontalDistance, {s.origin(), c(c1).center}, 40.0);
        s.addDimension(ConstraintType::VerticalDistance, {s.origin(), c(c1).center}, 5.0);
        s.addDimension(ConstraintType::HorizontalDistance, {s.origin(), c(c2).center}, 5.0);
        s.addDimension(ConstraintType::VerticalDistance, {s.origin(), c(c2).center}, 30.0);
    }

    void expectAt(Id point, double x, double y) {
        EXPECT_NEAR(p(point).x, x, kTol) << s.name(point);
        EXPECT_NEAR(p(point).y, y, kTol) << s.name(point);
    }
};

} // namespace

TEST(SketchSolver, BracketBecomesFullyDefined) {
    Bracket b;
    const SolveResult r = solve(b.s);
    ASSERT_TRUE(r.converged);
    EXPECT_EQ(r.dof, 0);
    EXPECT_EQ(r.status, SketchStatus::FullyDefined);
    EXPECT_TRUE(r.redundant.empty());

    b.expectAt(b.c(b.l1).p2, 60, 0);
    b.expectAt(b.c(b.l2).p2, 60, 10);
    b.expectAt(b.c(b.l3).p2, 15, 10);
    b.expectAt(b.c(b.arc).center, 15, 15);
    b.expectAt(b.c(b.arc).p1, 10, 15);
    b.expectAt(b.c(b.l4).p2, 10, 40);
    b.expectAt(b.c(b.l6).p1, 0, 40);
    b.expectAt(b.c(b.c1).center, 40, 5);
    b.expectAt(b.c(b.c2).center, 5, 30);
    EXPECT_NEAR(b.c(b.c1).radius, 2.5, kTol);

    for (const Point& p : b.s.points()) {
        EXPECT_TRUE(r.isDefined(p.id)) << b.s.name(p.id);
    }
    for (const Curve& c : b.s.curves()) {
        EXPECT_TRUE(r.isDefined(c.id)) << b.s.name(c.id);
    }
}

TEST(SketchSolver, MissingDimensionLeavesOneDegreeOfFreedom) {
    Bracket b;
    const Id width = std::find_if(b.s.constraints().begin(), b.s.constraints().end(), [&](const Constraint& c) {
                         return c.type == ConstraintType::Distance && c.refs == std::vector<Id>{b.l1};
                     })->id;
    ASSERT_TRUE(b.s.remove(width));
    const SolveResult r = solve(b.s);
    ASSERT_TRUE(r.converged);
    EXPECT_EQ(r.dof, 1);
    EXPECT_EQ(r.status, SketchStatus::UnderDefined);
    EXPECT_FALSE(r.isDefined(b.l2)); // the right-hand end can slide
    EXPECT_FALSE(r.isDefined(b.l3));
    EXPECT_TRUE(r.isDefined(b.l6));
    EXPECT_TRUE(r.isDefined(b.arc));
    EXPECT_TRUE(r.isDefined(b.c1));
}

TEST(SketchSolver, ConflictingDimensionIsNamedAndGeometryKept) {
    Bracket b;
    ASSERT_TRUE(solve(b.s).converged);
    const Sketch before = b.s;

    const Id extra = b.s.addDimension(ConstraintType::Distance, {b.l3}, 50.0); // it must be 45
    const SolveResult r = solve(b.s);
    EXPECT_FALSE(r.converged);
    EXPECT_EQ(r.status, SketchStatus::OverDefined);
    EXPECT_TRUE(contains(r.conflicting, extra));
    for (const Constraint& c : b.s.constraints()) {
        if (c.type == ConstraintType::Diameter) {
            EXPECT_FALSE(contains(r.conflicting, c.id)) << "the holes have nothing to do with it";
        }
    }
    for (const Point& p : before.points()) {
        EXPECT_DOUBLE_EQ(b.s.point(p.id)->x, p.x);
        EXPECT_DOUBLE_EQ(b.s.point(p.id)->y, p.y);
    }
}

TEST(SketchSolver, RedundantRelationIsReported) {
    Bracket b;
    const Id parallel = b.s.addRelation(ConstraintType::Parallel, {b.l1, b.l3}); // both already horizontal
    const SolveResult r = solve(b.s);
    ASSERT_TRUE(r.converged);
    EXPECT_EQ(r.status, SketchStatus::OverDefined);
    EXPECT_EQ(r.redundant, std::vector<Id>{parallel});
}

TEST(SketchSolver, DimensionChangeMovesGeometryAsLittleAsPossible) {
    Sketch s;
    const Id line = s.addLine(0, 0, 10, 0);
    s.addRelation(ConstraintType::Horizontal, {line});
    const Id d = s.addDimension(ConstraintType::Distance, {line});
    s.constraint(d)->value = 20.0;
    ASSERT_TRUE(solve(s).converged);
    EXPECT_NEAR(s.point(s.curve(line)->p1)->x, -5.0, kTol); // both ends move half the change
    EXPECT_NEAR(s.point(s.curve(line)->p2)->x, 15.0, kTol);
    EXPECT_NEAR(s.point(s.curve(line)->p1)->y, 0.0, kTol);
}

TEST(SketchSolver, DraggedPointFollowsPointerWithinConstraints) {
    Sketch s;
    const Id line = s.addLine(0, 0, 10, 0);
    s.addRelation(ConstraintType::Coincident, {s.origin(), s.curve(line)->p1});
    s.addRelation(ConstraintType::Horizontal, {line});

    SolveOptions drag;
    drag.dragPoint = s.curve(line)->p2;
    drag.dragX = 30.0;
    drag.dragY = 5.0;
    drag.analyse = false;
    ASSERT_TRUE(solve(s, drag).converged);
    EXPECT_NEAR(s.point(drag.dragPoint)->x, 30.0, 1e-4); // free along the line
    EXPECT_NEAR(s.point(drag.dragPoint)->y, 0.0, kTol);  // held by Horizontal
    EXPECT_NEAR(s.point(s.curve(line)->p1)->x, 0.0, kTol);

    const SolveResult r = solve(s);
    EXPECT_EQ(r.dof, 1);
    EXPECT_TRUE(r.isDefined(s.curve(line)->p1));
    EXPECT_FALSE(r.isDefined(s.curve(line)->p2));
}

TEST(SketchSolver, FreeGeometryCountsItsDegreesOfFreedom) {
    Sketch s;
    s.addLine(0, 0, 10, 5);
    s.addCircle(20, 0, 3);
    s.addArc(0, 20, 5, 20, 0, 25);
    const SolveResult r = solve(s);
    ASSERT_TRUE(r.converged);
    EXPECT_EQ(r.dof, 4 + 3 + 5);
    EXPECT_EQ(r.status, SketchStatus::UnderDefined);
    EXPECT_EQ(r.defined, std::vector<Id>{s.origin()});
}

TEST(SketchSolver, CircleTangencyKeepsItsSide) {
    Sketch s;
    const Id a = s.addCircle(0, 0, 10);
    const Id b = s.addCircle(14, 1, 3);   // nearer outside than inside tangency
    const Id c = s.addCircle(4, -2, 4);   // inside
    s.addRelation(ConstraintType::Tangent, {a, b});
    s.addRelation(ConstraintType::Tangent, {a, c});
    ASSERT_TRUE(solve(s).converged);
    auto gap = [&s](Id x, Id y) {
        const Point* p = s.point(s.curve(x)->center);
        const Point* q = s.point(s.curve(y)->center);
        return std::hypot(q->x - p->x, q->y - p->y);
    };
    EXPECT_NEAR(gap(a, b), s.curve(a)->radius + s.curve(b)->radius, kTol);
    EXPECT_NEAR(gap(a, c), s.curve(a)->radius - s.curve(c)->radius, kTol);
}

TEST(SketchSolver, AngleSymmetryAndMidpoint) {
    Sketch s;
    const Id base = s.addLine(0, 0, 20, 0);
    const Id slope = s.addLine(0, 0, 15, 10);
    s.addRelation(ConstraintType::Coincident, {s.origin(), s.curve(base)->p1});
    s.addRelation(ConstraintType::Coincident, {s.origin(), s.curve(slope)->p1});
    s.addRelation(ConstraintType::Horizontal, {base});
    s.addDimension(ConstraintType::Angle, {base, slope}, std::numbers::pi / 6);
    s.addDimension(ConstraintType::Distance, {slope}, 20.0);

    const Id mid = s.addPoint(9, 1);
    s.addRelation(ConstraintType::Midpoint, {mid, base});
    const Id axis = s.addLine(5, -5, 5, 5);
    s.addRelation(ConstraintType::Vertical, {axis});
    const Id left = s.addPoint(2, 3);
    const Id right = s.addPoint(9, 2);
    s.addRelation(ConstraintType::Symmetric, {left, right, axis});

    ASSERT_TRUE(solve(s).converged);
    const Point* tip = s.point(s.curve(slope)->p2);
    EXPECT_NEAR(tip->x, 20.0 * std::cos(std::numbers::pi / 6), kTol);
    EXPECT_NEAR(tip->y, 10.0, kTol);
    EXPECT_NEAR(s.point(mid)->x, 0.5 * s.point(s.curve(base)->p2)->x, kTol);
    EXPECT_NEAR(s.point(mid)->y, 0.0, kTol);
    const double axisX = s.point(s.curve(axis)->p1)->x;
    EXPECT_NEAR(s.point(left)->x + s.point(right)->x, 2.0 * axisX, kTol);
    EXPECT_NEAR(s.point(left)->y, s.point(right)->y, kTol);
}

TEST(SketchSolver, FixHoldsGeometryWhereItWas) {
    Sketch s;
    const Id line = s.addLine(3, 4, 10, 4);
    s.addRelation(ConstraintType::Fix, {line});
    const SolveResult r = solve(s);
    ASSERT_TRUE(r.converged);
    EXPECT_EQ(r.dof, 0);
    EXPECT_EQ(r.status, SketchStatus::FullyDefined);
}

TEST(Sketch, RemovingACurveRemovesItsRelations) {
    Sketch s;
    const Id a = s.addLine(0, 0, 10, 0);
    const Id b = s.addLine(10, 0, 10, 10);
    s.addRelation(ConstraintType::Coincident, {s.curve(a)->p2, s.curve(b)->p1});
    s.addRelation(ConstraintType::Perpendicular, {a, b});
    s.addDimension(ConstraintType::Distance, {a});
    ASSERT_EQ(s.constraints().size(), 3u);
    EXPECT_TRUE(s.remove(a));
    EXPECT_TRUE(s.constraints().empty());
    EXPECT_EQ(s.curves().size(), 1u);
    EXPECT_EQ(s.points().size(), 3u); // origin + b's two points
    EXPECT_FALSE(s.remove(s.origin()));
    EXPECT_EQ(s.name(b), "Line2"); // names stay put when an earlier entity goes
    EXPECT_EQ(s.name(s.curve(b)->p1), "Point3");
    EXPECT_EQ(s.name(s.addDimension(ConstraintType::Distance, {b})), "D2");
    EXPECT_EQ(s.name(s.addRelation(ConstraintType::Vertical, {b})), "Vertical1");
    EXPECT_EQ(s.name(s.origin()), "Origin");
}

TEST(Sketch, ArcEndIsPutOnItsCircle) {
    Sketch s;
    const Id arc = s.addArc(0, 0, 10, 0, 0, 7);
    const Point* e = s.point(s.curve(arc)->p2);
    EXPECT_NEAR(e->x, 0.0, kTol);
    EXPECT_NEAR(e->y, 10.0, kTol);
    EXPECT_NEAR(s.arcRadius(*s.curve(arc)), 10.0, kTol);
}

// --- Geometry helpers used by the sketch tools ---------------------------------------------------------

TEST(SketchGeometry, TangentArcTurnsTowardsTheEnd) {
    // Leaving (15,10) heading -x and ending at (10,15): the bracket fillet, clockwise round (15,15).
    const auto fillet = tangentArc({15, 10}, {-1, 0}, {10, 15});
    ASSERT_TRUE(fillet);
    EXPECT_NEAR(fillet->center.x, 15.0, kTol);
    EXPECT_NEAR(fillet->center.y, 15.0, kTol);
    EXPECT_FALSE(fillet->ccw);
    const auto left = tangentArc({0, 0}, {1, 0}, {0, 10}); // half circle up to the left
    ASSERT_TRUE(left);
    EXPECT_NEAR(left->center.x, 0.0, kTol);
    EXPECT_NEAR(left->center.y, 5.0, kTol);
    EXPECT_TRUE(left->ccw);
    EXPECT_FALSE(tangentArc({0, 0}, {1, 0}, {10, 0})); // straight ahead: no arc
}

TEST(SketchGeometry, ExitDirection) {
    Sketch s;
    const Curve line = *s.curve(s.addLine(0, 0, 10, 0));
    const auto atEnd = exitDirection(s, line, line.p2);
    const auto atStart = exitDirection(s, line, line.p1);
    ASSERT_TRUE(atEnd && atStart);
    EXPECT_NEAR(atEnd->x, 1.0, kTol);
    EXPECT_NEAR(atStart->x, -1.0, kTol);
    const Curve arc = *s.curve(s.addArc(0, 0, 5, 0, 0, 5)); // quarter, counter-clockwise
    const auto onwards = exitDirection(s, arc, arc.p2);
    const auto back = exitDirection(s, arc, arc.p1);
    ASSERT_TRUE(onwards && back);
    EXPECT_NEAR(onwards->x, -1.0, kTol); // at (0,5) going on counter-clockwise
    EXPECT_NEAR(back->y, -1.0, kTol);    // at (5,0) going back against it
    EXPECT_FALSE(exitDirection(s, arc, arc.center));
}

TEST(SketchGeometry, RectanglesKeepTheirShape) {
    Sketch s;
    addRectangle(s, {0, 0}, {30, 20});
    SolveResult r = solve(s);
    EXPECT_EQ(r.status, SketchStatus::UnderDefined);
    EXPECT_EQ(r.dof, 4); // two corners
    EXPECT_TRUE(r.redundant.empty());

    Sketch c;
    const auto ids = addCenterRectangle(c, {0, 0}, {15, 10});
    r = solve(c);
    EXPECT_EQ(r.dof, 4); // centre, width, height
    EXPECT_TRUE(r.redundant.empty());
    EXPECT_TRUE(c.curve(ids[4])->construction);
    EXPECT_TRUE(c.curve(ids[5])->construction);
    // Drag a corner: the centre stays in the middle and the sides stay square.
    SolveOptions o;
    o.dragPoint = c.curve(ids[2])->p1;
    o.dragX = 25;
    o.dragY = 16;
    ASSERT_TRUE(solve(c, o).converged);
    const Point* center = c.point(ids[6]);
    const Curve* bottom = c.curve(ids[0]);
    const Curve* top = c.curve(ids[2]);
    const Point* t = c.point(top->p1);
    const Point* b = c.point(bottom->p1);
    EXPECT_NEAR(c.point(bottom->p1)->y, c.point(bottom->p2)->y, kTol);
    EXPECT_NEAR(center->x, (t->x + c.point(top->p2)->x) / 2.0, 1e-4);
    EXPECT_NEAR(center->y, (t->y + b->y) / 2.0, 1e-4);
}

TEST(SketchGeometry, LineHelpers) {
    Sketch s;
    const Curve a = *s.curve(s.addLine(0, 0, 10, 0));
    const Curve b = *s.curve(s.addLine(0, 5, 20, 5));
    const Curve c = *s.curve(s.addLine(3, -4, 3, 9));
    EXPECT_TRUE(parallel(s, a, b));
    EXPECT_FALSE(parallel(s, a, c));
    EXPECT_FALSE(lineIntersection(s, a, b));
    const auto x = lineIntersection(s, a, c);
    ASSERT_TRUE(x);
    EXPECT_NEAR(x->x, 3.0, kTol);
    EXPECT_NEAR(x->y, 0.0, kTol);
}
