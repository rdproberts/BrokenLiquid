#include "kernel/ShapeInfo.h"
#include "kernel/StepIO.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// 40 x 20 x 10 block with a 6 mm through hole.
TopoDS_Shape blockWithHole() {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(40.0, 20.0, 10.0).Shape();
    const TopoDS_Shape pin = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20, 10, -1), gp::DZ()), 3.0, 12.0).Shape();
    BRepAlgoAPI_Cut cut(box, pin);
    cut.Build();
    return cut.Shape();
}

} // namespace

TEST(ShapeInfo, BoxStats) {
    const auto s = bl::kernel::computeStats(BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape());
    EXPECT_EQ(s.solids, 1);
    EXPECT_EQ(s.faces, 6);
    EXPECT_EQ(s.edges, 12);
    EXPECT_EQ(s.vertices, 8);
    EXPECT_NEAR(s.volume, 6000.0, 1e-6);
    EXPECT_NEAR(s.area, 2 * (200.0 + 600.0 + 300.0), 1e-6);
    EXPECT_TRUE(s.valid);
}

TEST(ShapeInfo, BooleanResult) {
    const auto s = bl::kernel::computeStats(blockWithHole());
    EXPECT_EQ(s.solids, 1);
    EXPECT_EQ(s.faces, 7); // 6 planar + 1 cylindrical
    EXPECT_NEAR(s.volume, 40.0 * 20.0 * 10.0 - M_PI * 9.0 * 10.0, 1e-3);
    EXPECT_TRUE(s.valid);
}

TEST(StepIO, ExportImportRoundTrip) {
    const fs::path dir = fs::temp_directory_path() / "bl_tests";
    fs::create_directories(dir);
    const fs::path file = dir / "roundtrip_block.step";
    fs::remove(file);

    const TopoDS_Shape part = blockWithHole();
    std::string error;
    ASSERT_TRUE(bl::kernel::exportStep(part, file, &error)) << error;
    ASSERT_TRUE(fs::exists(file));

    const auto result = bl::kernel::importStep(file);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.roots.size(), 1u);
    const auto before = bl::kernel::computeStats(part);
    const auto after = bl::kernel::computeStats(result.roots.front().shape);
    EXPECT_EQ(after.solids, 1);
    EXPECT_EQ(after.faces, before.faces);
    EXPECT_NEAR(after.volume, before.volume, before.volume * 1e-6);
    EXPECT_TRUE(after.valid);
}

TEST(StepIO, MissingFileFails) {
    const auto result = bl::kernel::importStep("Z:/definitely/not/here.step");
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}
