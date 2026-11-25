#pragma once

// Native part files (.blpart, PLAN.md 9.1): a ZIP container with
//   manifest.json      format, document type, schema version, app version
//   model.json         features, sketches, body names/colours, units
//   shapes/<id>.brep   imported bodies (OCCT binary B-rep)
// The bodies themselves are rebuilt from the features on opening.

#include "doc/PartModel.h"

#include <QString>

namespace bl::doc {

inline constexpr int kPartSchema = 1; // bump with a migration in PartFile.cpp when model.json changes
inline constexpr const char* kPartExtension = "blpart";

// Writes safely: a temporary file first, then the old file becomes <name>.bak.
bool savePart(const PartModel& model, const QString& path, QString* error = nullptr);

struct LoadedPart {
    bool ok = false;
    QString error;
    PartModel model; // not yet rebuilt
};
LoadedPart loadPart(const QString& path);

} // namespace bl::doc
