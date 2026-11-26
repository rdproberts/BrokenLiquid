#include "kernel/Export.h"
#include "kernel/ShapeInfo.h"
#include "kernel/StepIO.h"

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Quantity_Color.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <gp_Ax2.hxx>

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace bl::kernel;

namespace {

fs::path testDir() {
    const fs::path dir = fs::temp_directory_path() / "bl_tests" / "export";
    fs::create_directories(dir);
    return dir;
}

std::vector<char> readAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint32_t u32(const std::vector<char>& bytes, std::size_t offset) {
    std::uint32_t v = 0;
    std::memcpy(&v, bytes.data() + offset, 4);
    return v;
}

// A 40 x 20 x 10 block (red) and a pin of radius 3, height 12 (blue) beside it.
std::vector<ExportBody> sampleBodies(bool copies = false) {
    ExportBody block{"Block", BRepPrimAPI_MakeBox(40.0, 20.0, 10.0).Shape(), {0.8, 0.1, 0.1}};
    ExportBody pin{"Pin", BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(60, 10, 0), gp::DZ()), 3.0, 12.0).Shape(), {0.1, 0.2, 0.9}};
    if (copies) {
        block.shape = detachedCopy(block.shape);
        pin.shape = detachedCopy(pin.shape);
    }
    return {block, pin};
}

} // namespace

TEST(Export, FormatFromExtension) {
    EXPECT_EQ(exportFormatFor("a/part.STEP"), ExportFormat::Step);
    EXPECT_EQ(exportFormatFor("part.stp"), ExportFormat::Step);
    EXPECT_EQ(exportFormatFor("part.Stl"), ExportFormat::Stl);
    EXPECT_EQ(exportFormatFor("part.obj"), ExportFormat::Obj);
    EXPECT_EQ(exportFormatFor("part.glb"), ExportFormat::Glb);
    EXPECT_FALSE(exportFormatFor("part.gltf").has_value());
    EXPECT_FALSE(exportFormatFor("part").has_value());
    EXPECT_STREQ(exportExtension(ExportFormat::Glb), "glb");
    EXPECT_FALSE(isMeshFormat(ExportFormat::Step));
    EXPECT_TRUE(isMeshFormat(ExportFormat::Stl));
}

TEST(Export, NothingToExportFails) {
    const ExportResult r = exportBodies({}, testDir() / "empty.step", ExportFormat::Step);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(Export, StepKeepsBodiesNamesAndColours) {
    const fs::path file = testDir() / "bodies.step";
    fs::remove(file);
    const ExportResult r = exportBodies(sampleBodies(), file, ExportFormat::Step);
    ASSERT_TRUE(r.ok) << r.error;

    const ImportResult back = importStep(file);
    ASSERT_TRUE(back.ok) << back.error;
    ASSERT_EQ(back.roots.size(), 2u);
    EXPECT_EQ(back.roots[0].name, "Block");
    EXPECT_EQ(back.roots[1].name, "Pin");
    EXPECT_NEAR(computeStats(back.roots[0].shape).volume, 8000.0, 1e-3);

    Quantity_Color color;
    ASSERT_TRUE(XCAFDoc_ColorTool::GetColor(back.roots[0].label, XCAFDoc_ColorSurf, color) ||
                XCAFDoc_ColorTool::GetColor(back.roots[0].label, XCAFDoc_ColorGen, color));
    double r_ = 0, g = 0, b = 0;
    color.Values(r_, g, b, Quantity_TOC_sRGB);
    EXPECT_NEAR(r_, 0.8, 0.01);
    EXPECT_NEAR(g, 0.1, 0.01);
    EXPECT_NEAR(b, 0.1, 0.01);
}

TEST(Export, BinaryStlHasEveryTriangle) {
    const fs::path file = testDir() / "box.stl";
    std::vector<ExportBody> box{{"Box", detachedCopy(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape()), {}}};
    const ExportResult r = exportBodies(box, file, ExportFormat::Stl);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.triangles, 12u); // 6 faces x 2
    const std::vector<char> bytes = readAll(file);
    ASSERT_EQ(bytes.size(), 84u + 50u * 12u); // 80-byte header, count, 50 bytes a triangle
    EXPECT_EQ(u32(bytes, 80), 12u);
}

TEST(Export, AsciiStl) {
    const fs::path file = testDir() / "box_ascii.stl";
    std::vector<ExportBody> box{{"Box", detachedCopy(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape()), {}}};
    ExportOptions options;
    options.stlAscii = true;
    ASSERT_TRUE(exportBodies(box, file, ExportFormat::Stl, options).ok);
    const std::vector<char> bytes = readAll(file);
    EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 5), "solid");
}

TEST(Export, MeshQualityChangesTriangleCount) {
    const TopoDS_Shape pin = BRepPrimAPI_MakeCylinder(10.0, 20.0).Shape();
    ExportOptions coarse, fine;
    coarse.quality = MeshQuality::Coarse;
    fine.quality = MeshQuality::Fine;
    const ExportResult a = exportBodies({{"Pin", detachedCopy(pin), {}}}, testDir() / "coarse.stl", ExportFormat::Stl, coarse);
    const ExportResult b = exportBodies({{"Pin", detachedCopy(pin), {}}}, testDir() / "fine.stl", ExportFormat::Stl, fine);
    ASSERT_TRUE(a.ok && b.ok);
    EXPECT_GT(b.triangles, a.triangles * 2);
    EXPECT_LT(meshTolerance(pin, MeshQuality::Fine).linear, meshTolerance(pin, MeshQuality::Coarse).linear);
}

TEST(Export, ObjWritesMaterials) {
    const fs::path file = testDir() / "bodies.obj";
    fs::remove(file);
    fs::remove(testDir() / "bodies.mtl");
    const ExportResult r = exportBodies(sampleBodies(true), file, ExportFormat::Obj);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_GT(r.triangles, 12u);
    const std::vector<char> bytes = readAll(file);
    const std::string text(bytes.begin(), bytes.end());
    EXPECT_NE(text.find("\nv "), std::string::npos);
    EXPECT_NE(text.find("\nf "), std::string::npos);
    EXPECT_NE(text.find("mtllib"), std::string::npos);
    EXPECT_TRUE(fs::exists(testDir() / "bodies.mtl"));
}

TEST(Export, GlbIsValidGltf) {
    const fs::path file = testDir() / "bodies.glb";
    const ExportResult r = exportBodies(sampleBodies(true), file, ExportFormat::Glb);
    ASSERT_TRUE(r.ok) << r.error;

    const std::vector<char> bytes = readAll(file);
    ASSERT_GE(bytes.size(), 28u);
    EXPECT_EQ(u32(bytes, 0), 0x46546C67u); // "glTF"
    EXPECT_EQ(u32(bytes, 4), 2u);
    EXPECT_EQ(u32(bytes, 8), bytes.size());
    const std::uint32_t jsonLength = u32(bytes, 12);
    EXPECT_EQ(u32(bytes, 16), 0x4E4F534Au); // "JSON"
    EXPECT_EQ(jsonLength % 4, 0u);
    const std::size_t binHeader = 20 + jsonLength;
    const std::uint32_t binLength = u32(bytes, binHeader);
    EXPECT_EQ(u32(bytes, binHeader + 4), 0x004E4942u); // "BIN"
    EXPECT_EQ(binHeader + 8 + binLength, bytes.size());

    const auto gltf = nlohmann::json::parse(std::string(bytes.data() + 20, jsonLength));
    EXPECT_EQ(gltf["asset"]["version"], "2.0");
    ASSERT_EQ(gltf["meshes"].size(), 2u);
    ASSERT_EQ(gltf["materials"].size(), 2u);
    EXPECT_EQ(gltf["nodes"][0]["children"].size(), 2u);
    EXPECT_EQ(gltf["nodes"][1]["name"], "Block");
    EXPECT_LE(gltf["buffers"][0]["byteLength"].get<std::size_t>(), binLength);

    // The block's positions span its size in millimetres (the root node scales to metres).
    const auto& block = gltf["accessors"][gltf["meshes"][0]["primitives"][0]["attributes"]["POSITION"].get<int>()];
    EXPECT_NEAR(block["max"][0].get<double>() - block["min"][0].get<double>(), 40.0, 1e-4);
    EXPECT_NEAR(block["max"][2].get<double>() - block["min"][2].get<double>(), 10.0, 1e-4);
    EXPECT_DOUBLE_EQ(gltf["nodes"][0]["scale"][0].get<double>(), 0.001);

    // Every index refers to a vertex of its mesh, and there are whole triangles.
    std::size_t triangles = 0;
    for (const auto& mesh : gltf["meshes"]) {
        const auto& primitive = mesh["primitives"][0];
        const auto& indices = gltf["accessors"][primitive["indices"].get<int>()];
        const auto& positions = gltf["accessors"][primitive["attributes"]["POSITION"].get<int>()];
        const auto& view = gltf["bufferViews"][indices["bufferView"].get<int>()];
        const std::size_t count = indices["count"];
        ASSERT_EQ(count % 3, 0u);
        triangles += count / 3;
        const std::size_t vertexCount = positions["count"];
        const std::size_t offset = binHeader + 8 + view["byteOffset"].get<std::size_t>();
        for (std::size_t i = 0; i < count; ++i) {
            ASSERT_LT(u32(bytes, offset + 4 * i), vertexCount);
        }
    }
    EXPECT_EQ(triangles, r.triangles);
}
