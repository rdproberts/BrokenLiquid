#pragma once

#include "core/Units.h"

#include <TDF_Label.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>

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

// The editable state of a part document. Undo/redo snapshots this whole struct.
struct PartModel {
    std::vector<Body> bodies;
    Units units;
    int importCount = 0; // numbering for "Imported1", "Imported2", ...

    Body* body(const QUuid& id) {
        for (Body& b : bodies) {
            if (b.id == id) {
                return &b;
            }
        }
        return nullptr;
    }
    const Body* body(const QUuid& id) const { return const_cast<PartModel*>(this)->body(id); }
};

} // namespace bl::doc
