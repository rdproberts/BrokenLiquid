#pragma once

#include <TDF_Label.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>

#include <filesystem>
#include <string>
#include <vector>

namespace bl::kernel {

struct ImportedRoot {
    TDF_Label label;    // XCAF label (keeps colours, names, assembly structure)
    std::string name;   // UTF-8; may be empty
    TopoDS_Shape shape;
};

struct ImportResult {
    bool ok = false;
    std::string error;
    Handle(TDocStd_Document) doc;
    std::vector<ImportedRoot> roots;
    double seconds = 0.0;
};

// Reads a STEP file (AP203/AP214/AP242) into an XCAF document with colours, names and layers.
ImportResult importStep(const std::filesystem::path& file);

// Writes a single shape as STEP (AP214). Returns false and fills `error` on failure.
bool exportStep(const TopoDS_Shape& shape, const std::filesystem::path& file, std::string* error = nullptr);

// UTF-8 helpers for OCCT string types and paths.
std::string toUtf8(const std::filesystem::path& p);

} // namespace bl::kernel
