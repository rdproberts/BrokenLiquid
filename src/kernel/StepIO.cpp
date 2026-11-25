#include "kernel/StepIO.h"

#include <IFSelect_ReturnStatus.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TCollection_AsciiString.hxx>
#include <NCollection_Sequence.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <spdlog/spdlog.h>

#include <chrono>

namespace bl::kernel {

std::string toUtf8(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

namespace {

std::string labelName(const TDF_Label& label) {
    Handle(TDataStd_Name) attr;
    if (!label.FindAttribute(TDataStd_Name::GetID(), attr)) {
        return {};
    }
    // Replacement char 0 => convert to UTF-8.
    const TCollection_AsciiString utf8(attr->Get(), 0);
    return utf8.ToCString();
}

} // namespace

ImportResult importStep(const std::filesystem::path& file) {
    ImportResult result;
    const auto t0 = std::chrono::steady_clock::now();
    const std::string path = toUtf8(file);

    try {
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("MDTV-XCAF", result.doc);

        STEPCAFControl_Reader reader;
        reader.SetColorMode(true);
        reader.SetNameMode(true);
        reader.SetLayerMode(true);

        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
            result.error = "The file could not be read as STEP.";
            return result;
        }
        if (!reader.Transfer(result.doc)) {
            result.error = "The STEP file was read but no geometry could be transferred.";
            return result;
        }

        Handle(XCAFDoc_ShapeTool) shapeTool = XCAFDoc_DocumentTool::ShapeTool(result.doc->Main());
        NCollection_Sequence<TDF_Label> freeShapes;
        shapeTool->GetFreeShapes(freeShapes);
        for (int i = 1; i <= freeShapes.Length(); ++i) {
            const TDF_Label& label = freeShapes.Value(i);
            ImportedRoot root;
            root.label = label;
            root.name = labelName(label);
            root.shape = XCAFDoc_ShapeTool::GetShape(label);
            result.roots.push_back(std::move(root));
        }
        if (result.roots.empty()) {
            result.error = "The STEP file contains no shapes.";
            return result;
        }
        result.ok = true;
    } catch (const Standard_Failure& f) {
        result.error = std::string("Geometry kernel error: ") + f.what();
    } catch (const std::exception& e) {
        result.error = e.what();
    }

    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (result.ok) {
        spdlog::info("imported STEP {} ({} roots) in {:.2f}s", path, result.roots.size(), result.seconds);
    } else {
        spdlog::warn("STEP import failed for {}: {}", path, result.error);
    }
    return result;
}

bool exportStep(const TopoDS_Shape& shape, const std::filesystem::path& file, std::string* error) {
    try {
        STEPControl_Writer writer;
        if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) {
            if (error) {
                *error = "Shape could not be converted to STEP.";
            }
            return false;
        }
        if (writer.Write(toUtf8(file).c_str()) != IFSelect_RetDone) {
            if (error) {
                *error = "STEP file could not be written.";
            }
            return false;
        }
        return true;
    } catch (const Standard_Failure& f) {
        if (error) {
            *error = std::string("Geometry kernel error: ") + f.what();
        }
        return false;
    }
}

} // namespace bl::kernel
