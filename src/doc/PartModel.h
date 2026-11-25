#pragma once

#include "core/Units.h"
#include "sketch/Sketch.h"
#include "sketch/Solver.h"

#include <TDF_Label.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>

#include <QColor>
#include <QString>
#include <QUuid>

#include <optional>
#include <vector>

namespace bl::doc {

// One solid body of a part. Phase 1 bodies are "dumb" imported solids; Phase 3 adds features that
// produce bodies. Copying a Body is cheap: shapes and XCAF documents are reference counted.
struct Body {
    QUuid id = QUuid::createUuid();
    QString name;
    TopoDS_Shape shape;
    Handle(TDocStd_Document) xcaf; // source document for imported colours/names (may be null)
    TDF_Label label;               // root label in `xcaf`
    QString sourceFile;            // where it was imported from
    bool visible = true;
    std::optional<QColor> color;   // user override; unset = imported or default colour
};

// A 2D sketch on a plane (PLAN.md 6.6). The plane's X and Y directions are the sketch's axes and
// its location is the sketch origin.
struct SketchFeature {
    QUuid id = QUuid::createUuid();
    QString name;            // "Sketch1"
    QString planeName;       // "Front Plane", "Face of Body1"
    gp_Ax3 plane;
    ::bl::sketch::Sketch sketch;
    ::bl::sketch::SketchStatus status = ::bl::sketch::SketchStatus::UnderDefined; // as last solved
    bool visible = true;
};

// The editable state of a part document. Undo/redo snapshots this whole struct.
struct PartModel {
    std::vector<Body> bodies;
    std::vector<SketchFeature> sketches; // in creation order (the history, until Phase 3 features)
    Units units;
    int importCount = 0; // numbering for "Imported1", "Imported2", ...
    int sketchCount = 0; // numbering for "Sketch1", "Sketch2", ...

    Body* body(const QUuid& id) {
        for (Body& b : bodies) {
            if (b.id == id) {
                return &b;
            }
        }
        return nullptr;
    }
    const Body* body(const QUuid& id) const { return const_cast<PartModel*>(this)->body(id); }

    SketchFeature* sketch(const QUuid& id) {
        for (SketchFeature& s : sketches) {
            if (s.id == id) {
                return &s;
            }
        }
        return nullptr;
    }
    const SketchFeature* sketch(const QUuid& id) const { return const_cast<PartModel*>(this)->sketch(id); }
};

} // namespace bl::doc
