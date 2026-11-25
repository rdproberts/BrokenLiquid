#include "doc/Rebuild.h"

#include "kernel/Features.h"
#include "sketch/Operations.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <QCoreApplication>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <limits>

namespace bl::doc {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("bl::doc::Rebuild", text); }

bool samePlane(const gp_Ax3& a, const gp_Ax3& b) {
    return a.Location().IsEqual(b.Location(), 0.0) && a.Direction().IsEqual(b.Direction(), 0.0) &&
           a.XDirection().IsEqual(b.XDirection(), 0.0);
}

// Did anything that feeds the rebuild change in a sketch? (A sketch on a face gets its plane from
// the rebuild, so only the face reference counts there.)
bool sameSketchInput(const SketchFeature* a, const SketchFeature* b) {
    if (!a || !b) {
        return a == b;
    }
    if (!(a->sketch == b->sketch) || a->face != b->face) {
        return false;
    }
    return a->face || samePlane(a->plane, b->plane);
}

// Every sketch has a history entry and every sketch entry a sketch.
void normalizeHistory(PartModel& m) {
    std::erase_if(m.features, [&m](const Feature& f) { return f.type == FeatureType::Sketch && !m.sketch(f.id); });
    for (const SketchFeature& s : m.sketches) {
        if (!m.feature(s.id)) {
            Feature f;
            f.id = s.id;
            f.type = FeatureType::Sketch;
            if (m.rollback >= 0 && m.rollback <= static_cast<int>(m.features.size())) {
                m.features.insert(m.features.begin() + m.rollback, f);
                ++m.rollback; // new work goes above the bar, which stays below it
            } else {
                m.features.push_back(f);
            }
        }
    }
    if (m.rollback >= static_cast<int>(m.features.size())) {
        m.rollback = -1;
    }
}

std::vector<const kernel::NamedShape*> shapesOf(const BuildState& state) {
    std::vector<const kernel::NamedShape*> out;
    for (const BuiltBody& b : state.bodies) {
        out.push_back(&b.shape);
    }
    return out;
}

void setError(Feature& f, const QString& text) {
    f.state = FeatureState::Error;
    f.message = text;
}

void buildImport(Feature& f, BuildState& state) {
    if (f.import.shape.IsNull()) {
        setError(f, tr("The imported shape is empty."));
        return;
    }
    if (f.body.isNull()) {
        f.body = QUuid::createUuid();
    }
    state.bodies.push_back({f.body, f.id, kernel::namedImport(f.import.shape, namingKey(f.id))});
}

void buildSketch(PartModel& m, Feature& f, const BuildState& state) {
    SketchFeature* s = m.sketch(f.id);
    if (!s || !s->face) {
        return;
    }
    const kernel::Resolved r = kernel::resolve(shapesOf(state), *s->face);
    if (r.status == kernel::RefStatus::Missing) {
        setError(f, tr("The face this sketch is on is gone. Edit the sketch plane or delete the sketch."));
        return;
    }
    const std::optional<gp_Pln> plane = kernel::planeOfFace(r.face);
    if (!plane) {
        setError(f, tr("The face this sketch is on is no longer flat."));
        return;
    }
    s->plane = kernel::followFace(s->plane, *plane);
    if (r.status != kernel::RefStatus::Exact) {
        f.state = FeatureState::Warning;
        f.message = r.status == kernel::RefStatus::Ambiguous
                        ? tr("The face this sketch is on was split; the sketch is on the closest part.")
                        : tr("The face this sketch is on was replaced; the sketch is on a face that looks the same.");
    }
}

// Distance from the plane origin to the far side of all bodies, along `direction`.
std::optional<double> throughAllLength(const BuildState& state, const gp_Pnt& origin, const gp_Dir& direction) {
    Bnd_Box box;
    for (const BuiltBody& b : state.bodies) {
        BRepBndLib::Add(b.shape.shape, box);
    }
    if (box.IsVoid()) {
        return std::nullopt;
    }
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    double far = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < 8; ++i) {
        const gp_Pnt corner(i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0);
        far = std::max(far, gp_Vec(origin, corner).Dot(gp_Vec(direction)));
    }
    if (far <= 1e-6) {
        return std::nullopt;
    }
    return far + 1.0 + 0.01 * std::sqrt(box.SquareExtent());
}

} // namespace

kernel::NamedShape extrudeTool(const SketchFeature& sk, const ExtrudeParams& p, const BuildState& state,
                               const std::string& featureKey, QString* error) {
    auto fail = [error](const QString& text) {
        if (error) {
            *error = text;
        }
        return kernel::NamedShape{};
    };
    const sketch::Contours contours = sketch::findRegions(sk.sketch);
    const std::vector<int> regions = kernel::defaultRegions(contours);
    if (regions.empty()) {
        return fail(tr("The sketch has no closed contour to extrude."));
    }
    const gp_Dir normal = p.reverse ? sk.plane.Direction().Reversed() : sk.plane.Direction();
    double start = 0.0;
    double length = p.depth;
    switch (p.end) {
    case EndCondition::Blind: break;
    case EndCondition::MidPlane: start = -0.5 * p.depth; break;
    case EndCondition::ThroughAll: {
        const std::optional<double> l = throughAllLength(state, sk.plane.Location(), normal);
        if (!l) {
            return fail(tr("Through All needs a body on that side of the sketch."));
        }
        length = *l;
        break;
    }
    }
    if (p.direction2 && p.end != EndCondition::MidPlane) {
        start -= p.depth2;
        length += p.depth2;
    }
    if (length <= 1e-7) {
        return fail(tr("The depth must be more than zero."));
    }
    gp_Ax3 plane = sk.plane;
    plane.Translate(gp_Vec(normal) * start);

    std::string message;
    const kernel::Profile profile = kernel::buildProfile(sk.sketch, plane, contours, regions, &message);
    if (profile.empty()) {
        return fail(tr("The sketch cannot be extruded: %1.").arg(QString::fromStdString(message)));
    }
    kernel::NamedShape tool = kernel::extrude(profile, normal, length, featureKey, namingKey(sk.id), &message);
    if (tool.isNull()) {
        return fail(tr("The extrusion failed: %1.").arg(QString::fromStdString(message)));
    }
    return tool;
}

namespace {

void buildExtrude(PartModel& m, Feature& f, int index, BuildState& state) {
    const ExtrudeParams& p = f.extrude;
    const SketchFeature* sk = m.sketch(p.sketch);
    const int sketchIndex = m.featureIndex(p.sketch);
    if (!sk || sketchIndex < 0) {
        setError(f, tr("Its sketch was deleted."));
        return;
    }
    if (sketchIndex > index) {
        setError(f, tr("Its sketch comes later in the history."));
        return;
    }
    const Feature& sketchFeature = m.features[static_cast<size_t>(sketchIndex)];
    if (sketchFeature.suppressed) {
        setError(f, tr("Its sketch is suppressed."));
        return;
    }
    if (sketchFeature.state == FeatureState::Error) {
        setError(f, tr("Its sketch has an error."));
        return;
    }
    QString why;
    const kernel::NamedShape tool = extrudeTool(*sk, p, state, namingKey(f.id), &why);
    if (tool.isNull()) {
        setError(f, why);
        return;
    }
    std::string error;

    if (!p.cut) {
        if (f.body.isNull()) {
            f.body = QUuid::createUuid();
        }
        std::vector<size_t> touching;
        if (p.merge) {
            for (size_t i = 0; i < state.bodies.size(); ++i) {
                if (kernel::touches(state.bodies[i].shape.shape, tool.shape)) {
                    touching.push_back(i);
                }
            }
        }
        if (touching.empty()) {
            state.bodies.push_back({f.body, f.id, tool});
            return;
        }
        // Merge into the first body it touches; the others it touches join that body.
        kernel::NamedShape merged = tool;
        for (size_t i : touching) {
            merged = kernel::fuse(state.bodies[i].shape, merged, &error);
            if (merged.isNull()) {
                setError(f, tr("The boss could not be merged: %1.").arg(QString::fromStdString(error)));
                return;
            }
        }
        state.bodies[touching.front()].shape = std::move(merged);
        for (auto it = touching.rbegin(); it != touching.rend() && *it != touching.front(); ++it) {
            state.bodies.erase(state.bodies.begin() + static_cast<std::ptrdiff_t>(*it));
        }
        return;
    }

    bool any = false;
    for (size_t i = 0; i < state.bodies.size();) {
        BuiltBody& body = state.bodies[i];
        if (!kernel::touches(body.shape.shape, tool.shape)) {
            ++i;
            continue;
        }
        kernel::NamedShape result = kernel::cut(body.shape, tool, &error);
        if (result.isNull()) {
            setError(f, tr("The cut failed: %1.").arg(QString::fromStdString(error)));
            return;
        }
        any = true;
        if (!kernel::hasSolid(result.shape)) {
            state.bodies.erase(state.bodies.begin() + static_cast<std::ptrdiff_t>(i)); // cut away completely
            continue;
        }
        body.shape = std::move(result);
        ++i;
    }
    if (!any) {
        setError(f, tr("The cut does not touch any body."));
    }
}

// Bodies for display from the final state, keeping what the user set on them.
void publishBodies(PartModel& m, const BuildState& state) {
    std::vector<Body> out;
    out.reserve(state.bodies.size());
    for (const BuiltBody& b : state.bodies) {
        Body body;
        body.id = b.id;
        body.feature = b.creator;
        body.named = b.shape;
        body.shape = b.shape.shape;
        const Feature* creator = m.feature(b.creator);
        if (const Body* old = m.body(b.id)) {
            body.name = old->name;
            body.visible = old->visible;
            body.color = old->color;
        } else if (creator) {
            body.name = creator->type == FeatureType::Import && !creator->import.bodyName.isEmpty()
                            ? creator->import.bodyName
                            : creator->name;
        }
        if (creator && creator->type == FeatureType::Import) {
            body.sourceFile = creator->import.sourceFile;
            if (body.shape.IsSame(creator->import.shape)) { // imported colours fit the unchanged shape only
                body.xcaf = creator->import.xcaf;
                body.label = creator->import.label;
            }
        }
        out.push_back(std::move(body));
    }
    m.bodies = std::move(out);
}

} // namespace

std::string namingKey(const QUuid& id) { return id.toString(QUuid::WithoutBraces).left(8).toStdString(); }

void rebuild(PartModel& m, const PartModel* previous) {
    const auto started = std::chrono::steady_clock::now();
    normalizeHistory(m);
    const size_t n = m.features.size();
    const int rollback = m.rollback < 0 ? INT_MAX : m.rollback;

    // First feature whose result cannot be reused.
    size_t k = 0;
    if (previous) {
        const int oldRollback = previous->rollback < 0 ? INT_MAX : previous->rollback;
        const size_t limit = static_cast<size_t>(std::min({rollback, oldRollback, INT_MAX - 1}));
        while (k < n && k < previous->features.size() && k < limit) {
            const Feature& a = m.features[k];
            const Feature& b = previous->features[k];
            if (!a.sameInput(b) || !b.after ||
                (a.type == FeatureType::Sketch && !sameSketchInput(m.sketch(a.id), previous->sketch(b.id)))) {
                break;
            }
            m.features[k].after = b.after;
            m.features[k].state = b.state;
            m.features[k].message = b.message;
            ++k;
        }
    }

    std::shared_ptr<const BuildState> state =
        k > 0 ? m.features[k - 1].after : std::make_shared<const BuildState>();
    for (size_t i = k; i < n; ++i) {
        Feature& f = m.features[i];
        f.message.clear();
        f.state = FeatureState::Ok;
        if (static_cast<int>(i) >= rollback) {
            f.state = FeatureState::RolledBack;
            f.after = state;
            continue;
        }
        if (f.suppressed) {
            f.state = FeatureState::Suppressed;
            f.after = state;
            continue;
        }
        BuildState next = *state;
        switch (f.type) {
        case FeatureType::Import: buildImport(f, next); break;
        case FeatureType::Sketch: buildSketch(m, f, next); break;
        case FeatureType::Extrude: buildExtrude(m, f, static_cast<int>(i), next); break;
        }
        if (f.state == FeatureState::Error) {
            f.after = state; // a failed feature changes nothing
            spdlog::warn("rebuild: {} failed: {}", m.featureName(f).toStdString(), f.message.toStdString());
            continue;
        }
        f.after = std::make_shared<const BuildState>(std::move(next));
        state = f.after;
    }
    publishBodies(m, *state);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (k < n) {
        spdlog::debug("rebuild: features {}..{} of {} in {:.1f} ms", k + 1, n, n, ms);
    }
}

void addImport(PartModel& model, const TopoDS_Shape& shape, const QString& name, const QString& sourceFile,
               Handle(TDocStd_Document) xcaf, TDF_Label label) {
    Feature f;
    f.type = FeatureType::Import;
    f.name = QStringLiteral("Imported%1").arg(++model.importCount);
    f.body = QUuid::createUuid();
    f.import.shape = shape;
    f.import.xcaf = std::move(xcaf);
    f.import.label = label;
    f.import.sourceFile = sourceFile;
    f.import.bodyName = name.isEmpty() ? f.name : name;
    model.features.push_back(std::move(f));
}

} // namespace bl::doc
