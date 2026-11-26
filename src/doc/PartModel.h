#pragma once

#include "core/Units.h"
#include "kernel/Naming.h"
#include "sketch/Sketch.h"
#include "sketch/Solver.h"

#include <TDF_Label.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>

#include <QColor>
#include <QString>
#include <QUuid>

#include <memory>
#include <optional>
#include <vector>

namespace bl::doc {

// One solid body of a part, as the last rebuild left it (PLAN.md 6.4). Bodies are made by
// features; their name, visibility and colour are the user's and survive rebuilds. Copying a Body
// is cheap: shapes and XCAF documents are reference counted.
struct Body {
    QUuid id = QUuid::createUuid();
    QString name;
    TopoDS_Shape shape;
    kernel::NamedShape named;      // `shape` with persistent face names (ADR 0003)
    QUuid feature;                 // the feature that created the body
    Handle(TDocStd_Document) xcaf; // imported colours/names; only while the shape is the imported one
    TDF_Label label;               // root label in `xcaf`
    QString sourceFile;            // where it was imported from
    bool visible = true;
    std::optional<QColor> color;   // user override; unset = imported or default colour
};

// A 2D sketch on a plane (PLAN.md 6.6). The plane's X and Y directions are the sketch's axes and
// its location is the sketch origin. A sketch on a model face keeps a reference to the face and
// follows it when the model changes.
struct SketchFeature {
    QUuid id = QUuid::createUuid();
    QString name;            // "Sketch1"
    QString planeName;       // "Front Plane", "Face of Body1"
    gp_Ax3 plane;
    std::optional<kernel::FaceRef> face; // set when sketched on a model face
    ::bl::sketch::Sketch sketch;
    ::bl::sketch::SketchStatus status = ::bl::sketch::SketchStatus::UnderDefined; // as last solved
    bool visible = true;
};

enum class FeatureType { Import, Sketch, Extrude, Revolve, Fillet, Chamfer };

enum class FeatureState {
    Ok,
    Warning,    // built, but something needs a look (a reference had to be repaired)
    Error,      // not built: `message` says why; later features build without it
    Suppressed, // switched off by the user
    RolledBack, // below the rollback bar
};

enum class EndCondition { Blind, ThroughAll, MidPlane };

// Extruded Boss/Base and Extruded Cut (PLAN.md 6.7).
struct ExtrudeParams {
    QUuid sketch;
    bool cut = false;
    EndCondition end = EndCondition::Blind;
    double depth = 10.0;    // mm; Mid Plane: total depth
    bool reverse = false;   // extrude against the sketch normal
    bool direction2 = false; // also extrude the other way, `depth2` (blind)
    double depth2 = 10.0;
    bool merge = true;      // boss: merge with the bodies it touches (false: always a new body)

    bool operator==(const ExtrudeParams&) const = default;
};

// Revolved Boss/Base and Revolved Cut (PLAN.md 6.7): the sketch's closed regions turned about a
// line of the same sketch (normally a centerline).
struct RevolveParams {
    QUuid sketch;
    bool cut = false;
    ::bl::sketch::Id axis = 0; // a line of the sketch; 0: its first centerline
    double angle = 360.0;  // degrees, (0, 360]
    bool reverse = false;  // turn the other way round the axis
    bool midPlane = false; // half the angle each way from the sketch plane
    bool merge = true;

    bool operator==(const RevolveParams&) const = default;
};

// Fillet (constant radius) and Chamfer on picked model edges (PLAN.md 6.7). The edges are stored as
// persistent references (ADR 0003) and found again on every rebuild.
struct FilletParams {
    std::vector<kernel::EdgeRef> edges;
    double radius = 2.0;

    bool operator==(const FilletParams&) const = default;
};

enum class ChamferType { Distance, TwoDistances };

struct ChamferParams {
    std::vector<kernel::EdgeRef> edges;
    ChamferType type = ChamferType::Distance;
    double distance = 1.0;
    double distance2 = 1.0; // TwoDistances: on the second face of each edge
    bool flip = false;      // TwoDistances: swap which face takes which distance

    bool operator==(const ChamferParams&) const = default;
};

// A body imported from a STEP file (a dumb solid other features can build on).
struct ImportParams {
    TopoDS_Shape shape;
    Handle(TDocStd_Document) xcaf;
    TDF_Label label;
    QString sourceFile;
    QString bodyName;

    bool operator==(const ImportParams& o) const {
        return shape.IsSame(o.shape) && sourceFile == o.sourceFile && bodyName == o.bodyName;
    }
};

// The bodies at one point of the history, with persistent names.
struct BuiltBody {
    QUuid id;
    QUuid creator;
    kernel::NamedShape shape;
};
struct BuildState {
    std::vector<BuiltBody> bodies;
};

// One entry of the part's history (the Feature Tree). Sketch entries keep their data in
// PartModel::sketches under the same id.
struct Feature {
    QUuid id = QUuid::createUuid();
    FeatureType type = FeatureType::Extrude;
    QString name; // sketches: their name lives in the SketchFeature
    bool suppressed = false;
    QUuid body; // the body this feature creates (imports; bosses that start a new body)
    ImportParams import;
    ExtrudeParams extrude;
    RevolveParams revolve;
    FilletParams fillet;
    ChamferParams chamfer;

    // Results of the last rebuild (not compared when deciding whether anything changed).
    FeatureState state = FeatureState::Ok;
    QString message;
    std::shared_ptr<const BuildState> after; // bodies after this feature, reused while unchanged

    bool sameInput(const Feature& o) const {
        return id == o.id && type == o.type && name == o.name && suppressed == o.suppressed && body == o.body &&
               import == o.import && extrude == o.extrude && revolve == o.revolve && fillet == o.fillet &&
               chamfer == o.chamfer;
    }
    // The sketch a sketch-based feature (extrude, revolve) uses; null for others.
    QUuid sketchUsed() const {
        switch (type) {
        case FeatureType::Extrude: return extrude.sketch;
        case FeatureType::Revolve: return revolve.sketch;
        default: return {};
        }
    }
};

// The editable state of a part document. Undo/redo snapshots this whole struct.
struct PartModel {
    std::vector<Feature> features;       // the history, in order
    std::vector<SketchFeature> sketches; // sketch data (their history entries are in `features`)
    std::vector<Body> bodies;            // rebuild output, with the user's names/colours
    int rollback = -1;                   // features from this index on are rolled back (-1: none)
    Units units;
    int importCount = 0;  // numbering for "Imported1", ...
    int sketchCount = 0;  // "Sketch1", ...
    int extrudeCount = 0; // "Boss-Extrude1", ...
    int cutCount = 0;     // "Cut-Extrude1", ...
    int revolveCount = 0;    // "Revolve1", ...
    int revolveCutCount = 0; // "Cut-Revolve1", ...
    int filletCount = 0;     // "Fillet1", ...
    int chamferCount = 0;    // "Chamfer1", ...

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

    Feature* feature(const QUuid& id) {
        for (Feature& f : features) {
            if (f.id == id) {
                return &f;
            }
        }
        return nullptr;
    }
    const Feature* feature(const QUuid& id) const { return const_cast<PartModel*>(this)->feature(id); }
    int featureIndex(const QUuid& id) const {
        for (size_t i = 0; i < features.size(); ++i) {
            if (features[i].id == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    // The feature that uses a sketch (it shows the sketch as its child in the tree), if any.
    const Feature* consumerOf(const QUuid& sketch) const {
        for (const Feature& f : features) {
            if (!sketch.isNull() && f.sketchUsed() == sketch) {
                return &f;
            }
        }
        return nullptr;
    }
    // Display name of a history entry.
    QString featureName(const Feature& f) const {
        if (f.type == FeatureType::Sketch) {
            const SketchFeature* s = sketch(f.id);
            return s ? s->name : f.name;
        }
        return f.name;
    }
};

// Adds an imported body (feature + body) to the end of the history.
void addImport(PartModel& model, const TopoDS_Shape& shape, const QString& name, const QString& sourceFile,
               Handle(TDocStd_Document) xcaf = {}, TDF_Label label = {});

} // namespace bl::doc
