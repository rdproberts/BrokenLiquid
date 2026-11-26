#include "kernel/Export.h"

#include "kernel/StepIO.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Message_ProgressRange.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <RWObj_CafWriter.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>

namespace bl::kernel {
namespace {

using json = nlohmann::json;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

Quantity_Color toOcct(const std::array<double, 3>& srgb) {
    return Quantity_Color(srgb[0], srgb[1], srgb[2], Quantity_TOC_sRGB);
}

// An XDE document with one top-level shape per body, named and coloured.
Handle(TDocStd_Document) makeDocument(const std::vector<ExportBody>& bodies) {
    Handle(TDocStd_Document) doc;
    XCAFApp_Application::GetApplication()->NewDocument("MDTV-XCAF", doc);
    Handle(XCAFDoc_ShapeTool) shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    Handle(XCAFDoc_ColorTool) colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    for (const ExportBody& body : bodies) {
        const TDF_Label label = shapes->AddShape(body.shape, false);
        TDataStd_Name::Set(label, TCollection_ExtendedString(body.name.c_str(), true));
        colors->SetColor(label, toOcct(body.color), XCAFDoc_ColorSurf);
    }
    return doc;
}

void closeDocument(const Handle(TDocStd_Document)& doc) {
    if (!doc.IsNull() && doc->CanClose() == CDM_CCS_OK) {
        XCAFApp_Application::GetApplication()->Close(doc);
    }
}

TopoDS_Compound compoundOf(const std::vector<ExportBody>& bodies) {
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const ExportBody& body : bodies) {
        builder.Add(compound, body.shape);
    }
    return compound;
}

// Triangulates every body; returns the number of triangles.
std::size_t meshBodies(const std::vector<ExportBody>& bodies, MeshQuality quality) {
    std::size_t triangles = 0;
    for (const ExportBody& body : bodies) {
        const MeshTolerance tol = meshTolerance(body.shape, quality);
        BRepMesh_IncrementalMesh mesher(body.shape, tol.linear, false, tol.angular, true);
        for (TopExp_Explorer ex(body.shape, TopAbs_FACE); ex.More(); ex.Next()) {
            TopLoc_Location loc;
            const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(TopoDS::Face(ex.Current()), loc);
            if (!tri.IsNull()) {
                triangles += static_cast<std::size_t>(tri->NbTriangles());
            }
        }
    }
    return triangles;
}

void writeStep(const std::vector<ExportBody>& bodies, const std::filesystem::path& file, ExportResult& result) {
    Handle(TDocStd_Document) doc = makeDocument(bodies);
    STEPCAFControl_Writer writer;
    writer.SetColorMode(true);
    writer.SetNameMode(true);
    if (!writer.Transfer(doc, STEPControl_AsIs)) {
        result.error = "The bodies could not be converted to STEP.";
    } else if (writer.Write(toUtf8(file).c_str()) != IFSelect_RetDone) {
        result.error = "The STEP file could not be written.";
    } else {
        result.ok = true;
    }
    closeDocument(doc);
}

void writeStl(const std::vector<ExportBody>& bodies, const std::filesystem::path& file, bool ascii,
              ExportResult& result) {
    StlAPI_Writer writer;
    writer.ASCIIMode() = ascii;
    if (!writer.Write(compoundOf(bodies), toUtf8(file).c_str())) {
        result.error = "The STL file could not be written.";
        return;
    }
    result.ok = true;
}

void writeObj(const std::vector<ExportBody>& bodies, const std::filesystem::path& file, ExportResult& result) {
    Handle(TDocStd_Document) doc = makeDocument(bodies);
    RWObj_CafWriter writer(TCollection_AsciiString(toUtf8(file).c_str()));
    NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString> info;
    info.Add("Author", "BrokenLiquid");
    if (!writer.Perform(doc, info, Message_ProgressRange())) {
        result.error = "The OBJ file could not be written.";
    } else {
        result.ok = true;
    }
    closeDocument(doc);
}

// --- Binary glTF ------------------------------------------------------------------------------------
// OCCT's glTF writer needs RapidJSON, so BrokenLiquid writes the (small) format itself.

struct Mesh {
    std::vector<float> positions; // xyz, model units
    std::vector<float> normals;   // xyz
    std::vector<std::uint32_t> indices;
};

Mesh collectMesh(const TopoDS_Shape& shape) {
    Mesh mesh;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull() || tri->NbTriangles() == 0) {
            continue;
        }
        if (!tri->HasNormals()) {
            BRepLib_ToolTriangulatedShape::ComputeNormals(face, tri);
        }
        const gp_Trsf trsf = loc.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        const auto base = static_cast<std::uint32_t>(mesh.positions.size() / 3);
        for (int i = 1; i <= tri->NbNodes(); ++i) {
            const gp_Pnt p = tri->Node(i).Transformed(trsf);
            gp_Dir n = tri->HasNormals() ? tri->Normal(i) : gp_Dir(0, 0, 1);
            n.Transform(trsf);
            if (reversed) {
                n.Reverse();
            }
            mesh.positions.insert(mesh.positions.end(), {float(p.X()), float(p.Y()), float(p.Z())});
            mesh.normals.insert(mesh.normals.end(), {float(n.X()), float(n.Y()), float(n.Z())});
        }
        for (int t = 1; t <= tri->NbTriangles(); ++t) {
            int a = 0, b = 0, c = 0;
            tri->Triangle(t).Get(a, b, c);
            if (reversed) {
                std::swap(b, c);
            }
            mesh.indices.insert(mesh.indices.end(), {base + std::uint32_t(a - 1), base + std::uint32_t(b - 1),
                                                     base + std::uint32_t(c - 1)});
        }
    }
    return mesh;
}

double srgbToLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

template <class T>
void append(std::vector<char>& bin, const std::vector<T>& data) {
    const auto* bytes = reinterpret_cast<const char*>(data.data());
    bin.insert(bin.end(), bytes, bytes + data.size() * sizeof(T));
}

void writeGlb(const std::vector<ExportBody>& bodies, const std::filesystem::path& file, ExportResult& result) {
    constexpr int kFloat = 5126, kUint = 5125, kArrayBuffer = 34962, kElementBuffer = 34963, kTriangles = 4;
    std::vector<char> bin;
    json accessors = json::array(), views = json::array(), meshes = json::array(), materials = json::array();
    json children = json::array(), nodes = json::array();

    // Node 0 turns the model's Z-up millimetres into glTF's Y-up metres.
    const double h = std::sqrt(0.5);
    nodes.push_back({{"name", "BrokenLiquid"}, {"rotation", {-h, 0.0, 0.0, h}}, {"scale", {0.001, 0.001, 0.001}}});

    auto addView = [&](const auto& data, int target) {
        const std::size_t offset = bin.size();
        append(bin, data);
        views.push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", bin.size() - offset}, {"target", target}});
        return views.size() - 1;
    };

    for (const ExportBody& body : bodies) {
        const Mesh mesh = collectMesh(body.shape);
        if (mesh.indices.empty()) {
            continue;
        }
        std::array<float, 3> lo{}, hi{};
        lo.fill(std::numeric_limits<float>::max());
        hi.fill(std::numeric_limits<float>::lowest());
        for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
            lo[i % 3] = std::min(lo[i % 3], mesh.positions[i]);
            hi[i % 3] = std::max(hi[i % 3], mesh.positions[i]);
        }
        const std::size_t vertices = mesh.positions.size() / 3;
        const std::size_t position = accessors.size();
        accessors.push_back({{"bufferView", addView(mesh.positions, kArrayBuffer)}, {"componentType", kFloat},
                             {"count", vertices}, {"type", "VEC3"}, {"min", lo}, {"max", hi}});
        accessors.push_back({{"bufferView", addView(mesh.normals, kArrayBuffer)}, {"componentType", kFloat},
                             {"count", vertices}, {"type", "VEC3"}});
        accessors.push_back({{"bufferView", addView(mesh.indices, kElementBuffer)}, {"componentType", kUint},
                             {"count", mesh.indices.size()}, {"type", "SCALAR"}});

        const auto& c = body.color;
        materials.push_back({{"name", body.name},
                             {"pbrMetallicRoughness",
                              {{"baseColorFactor", {srgbToLinear(c[0]), srgbToLinear(c[1]), srgbToLinear(c[2]), 1.0}},
                               {"metallicFactor", 0.0},
                               {"roughnessFactor", 0.6}}}});
        meshes.push_back({{"name", body.name},
                          {"primitives",
                           {{{"attributes", {{"POSITION", position}, {"NORMAL", position + 1}}},
                             {"indices", position + 2},
                             {"material", materials.size() - 1},
                             {"mode", kTriangles}}}}});
        children.push_back(nodes.size());
        nodes.push_back({{"name", body.name}, {"mesh", meshes.size() - 1}});
    }
    if (meshes.empty()) {
        result.error = "The bodies have no triangles to export.";
        return;
    }
    nodes[0]["children"] = children;

    const json gltf{{"asset", {{"version", "2.0"}, {"generator", "BrokenLiquid"}}},
                    {"scene", 0},
                    {"scenes", {{{"nodes", {0}}}}},
                    {"nodes", nodes},
                    {"meshes", meshes},
                    {"materials", materials},
                    {"accessors", accessors},
                    {"bufferViews", views},
                    {"buffers", {{{"byteLength", bin.size()}}}}};

    std::string text = gltf.dump();
    text.resize((text.size() + 3) / 4 * 4, ' '); // chunks are 4-byte aligned
    bin.resize((bin.size() + 3) / 4 * 4, '\0');

    auto u32 = [](std::ofstream& out, std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        result.error = "The file could not be created.";
        return;
    }
    u32(out, 0x46546C67); // "glTF"
    u32(out, 2);
    u32(out, static_cast<std::uint32_t>(12 + 8 + text.size() + 8 + bin.size()));
    u32(out, static_cast<std::uint32_t>(text.size()));
    u32(out, 0x4E4F534A); // "JSON"
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    u32(out, static_cast<std::uint32_t>(bin.size()));
    u32(out, 0x004E4942); // "BIN"
    out.write(bin.data(), static_cast<std::streamsize>(bin.size()));
    out.close();
    if (!out) {
        result.error = "The glTF file could not be written.";
        return;
    }
    result.ok = true;
}

} // namespace

std::optional<ExportFormat> exportFormatFor(const std::filesystem::path& file) {
    const std::string ext = lower(file.extension().string());
    if (ext == ".step" || ext == ".stp") {
        return ExportFormat::Step;
    }
    if (ext == ".stl") {
        return ExportFormat::Stl;
    }
    if (ext == ".obj") {
        return ExportFormat::Obj;
    }
    if (ext == ".glb") {
        return ExportFormat::Glb;
    }
    return std::nullopt;
}

const char* exportExtension(ExportFormat format) {
    switch (format) {
    case ExportFormat::Step: return "step";
    case ExportFormat::Stl: return "stl";
    case ExportFormat::Obj: return "obj";
    case ExportFormat::Glb: return "glb";
    }
    return "";
}

bool isMeshFormat(ExportFormat format) { return format != ExportFormat::Step; }

TopoDS_Shape detachedCopy(const TopoDS_Shape& shape) {
    if (shape.IsNull()) {
        return shape;
    }
    BRepBuilderAPI_Copy copy(shape, true, false);
    return copy.Shape();
}

MeshTolerance meshTolerance(const TopoDS_Shape& shape, MeshQuality quality) {
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    const double size = box.IsVoid() ? 1.0 : std::sqrt(box.SquareExtent());
    constexpr double deg = std::numbers::pi / 180.0;
    switch (quality) {
    case MeshQuality::Coarse: return {std::max(size * 0.003, 1e-3), 30.0 * deg};
    case MeshQuality::Fine: return {std::max(size * 0.0002, 1e-4), 8.0 * deg};
    case MeshQuality::Normal: break;
    }
    return {std::max(size * 0.001, 5e-4), 15.0 * deg};
}

ExportResult exportBodies(const std::vector<ExportBody>& bodies, const std::filesystem::path& file,
                          ExportFormat format, const ExportOptions& options) {
    ExportResult result;
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<ExportBody> valid;
    for (const ExportBody& body : bodies) {
        if (!body.shape.IsNull()) {
            valid.push_back(body);
        }
    }
    if (valid.empty()) {
        result.error = "There is nothing to export.";
        return result;
    }
    try {
        if (isMeshFormat(format)) {
            result.triangles = meshBodies(valid, options.quality);
        }
        switch (format) {
        case ExportFormat::Step: writeStep(valid, file, result); break;
        case ExportFormat::Stl: writeStl(valid, file, options.stlAscii, result); break;
        case ExportFormat::Obj: writeObj(valid, file, result); break;
        case ExportFormat::Glb: writeGlb(valid, file, result); break;
        }
    } catch (const Standard_Failure& f) {
        result.ok = false;
        result.error = std::string("Geometry kernel error: ") + f.what();
    } catch (const std::exception& e) {
        result.ok = false;
        result.error = e.what();
    }
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (result.ok) {
        spdlog::info("exported {} body(s) to {} ({} triangles) in {:.2f}s", valid.size(), toUtf8(file),
                     result.triangles, result.seconds);
    } else {
        spdlog::warn("export to {} failed: {}", toUtf8(file), result.error);
    }
    return result;
}

} // namespace bl::kernel
