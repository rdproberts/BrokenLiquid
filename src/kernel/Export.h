#pragma once

// Exporting bodies to exchange formats (PLAN.md 9.2):
//   STEP  exact B-rep, with body names and colours (AP214)
//   STL   triangle mesh, binary or ASCII, millimetres
//   OBJ   triangle mesh with a .mtl file for colours, millimetres
//   GLB   binary glTF 2.0: one mesh and material per body, metres, Y up (as the format requires)

#include <TopoDS_Shape.hxx>

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bl::kernel {

enum class ExportFormat { Step, Stl, Obj, Glb };

// The format for a file name's extension (case-insensitive): .step/.stp, .stl, .obj, .glb.
std::optional<ExportFormat> exportFormatFor(const std::filesystem::path& file);
// The usual extension without the dot ("step", "stl", "obj", "glb").
const char* exportExtension(ExportFormat format);
bool isMeshFormat(ExportFormat format);

enum class MeshQuality { Coarse, Normal, Fine };

struct ExportBody {
    std::string name;                                // UTF-8
    TopoDS_Shape shape;
    std::array<double, 3> color{0.72, 0.76, 0.81};   // sRGB, 0..1
};

struct ExportOptions {
    MeshQuality quality = MeshQuality::Normal;
    bool stlAscii = false;
};

struct ExportResult {
    bool ok = false;
    std::string error;
    std::size_t triangles = 0; // mesh formats
    double seconds = 0.0;
};

// Writes `bodies` to `file`. Mesh formats triangulate the shapes in place: pass shapes from
// detachedCopy() when the originals are on screen and this runs on a worker thread.
ExportResult exportBodies(const std::vector<ExportBody>& bodies, const std::filesystem::path& file,
                          ExportFormat format, const ExportOptions& options = {});

// A copy of `shape` that shares no topology or mesh with it.
TopoDS_Shape detachedCopy(const TopoDS_Shape& shape);

// Mesh tolerances for `shape` at `quality`: the chord error scales with the shape's size.
struct MeshTolerance {
    double linear = 0.1;  // model units (mm)
    double angular = 0.5; // radians
};
MeshTolerance meshTolerance(const TopoDS_Shape& shape, MeshQuality quality);

} // namespace bl::kernel
