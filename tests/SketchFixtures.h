#pragma once

// Shared sketch test fixtures.

#include "sketch/Sketch.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace bl::sketch::test {

inline constexpr double kTol = 1e-6;

inline bool contains(const std::vector<Id>& ids, Id id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

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


} // namespace bl::sketch::test
