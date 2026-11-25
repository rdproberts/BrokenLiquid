#include "kernel/Measure.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace bl::kernel;

namespace {

// Face of a box whose outward normal is `n` (box from origin, size 10 x 20 x 30).
TopoDS_Shape boxFace(const TopoDS_Shape& box, const gp_Dir& n) {
    for (TopExp_Explorer it(box, TopAbs_FACE); it.More(); it.Next()) {
        const EntityInfo info = describe(it.Current());
        if (info.axis && info.axis->Direction().IsEqual(n, 1e-9)) {
            return it.Current();
        }
    }
    return {};
}

} // namespace

TEST(Measure, DescribesBoxFacesAndSolid) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    const EntityInfo solid = describe(box);
    EXPECT_EQ(solid.kind, EntityKind::Solid);
    EXPECT_NEAR(solid.volume, 6000.0, 1e-6);
    EXPECT_NEAR(solid.area, 2 * (200 + 300 + 600), 1e-6);

    const TopoDS_Shape top = boxFace(box, gp_Dir(0, 0, 1));
    ASSERT_FALSE(top.IsNull());
    const EntityInfo face = describe(top);
    EXPECT_EQ(face.kind, EntityKind::Face);
    EXPECT_EQ(face.geometry, "Plane");
    EXPECT_NEAR(face.area, 200.0, 1e-6);
    EXPECT_NEAR(face.perimeter, 60.0, 1e-6);
}

TEST(Measure, DistanceBetweenParallelFaces) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    const TopoDS_Shape top = boxFace(box, gp_Dir(0, 0, 1));
    const TopoDS_Shape bottom = boxFace(box, gp_Dir(0, 0, -1));
    ASSERT_FALSE(top.IsNull());
    ASSERT_FALSE(bottom.IsNull());
    const PairMeasure m = measurePair(top, bottom);
    ASSERT_TRUE(m.valid);
    EXPECT_NEAR(m.distance, 30.0, 1e-9);
    EXPECT_NEAR(std::abs(m.delta.Z()), 30.0, 1e-9);
    EXPECT_TRUE(m.parallel);
    ASSERT_TRUE(m.angle);
    EXPECT_NEAR(*m.angle, 0.0, 1e-9);
}

TEST(Measure, AngleBetweenAdjacentFaces) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    const PairMeasure m = measurePair(boxFace(box, gp_Dir(0, 0, 1)), boxFace(box, gp_Dir(1, 0, 0)));
    ASSERT_TRUE(m.valid);
    EXPECT_NEAR(m.distance, 0.0, 1e-9); // they touch
    ASSERT_TRUE(m.angle);
    EXPECT_NEAR(*m.angle, std::numbers::pi / 2, 1e-9);
    EXPECT_FALSE(m.parallel);
}

TEST(Measure, CircleRadiusAndCentreDistance) {
    const gp_Circ c1(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 5.0);
    const gp_Circ c2(gp_Ax2(gp_Pnt(30, 40, 0), gp_Dir(0, 0, 1)), 2.0);
    const TopoDS_Shape e1 = BRepBuilderAPI_MakeEdge(c1).Edge();
    const TopoDS_Shape e2 = BRepBuilderAPI_MakeEdge(c2).Edge();

    const EntityInfo info = describe(e1);
    EXPECT_EQ(info.kind, EntityKind::Edge);
    ASSERT_TRUE(info.radius);
    EXPECT_NEAR(*info.radius, 5.0, 1e-9);
    EXPECT_NEAR(info.length, 2 * std::numbers::pi * 5.0, 1e-6);

    const PairMeasure m = measurePair(e1, e2);
    ASSERT_TRUE(m.valid);
    ASSERT_TRUE(m.centerDistance);
    EXPECT_NEAR(*m.centerDistance, 50.0, 1e-9);
    EXPECT_NEAR(m.distance, 50.0 - 5.0 - 2.0, 1e-6);
}

TEST(Measure, CylinderFaceRadius) {
    const TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(4.0, 10.0).Shape();
    bool found = false;
    for (TopExp_Explorer it(cyl, TopAbs_FACE); it.More(); it.Next()) {
        const EntityInfo info = describe(it.Current());
        if (info.geometry == "Cylinder") {
            found = true;
            ASSERT_TRUE(info.radius);
            EXPECT_NEAR(*info.radius, 4.0, 1e-9);
            EXPECT_NEAR(info.area, 2 * std::numbers::pi * 4.0 * 10.0, 1e-6);
        }
    }
    EXPECT_TRUE(found);
}

TEST(Measure, TotalsSumEdges) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(box, TopAbs_EDGE, edges);
    std::vector<TopoDS_Shape> picks;
    for (int i = 1; i <= edges.Extent(); ++i) {
        picks.push_back(edges(i));
    }
    const Totals t = totals(picks);
    EXPECT_EQ(t.edges, 12);
    EXPECT_NEAR(t.length, 4 * (10 + 20 + 30), 1e-6);
}
