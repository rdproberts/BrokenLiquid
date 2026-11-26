#include "doc/Rebuild.h"

#include "kernel/Features.h"
#include "sketch/Operations.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <gp_Ax1.hxx>

#include <QCoreApplication>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <limits>
#include <map>
#include <numbers>

namespace bl::doc {
namespace {

QString tr(const char* text, const char* disambiguation = nullptr, int n = -1) {
    return QCoreApplication::translate("bl::doc::Rebuild", text, disambiguation, n);
}

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

sketch::Id revolveAxis(const sketch::Sketch& s, sketch::Id axis) {
    if (axis != 0) {
        const sketch::Curve* c = s.curve(axis);
        return c && c->type == sketch::CurveType::Line ? axis : 0;
    }
    for (const sketch::Curve& c : s.curves()) {
        if (c.type == sketch::CurveType::Line && c.construction) {
            return c.id;
        }
    }
    return 0;
}

kernel::NamedShape revolveTool(const SketchFeature& sk, const RevolveParams& p, const std::string& featureKey,
                               QString* error) {
    auto fail = [error](const QString& text) {
        if (error) {
            *error = text;
        }
        return kernel::NamedShape{};
    };
    const sketch::Contours contours = sketch::findRegions(sk.sketch);
    const std::vector<int> regions = kernel::defaultRegions(contours);
    if (regions.empty()) {
        return fail(tr("The sketch has no closed contour to revolve."));
    }
    const sketch::Id axisId = revolveAxis(sk.sketch, p.axis);
    if (axisId == 0) {
        return fail(p.axis != 0 ? tr("The axis line is gone from the sketch. Pick another axis.")
                                : tr("Draw a centreline in the sketch to revolve about."));
    }
    const sketch::Curve* axisCurve = sk.sketch.curve(axisId);
    const sketch::Point* a = sk.sketch.point(axisCurve->p1);
    const sketch::Point* b = sk.sketch.point(axisCurve->p2);
    if (!a || !b || std::hypot(b->x - a->x, b->y - a->y) < 1e-9) {
        return fail(tr("The axis line has no length."));
    }
    // The profile must stay on one side of the axis (touching it is fine).
    const double ax = b->x - a->x, ay = b->y - a->y;
    const double axisLength = std::hypot(ax, ay);
    double lo = 0.0, hi = 0.0, extent = 1.0;
    for (int index : regions) {
        for (const sketch::P2& q : contours.regions[static_cast<size_t>(index)].outer.polygon) {
            const double side = (ax * (q.y - a->y) - ay * (q.x - a->x)) / axisLength;
            lo = std::min(lo, side);
            hi = std::max(hi, side);
            extent = std::max({extent, std::abs(q.x - a->x), std::abs(q.y - a->y)});
        }
    }
    if (lo < -1e-6 * extent && hi > 1e-6 * extent) {
        return fail(tr("The profile crosses the axis. Keep it on one side of the centreline."));
    }
    if (!(p.angle > 1e-6) || p.angle > 360.0 + 1e-9) {
        return fail(tr("The angle must be more than 0 and at most 360 degrees."));
    }
    auto at = [&sk](double x, double y) {
        return sk.plane.Location()
            .Translated(gp_Vec(sk.plane.XDirection()) * x)
            .Translated(gp_Vec(sk.plane.YDirection()) * y);
    };
    const gp_Ax1 axis(at(a->x, a->y), gp_Dir(gp_Vec(at(a->x, a->y), at(b->x, b->y))));
    double angle = p.angle * std::numbers::pi / 180.0;
    if (p.reverse) {
        angle = -angle;
    }
    gp_Ax3 plane = sk.plane;
    if (p.midPlane && p.angle < 360.0 - 1e-9) {
        plane.Rotate(axis, -0.5 * angle);
    }
    std::string message;
    const kernel::Profile profile = kernel::buildProfile(sk.sketch, plane, contours, regions, &message);
    if (profile.empty()) {
        return fail(tr("The sketch cannot be revolved: %1.").arg(QString::fromStdString(message)));
    }
    kernel::NamedShape tool = kernel::revolve(profile, axis, angle, featureKey, namingKey(sk.id), &message);
    if (tool.isNull()) {
        return fail(tr("The revolve failed: %1.").arg(QString::fromStdString(message)));
    }
    return tool;
}

namespace {

// The sketch a sketch-based feature uses, if it can be used at this point of the history.
const SketchFeature* usableSketch(PartModel& m, Feature& f, int index, const QUuid& id) {
    const SketchFeature* sk = m.sketch(id);
    const int sketchIndex = m.featureIndex(id);
    if (!sk || sketchIndex < 0) {
        setError(f, tr("Its sketch was deleted."));
        return nullptr;
    }
    if (sketchIndex > index) {
        setError(f, tr("Its sketch comes later in the history."));
        return nullptr;
    }
    const Feature& sketchFeature = m.features[static_cast<size_t>(sketchIndex)];
    if (sketchFeature.suppressed) {
        setError(f, tr("Its sketch is suppressed."));
        return nullptr;
    }
    if (sketchFeature.state == FeatureState::Error) {
        setError(f, tr("Its sketch has an error."));
        return nullptr;
    }
    return sk;
}

// Adds a boss tool to the bodies (merging into those it touches) or cuts it from them.
void applyTool(Feature& f, const kernel::NamedShape& tool, bool cut, bool merge, BuildState& state) {
    std::string error;
    if (!cut) {
        if (f.body.isNull()) {
            f.body = QUuid::createUuid();
        }
        std::vector<size_t> touching;
        if (merge) {
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
                setError(f, tr("The new solid could not be merged: %1.").arg(QString::fromStdString(error)));
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

void buildExtrude(PartModel& m, Feature& f, int index, BuildState& state) {
    const ExtrudeParams& p = f.extrude;
    const SketchFeature* sk = usableSketch(m, f, index, p.sketch);
    if (!sk) {
        return;
    }
    QString why;
    const kernel::NamedShape tool = extrudeTool(*sk, p, state, namingKey(f.id), &why);
    if (tool.isNull()) {
        setError(f, why);
        return;
    }
    applyTool(f, tool, p.cut, p.merge, state);
}

void buildRevolve(PartModel& m, Feature& f, int index, BuildState& state) {
    const RevolveParams& p = f.revolve;
    const SketchFeature* sk = usableSketch(m, f, index, p.sketch);
    if (!sk) {
        return;
    }
    QString why;
    const kernel::NamedShape tool = revolveTool(*sk, p, namingKey(f.id), &why);
    if (tool.isNull()) {
        setError(f, why);
        return;
    }
    applyTool(f, tool, p.cut, p.merge, state);
}

// Fillet and Chamfer: find the picked edges again, then treat each body that has some.
void buildEdgeFeature(Feature& f, BuildState& state) {
    const bool isFillet = f.type == FeatureType::Fillet;
    const std::vector<kernel::EdgeRef>& refs = isFillet ? f.fillet.edges : f.chamfer.edges;
    if (refs.empty()) {
        setError(f, isFillet ? tr("Pick the edges to round.") : tr("Pick the edges to bevel."));
        return;
    }
    const std::vector<const kernel::NamedShape*> shapes = shapesOf(state);
    std::map<int, std::vector<kernel::ChamferEdge>> byBody; // faces only matter for chamfers
    int missing = 0, guessed = 0;
    for (const kernel::EdgeRef& ref : refs) {
        const kernel::ResolvedEdge r = kernel::resolveEdge(shapes, ref);
        if (r.status == kernel::RefStatus::Missing) {
            ++missing;
            continue;
        }
        guessed += r.status == kernel::RefStatus::Exact ? 0 : 1;
        std::vector<kernel::ChamferEdge>& list = byBody[r.shape];
        if (std::any_of(list.begin(), list.end(), [&](const kernel::ChamferEdge& e) { return e.edge.IsSame(r.edge); })) {
            continue;
        }
        kernel::ChamferEdge e{r.edge, {}};
        if (!isFillet && f.chamfer.type == ChamferType::TwoDistances) {
            // The first distance goes on the face named first in the reference (the other one when flipped).
            const std::vector<TopoDS_Face> faces = kernel::adjacentFaces(shapes[static_cast<size_t>(r.shape)]->shape, r.edge);
            if (faces.size() == 2) {
                const std::vector<std::string> names = shapes[static_cast<size_t>(r.shape)]->namesOf(faces[1]);
                const bool secondIsFirst = std::find(names.begin(), names.end(), ref.face1) != names.end();
                e.face = (secondIsFirst != f.chamfer.flip) ? faces[1] : faces[0];
            }
        }
        list.push_back(e);
    }
    if (byBody.empty()) {
        setError(f, tr("The picked edges are gone. Edit the feature and pick them again."));
        return;
    }
    for (auto& [index, edges] : byBody) {
        BuiltBody& body = state.bodies[static_cast<size_t>(index)];
        std::string error;
        kernel::NamedShape result;
        if (isFillet) {
            std::vector<TopoDS_Edge> plain;
            for (const kernel::ChamferEdge& e : edges) {
                plain.push_back(e.edge);
            }
            result = kernel::fillet(body.shape, plain, f.fillet.radius, namingKey(f.id), &error);
        } else {
            const ChamferParams& c = f.chamfer;
            result = kernel::chamfer(body.shape, edges, c.distance, c.type == ChamferType::TwoDistances ? c.distance2 : 0.0,
                                     namingKey(f.id), &error);
        }
        if (result.isNull()) {
            setError(f, (isFillet ? tr("The fillet failed: %1.") : tr("The chamfer failed: %1."))
                            .arg(QString::fromStdString(error)));
            return;
        }
        body.shape = std::move(result);
    }
    if (missing > 0) {
        f.state = FeatureState::Warning;
        f.message = tr("%n of the picked edges are gone; the others were done. Edit the feature to pick again.", nullptr,
                       missing);
    } else if (guessed > 0) {
        f.state = FeatureState::Warning;
        f.message = tr("%n edge(s) changed upstream and were found by their shape. Check the result.", nullptr, guessed);
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
        case FeatureType::Revolve: buildRevolve(m, f, static_cast<int>(i), next); break;
        case FeatureType::Fillet:
        case FeatureType::Chamfer: buildEdgeFeature(f, next); break;
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
