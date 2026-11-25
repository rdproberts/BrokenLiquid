#include "SketchFixtures.h"
#include "sketch/Operations.h"
#include "sketch/Solver.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace bl::sketch;
using namespace bl::sketch::test;

namespace {

constexpr double kPi = std::numbers::pi;

bool hasConstraint(const Sketch& s, ConstraintType type, std::vector<Id> refs) {
    std::sort(refs.begin(), refs.end());
    return std::any_of(s.constraints().begin(), s.constraints().end(), [&](const Constraint& c) {
        std::vector<Id> r = c.refs;
        std::sort(r.begin(), r.end());
        return c.type == type && r == refs;
    });
}

void expectNear(P2 p, double x, double y) {
    EXPECT_NEAR(p.x, x, kTol);
    EXPECT_NEAR(p.y, y, kTol);
}

} // namespace

TEST(SketchOperations, Intersections) {
    Sketch s;
    const Curve a = *s.curve(s.addLine(-10, 0, 10, 0));
    const Curve b = *s.curve(s.addLine(0, -10, 0, 10));
    const Curve far = *s.curve(s.addLine(20, -10, 20, 10));
    const Curve circle = *s.curve(s.addCircle(0, 0, 5));
    const Curve arc = *s.curve(s.addArc(0, 0, 5, 0, -5, 0)); // upper half
    ASSERT_EQ(intersections(s, a, b).size(), 1u);
    expectNear(intersections(s, a, b)[0], 0, 0);
    EXPECT_TRUE(intersections(s, a, far).empty());
    EXPECT_EQ(intersections(s, a, far, true).size(), 1u); // a extended reaches it
    EXPECT_EQ(intersections(s, b, circle).size(), 2u);
    const auto up = intersections(s, b, arc);
    ASSERT_EQ(up.size(), 1u); // only the upper half is there
    expectNear(up[0], 0, 5);
    const Curve other = *s.curve(s.addCircle(8, 0, 5));
    EXPECT_EQ(intersections(s, circle, other).size(), 2u);
}

TEST(SketchOperations, TrimShortensSplitsAndDeletes) {
    Sketch s;
    const Id h = s.addLine(0, 0, 30, 0);
    const Id v1 = s.addLine(10, -5, 10, 5);
    const Id v2 = s.addLine(20, -5, 20, 5);
    s.addDimension(ConstraintType::Distance, {h}, 30.0);

    // Picking the middle piece cuts it out: two collinear lines remain.
    ASSERT_TRUE(trim(s, h, {15, 0}));
    ASSERT_EQ(s.curves().size(), 4u);
    const Curve left = *s.curve(h);
    expectNear(pointOf(s, left.p1), 0, 0);
    expectNear(pointOf(s, left.p2), 10, 0);
    const Curve right = s.curves().back();
    expectNear(pointOf(s, right.p1), 20, 0);
    expectNear(pointOf(s, right.p2), 30, 0);
    EXPECT_TRUE(hasConstraint(s, ConstraintType::Collinear, {h, right.id}));
    EXPECT_TRUE(hasConstraint(s, ConstraintType::PointOnCurve, {left.p2, v1}));
    EXPECT_TRUE(hasConstraint(s, ConstraintType::PointOnCurve, {right.p1, v2}));
    EXPECT_FALSE(hasConstraint(s, ConstraintType::Distance, {h})); // its length no longer applies

    // Picking an end piece shortens the line.
    ASSERT_TRUE(trim(s, v1, {10, 4}));
    expectNear(pointOf(s, s.curve(v1)->p2), 10, 0);
    // A line crossing nothing goes completely.
    const Id lone = s.addLine(0, 50, 10, 50);
    ASSERT_TRUE(trim(s, lone, {5, 50}));
    EXPECT_EQ(s.curve(lone), nullptr);
    EXPECT_TRUE(solve(s).converged);
}

TEST(SketchOperations, TrimTurnsACircleIntoAnArc) {
    Sketch s;
    const Id circle = s.addCircle(0, 0, 10);
    s.addLine(-20, 0, 20, 0);
    ASSERT_TRUE(trim(s, circle, {0, 10})); // remove the upper half
    const Curve& k = *s.curve(circle);
    ASSERT_EQ(k.type, CurveType::Arc);
    expectNear(midpointOf(s, k), 0, -10);
    EXPECT_EQ(s.name(circle), "Arc1");
}

TEST(SketchOperations, ExtendReachesTheNextCurve) {
    Sketch s;
    const Id line = s.addLine(0, 0, 10, 0);
    s.addLine(25, -5, 25, 5);
    s.addCircle(-20, 0, 4);
    ASSERT_TRUE(extend(s, line, {9, 0}));
    expectNear(pointOf(s, s.curve(line)->p2), 25, 0);
    ASSERT_TRUE(extend(s, line, {1, 0})); // the other end, to the near side of the circle
    expectNear(pointOf(s, s.curve(line)->p1), -16, 0);
    EXPECT_FALSE(extend(s, line, {24, 0})); // nothing further on
}

TEST(SketchOperations, FilletKeepsTheBracketFullyDefined) {
    Bracket b;
    ASSERT_TRUE(solve(b.s).converged);
    const auto arc = filletCorner(b.s, b.c(b.l1).p2, 3.0); // the outer corner at (60, 0)
    ASSERT_TRUE(arc);
    const SolveResult r = solve(b.s);
    ASSERT_TRUE(r.converged);
    EXPECT_EQ(r.status, SketchStatus::FullyDefined);
    EXPECT_TRUE(r.redundant.empty());
    const Curve& k = *b.s.curve(*arc);
    EXPECT_NEAR(radiusOf(b.s, k), 3.0, kTol);
    expectNear(centerOf(b.s, k), 57, 3);
    expectNear(pointOf(b.s, b.c(b.l1).p2), 57, 0);
    expectNear(pointOf(b.s, b.c(b.l2).p1), 60, 3);
    // Changing the overall width still moves the rounded corner with it.
    for (Constraint& c : b.s.constraints()) {
        if (c.type == ConstraintType::Distance && c.value == 60.0) {
            c.value = 70.0;
        }
    }
    ASSERT_TRUE(solve(b.s).converged);
    expectNear(centerOf(b.s, *b.s.curve(*arc)), 67, 3);
    EXPECT_FALSE(filletCorner(b.s, b.c(b.l5).p2, 50.0)); // does not fit
}

TEST(SketchOperations, SlotAndPolygonHaveTheExpectedFreedom) {
    Sketch s;
    const auto slot = addSlot(s, {0, 0}, {30, 0}, 5.0);
    SolveResult r = solve(s);
    EXPECT_TRUE(r.converged);
    EXPECT_TRUE(r.redundant.empty());
    EXPECT_EQ(r.dof, 5); // two centres and the width
    EXPECT_NEAR(radiusOf(s, *s.curve(slot[0])), 5.0, kTol);

    Sketch p;
    const auto ids = addPolygon(p, {0, 0}, {10, 0}, 6);
    ASSERT_EQ(ids.size(), 7u);
    r = solve(p);
    EXPECT_TRUE(r.redundant.empty());
    EXPECT_EQ(r.dof, 4); // centre, size, rotation
    EXPECT_NEAR(length(pointOf(p, p.curve(ids[0])->p2) - pointOf(p, p.curve(ids[0])->p1)), 10.0, kTol);
    EXPECT_TRUE(p.curve(ids[6])->construction);
}

TEST(SketchOperations, OffsetChainsAndDimensions) {
    Sketch s;
    addRectangle(s, {0, 0}, {30, 20});
    std::vector<Id> lines;
    for (const Curve& c : s.curves()) {
        lines.push_back(c.id);
    }
    const SolveResult before = solve(s);
    const std::vector<Id> out = offset(s, lines, 2.0);
    ASSERT_EQ(out.size(), 4u);
    double minX = 1e9, maxX = -1e9;
    for (Id id : out) {
        for (Id p : {s.curve(id)->p1, s.curve(id)->p2}) {
            minX = std::min(minX, pointOf(s, p).x);
            maxX = std::max(maxX, pointOf(s, p).x);
        }
    }
    EXPECT_NEAR(minX, -2.0, kTol); // outwards
    EXPECT_NEAR(maxX, 32.0, kTol);
    const SolveResult after = solve(s);
    EXPECT_TRUE(after.redundant.empty());
    EXPECT_EQ(after.dof, before.dof); // the copy follows the original

    // The fully defined bracket offset inwards stays fully defined.
    Bracket b;
    std::vector<Id> outline{b.l1, b.l2, b.l3, b.arc, b.l4, b.l5, b.l6};
    ASSERT_TRUE(solve(b.s).converged);
    const std::vector<Id> inner = offset(b.s, outline, -1.0);
    ASSERT_EQ(inner.size(), 7u);
    const SolveResult r = solve(b.s);
    EXPECT_TRUE(r.converged);
    EXPECT_TRUE(r.redundant.empty());
    EXPECT_EQ(r.status, SketchStatus::FullyDefined);
    const Contours regions = findRegions(b.s);
    ASSERT_GE(regions.regions.size(), 2u);
    EXPECT_TRUE(offset(b.s, outline, -20.0).empty()); // collapses
}

TEST(SketchOperations, MirrorHoldsCopiesSymmetric) {
    Sketch s;
    const Id axis = s.addLine(0, -20, 0, 20);
    s.curve(axis)->construction = true;
    s.addRelation(ConstraintType::Vertical, {axis});
    s.addRelation(ConstraintType::Coincident, {s.origin(), s.curve(axis)->p1});
    const Id line = s.addLine(0, 10, 10, 15);
    const Id arc = s.addArc(10, 0, 15, 0, 10, 5);
    const Id circle = s.addCircle(5, -10, 2);
    const SolveResult before = solve(s);
    const std::vector<Id> copies = mirror(s, {line, arc, circle}, axis);
    ASSERT_EQ(copies.size(), 3u);
    expectNear(pointOf(s, s.curve(copies[0])->p2), -10, 15);
    expectNear(centerOf(s, *s.curve(copies[1])), -10, 0);
    expectNear(centerOf(s, *s.curve(copies[2])), -5, -10);
    const SolveResult after = solve(s);
    EXPECT_TRUE(after.redundant.empty());
    EXPECT_EQ(after.dof, before.dof);
    // Moving an original drags its copy along.
    SolveOptions o;
    o.dragPoint = s.curve(arc)->center;
    o.dragX = 12;
    o.dragY = 3;
    ASSERT_TRUE(solve(s, o).converged);
    const P2 moved = centerOf(s, *s.curve(arc));
    EXPECT_NEAR(moved.x, 12, 1e-3);
    expectNear(centerOf(s, *s.curve(copies[1])), -moved.x, moved.y);
    EXPECT_NEAR(radiusOf(s, *s.curve(copies[1])), radiusOf(s, *s.curve(arc)), kTol);
}

TEST(SketchOperations, RegionsWithHolesAndOverlaps) {
    Sketch s;
    addRectangle(s, {0, 0}, {30, 20});
    s.addCircle(10, 10, 3);
    Contours c = findRegions(s);
    ASSERT_EQ(c.regions.size(), 2u);
    EXPECT_NEAR(c.regions[0].area, 600.0 - kPi * 9.0, 0.5); // plate with a hole
    EXPECT_EQ(c.regions[0].holes.size(), 1u);
    EXPECT_NEAR(c.regions[1].area, kPi * 9.0, 0.1); // the disc
    EXPECT_TRUE(regionContains(c.regions[0], {20, 10}));
    EXPECT_FALSE(regionContains(c.regions[0], {10, 10}));
    EXPECT_TRUE(c.openEnds.empty());

    Sketch o; // two overlapping circles: three regions
    o.addCircle(0, 0, 5);
    o.addCircle(6, 0, 5);
    c = findRegions(o);
    EXPECT_EQ(c.regions.size(), 3u);

    Sketch open;
    open.addLine(0, 0, 10, 0);
    open.addLine(10, 0, 10, 10);
    c = findRegions(open);
    EXPECT_TRUE(c.regions.empty());
    EXPECT_EQ(c.openEnds.size(), 2u);

    Bracket b;
    ASSERT_TRUE(solve(b.s).converged);
    c = findRegions(b.s);
    ASSERT_EQ(c.regions.size(), 3u); // the bracket with two holes, and the two discs
    EXPECT_EQ(c.regions[0].holes.size(), 2u);
    const double bracketArea = 60 * 10 + 10 * 30 + (25 - 25 * kPi / 4) - 2 * kPi * 6.25;
    EXPECT_NEAR(c.regions[0].area, bracketArea, 0.5);
}

TEST(SketchOperations, SketchDoctorFindsAndRepairs) {
    Sketch s;
    const Id a = s.addLine(0, 0, 10, 0);
    const Id b = s.addLine(10.05, 0.02, 10, 10); // almost meets a
    const Id dup = s.addLine(0, 0, 10, 0);       // a drawn twice
    s.addLine(5, 5, 5, 5);                       // nothing
    std::vector<Issue> issues = diagnose(s, 0.2);
    auto count = [&](IssueKind k) { return std::count_if(issues.begin(), issues.end(), [k](const Issue& i) { return i.kind == k; }); };
    EXPECT_EQ(count(IssueKind::Gap), 1);
    EXPECT_EQ(count(IssueKind::Duplicate), 1);
    EXPECT_EQ(count(IssueKind::ZeroLength), 1);
    EXPECT_GE(count(IssueKind::OpenEnd), 2);
    for (const Issue& i : issues) {
        if (i.repairable) {
            EXPECT_TRUE(repair(s, i)) << issueName(i.kind);
        }
    }
    EXPECT_EQ(s.curve(dup), nullptr);
    ASSERT_TRUE(solve(s).converged);
    expectNear(pointOf(s, s.curve(a)->p2), pointOf(s, s.curve(b)->p1).x, pointOf(s, s.curve(b)->p1).y);
    issues = diagnose(s, 0.2);
    EXPECT_EQ(count(IssueKind::Gap) + count(IssueKind::Duplicate) + count(IssueKind::ZeroLength), 0);
}
