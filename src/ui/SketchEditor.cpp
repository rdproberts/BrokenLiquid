#include "ui/SketchEditor.h"

#include "core/Units.h"
#include "doc/Document.h"
#include "ui/DocumentView.h"
#include "ui/Icons.h"
#include "ui/PropertyPanel.h"
#include "ui/Theme.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <V3d_View.hxx>
#include <gp_Circ.hxx>

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

namespace bl::ui {

using namespace bl::sketch;

namespace {

constexpr double kPi = std::numbers::pi;

Quantity_Color rgb(int r, int g, int b) { return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB); }

gp_Pnt2d g2(P2 p) { return gp_Pnt2d(p.x, p.y); }

TCollection_ExtendedString ext(const QString& s) {
    return TCollection_ExtendedString(reinterpret_cast<const char16_t*>(s.utf16()));
}

// Colours of sketch mode: blue under defined, black (white on dark) fully defined, red over defined.
struct Palette {
    Quantity_Color defined, under, over, selected, hover, dimension, driven, glyphText, glyphBox, preview, snap,
        inference, origin, region, problem;
};

Palette palette() {
    const bool dark = currentTheme() == ThemeKind::Dark;
    Palette p;
    p.defined = dark ? rgb(235, 238, 242) : rgb(15, 15, 18);
    p.under = dark ? rgb(96, 160, 255) : rgb(0, 72, 220);
    p.over = rgb(225, 30, 30);
    p.selected = rgb(0, 170, 150);
    p.hover = rgb(255, 140, 0);
    p.dimension = dark ? rgb(235, 238, 242) : rgb(15, 15, 18);
    p.driven = dark ? rgb(150, 155, 165) : rgb(110, 115, 125);
    p.glyphText = rgb(20, 40, 10);
    p.glyphBox = rgb(170, 220, 140);
    p.preview = dark ? rgb(120, 180, 255) : rgb(30, 100, 230);
    p.snap = rgb(255, 140, 0);
    p.inference = dark ? rgb(140, 170, 220) : rgb(90, 120, 180);
    p.origin = rgb(210, 40, 40);
    p.region = dark ? rgb(110, 150, 215) : rgb(120, 160, 225);
    p.problem = rgb(230, 40, 40);
    return p;
}

QString glyphText(ConstraintType type) {
    switch (type) {
    case ConstraintType::Horizontal: return QStringLiteral("H");
    case ConstraintType::Vertical: return QStringLiteral("V");
    case ConstraintType::Parallel: return QStringLiteral("//");
    case ConstraintType::Perpendicular: return QStringLiteral("Perp");
    case ConstraintType::Collinear: return QStringLiteral("Col");
    case ConstraintType::Equal: return QStringLiteral("=");
    case ConstraintType::Tangent: return QStringLiteral("T");
    case ConstraintType::Concentric: return QStringLiteral("Con");
    case ConstraintType::Midpoint: return QStringLiteral("Mid");
    case ConstraintType::PointOnCurve: return QStringLiteral("On");
    case ConstraintType::Symmetric: return QStringLiteral("Sym");
    case ConstraintType::Fix: return QStringLiteral("Fix");
    default: return QString();
    }
}

double wrapPi(double a) { // into (-pi, pi]
    while (a > kPi) {
        a -= 2.0 * kPi;
    }
    while (a <= -kPi) {
        a += 2.0 * kPi;
    }
    return a;
}

// Open arrowhead with its tip at `tip`, pointing along `dir`.
void arrow(render::Drawing2d& d, P2 tip, P2 dir, double size, const Quantity_Color& color) {
    const P2 u = normalized(dir);
    const P2 n = perpendicular(u);
    const P2 back = tip - u * size;
    d.strokes.push_back({{g2(back + n * (size * 0.32)), g2(tip), g2(back - n * (size * 0.32))}, color, 1.2f});
}

std::vector<gp_Pnt2d> arcPoints(P2 center, double radius, double start, double sweep, double step = 0.05) {
    const int n = std::max(2, static_cast<int>(std::ceil(std::abs(sweep) / step)));
    std::vector<gp_Pnt2d> pts;
    pts.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const double a = start + sweep * i / n;
        pts.push_back(gp_Pnt2d(center.x + radius * std::cos(a), center.y + radius * std::sin(a)));
    }
    return pts;
}

std::optional<gp_Pln> planarFaceOf(const TopoDS_Shape& shape) {
    if (shape.IsNull() || shape.ShapeType() != TopAbs_FACE) {
        return std::nullopt;
    }
    const TopoDS_Face face = TopoDS::Face(shape);
    BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) {
        return std::nullopt;
    }
    gp_Pln plane = surface.Plane();
    if (face.Orientation() == TopAbs_REVERSED) {
        gp_Ax3 pos = plane.Position();
        pos.ZReverse();
        plane.SetPosition(pos);
    }
    return plane;
}

bool isLine(const Curve* c) { return c && c->type == CurveType::Line; }
bool isRound(const Curve* c) { return c && c->type != CurveType::Line; }

} // namespace

// --- Planes ----------------------------------------------------------------------------------------

gp_Ax3 referencePlane(int which) {
    switch (which) {
    case 1: return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0), gp_Dir(1, 0, 0));  // Top: sketch Y is -Z
    case 2: return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 0, -1)); // Right: sketch X is -Z
    default: return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)); // Front
    }
}

QString referencePlaneName(int which) {
    switch (which) {
    case 1: return QObject::tr("Top Plane");
    case 2: return QObject::tr("Right Plane");
    default: return QObject::tr("Front Plane");
    }
}

gp_Ax3 facePlane(const gp_Pln& face) {
    const gp_Dir n = face.Axis().Direction();
    // Sketch origin: the model origin projected onto the face.
    const gp_Vec toOrigin(face.Location(), gp_Pnt(0, 0, 0));
    const gp_Pnt origin = gp_Pnt(0, 0, 0).Translated(-gp_Vec(n) * toOrigin.Dot(gp_Vec(n)));
    // Keep "up" as close to the model's +Y as the face allows (like the standard views).
    gp_Vec up = std::abs(n.Y()) < 0.9 ? gp_Vec(0, 1, 0) : gp_Vec(0, 0, n.Y() > 0 ? -1 : 1);
    up -= gp_Vec(n) * up.Dot(gp_Vec(n));
    const gp_Dir x = gp_Dir(up.Crossed(gp_Vec(n)));
    return gp_Ax3(origin, n, x);
}

render::Drawing2d inactiveSketchDrawing(const sketch::Sketch& s, bool dark) {
    render::Drawing2d d;
    const Quantity_Color color = dark ? rgb(185, 195, 210) : rgb(60, 66, 80);
    for (const Curve& c : s.curves()) {
        std::vector<gp_Pnt2d> pts;
        for (P2 p : tessellate(s, c, 0.05)) {
            pts.push_back(g2(p));
        }
        d.strokes.push_back({std::move(pts), color, 1.5f, c.construction ? Aspect_TOL_DASH : Aspect_TOL_SOLID});
    }
    return d;
}

// --- SketchPlaneCommand -----------------------------------------------------------------------------

SketchPlaneCommand::SketchPlaneCommand(CommandHost& host, Chosen chosen) : InteractiveCommand(host), m_chosen(std::move(chosen)) {}

void SketchPlaneCommand::start() {
    PropertyPanel& panel = m_host.panel();
    panel.setMessage(tr("Select a plane for the sketch: one of the default planes below, or a flat face in the "
                        "graphics area."));
    PanelGroup* group = panel.addGroup(tr("Plane"));
    for (int i = 0; i < 3; ++i) {
        auto* button = new QPushButton(icon(QStringLiteral("tree-plane")), referencePlaneName(i));
        connect(button, &QPushButton::clicked, this, [this, i] { choose(referencePlane(i), referencePlaneName(i)); });
        group->addRow(QString(), button);
    }
    if (DocumentView* view = m_host.activeView()) {
        view->viewer()->setSelectionFilter(true, false, false);
    }
    selectionChanged(); // a face may already be selected
}

void SketchPlaneCommand::selectionChanged() {
    DocumentView* view = m_host.activeView();
    if (!view) {
        return;
    }
    for (const render::OcctViewer::Pick& pick : view->viewer()->selection()) {
        if (const auto plane = planarFaceOf(pick.shape)) {
            const doc::Body* body = view->document()->model().body(view->bodyOf(pick.object));
            choose(facePlane(*plane), body ? tr("Face of %1").arg(body->name) : tr("Face"));
            return;
        }
    }
    if (!view->viewer()->selection().empty()) {
        m_host.showStatus(tr("Pick a flat face."));
    }
}

void SketchPlaneCommand::choose(const gp_Ax3& plane, const QString& name) {
    if (m_done) {
        return;
    }
    m_done = true;
    // Starting the sketch replaces this command; do it once this call has returned.
    QTimer::singleShot(0, [chosen = m_chosen, plane, name] { chosen(plane, name); });
}

// --- SketchEditor: life cycle -------------------------------------------------------------------------

SketchEditor::SketchEditor(CommandHost& host, doc::SketchFeature feature, bool isNew)
    : InteractiveCommand(host), m_feature(std::move(feature)), m_isNew(isNew), m_original(m_feature.sketch),
      m_plane(m_feature.plane) {}

SketchEditor::~SketchEditor() { teardown(); }

QString SketchEditor::title() const { return m_feature.name; }

render::OcctViewer* SketchEditor::viewer() const { return m_view ? m_view->viewer() : nullptr; }

void SketchEditor::start() {
    m_view = m_host.activeView();
    if (!m_view) {
        return;
    }
    render::OcctViewer* v = viewer();
    m_view->setEditingSketch(m_feature.id);
    v->setModelPickable(false);
    m_prs = new render::SketchPresentation(m_feature.plane);
    v->showOverlay(m_prs, true);
    v->setInput(this);
    v->setCursor(Qt::ArrowCursor);

    const bool hasContent = !sketch().curves().empty() || !m_view->document()->model().bodies.empty();
    v->normalTo(m_feature.plane, hasContent);
    if (!hasContent) {
        v->view()->SetSize(160.0); // an empty sketch on an empty part: about 160 mm across
        v->redraw();
    }
    m_px = v->pixelSize();
    solveAll();
    rebuildPanel();
    redraw();
    emit stateChanged();
    spdlog::info("sketch mode: {} on {}", m_feature.name.toStdString(), m_feature.planeName.toStdString());
}

void SketchEditor::teardown() {
    if (m_tornDown) {
        return;
    }
    m_tornDown = true;
    closeValueEditor(false);
    if (render::OcctViewer* v = viewer()) {
        if (v->input() == this) {
            v->setInput(nullptr);
        }
        if (!m_prs.IsNull()) {
            v->remove(m_prs);
        }
        v->setSelectionFilter(true, true, true);
        v->setModelPickable(true);
        v->unsetCursor();
    }
    if (m_view) {
        m_view->setEditingSketch({});
    }
    m_host.setStateText({});
}

bool SketchEditor::accept() {
    closeValueEditor(true);
    cancelGesture();
    solveAll();
    m_feature.status = m_solve.status;
    doc::Document* document = m_view ? m_view->document() : m_host.activeDocument();
    teardown();
    if (document) {
        const doc::SketchFeature feature = m_feature;
        const bool isNew = m_isNew;
        document->transact(isNew ? tr("New %1").arg(feature.name) : tr("Edit %1").arg(feature.name),
                           [&feature, isNew](doc::PartModel& m) {
                               if (isNew) {
                                   m.sketches.push_back(feature);
                                   ++m.sketchCount;
                                   return true;
                               }
                               if (doc::SketchFeature* f = m.sketch(feature.id)) {
                                   *f = feature;
                                   return true;
                               }
                               return false;
                           });
    }
    return true;
}

void SketchEditor::cancel() { teardown(); }

bool SketchEditor::confirmCancel() {
    const bool untouched = m_isNew ? sketch().curves().empty() && sketch().points().size() <= 1 : sketch() == m_original;
    if (untouched || m_tornDown) {
        return true;
    }
    return QMessageBox::question(m_view, tr("Exit Sketch"), tr("Discard the changes to %1?").arg(m_feature.name)) ==
           QMessageBox::Yes;
}

void SketchEditor::unitsChanged() {
    rebuildPanel();
    redraw();
}

// --- Tools and keys -------------------------------------------------------------------------------------

void SketchEditor::setTool(Tool tool) {
    closeValueEditor(true);
    cancelGesture();
    const Tool previous = m_tool;
    m_tool = tool;
    m_toolPicks.clear();
    m_mirrorAxis = 0;
    m_pickAxis = false;
    m_issueFocus = -1;
    if (tool == Tool::Offset || tool == Tool::Mirror) {
        // Entities selected beforehand are what the tool works on; for Mirror a lone centerline
        // among them is the line to mirror about.
        for (Id id : m_selected) {
            const Curve* c = sketch().curve(id);
            const Point* p = sketch().point(id);
            if (c || (tool == Tool::Mirror && p && p->owner == 0)) {
                m_toolPicks.insert(id);
            }
        }
        if (tool == Tool::Mirror) {
            std::vector<Id> centerlines;
            for (Id id : m_toolPicks) {
                if (const Curve* c = sketch().curve(id); c && c->type == CurveType::Line && c->construction) {
                    centerlines.push_back(id);
                }
            }
            if (centerlines.size() == 1 && m_toolPicks.size() > 1) {
                m_mirrorAxis = centerlines.front();
                m_toolPicks.erase(m_mirrorAxis);
            }
        }
    }
    if (tool != Tool::Select) {
        m_selected.clear();
    }
    m_hover = {};
    if (render::OcctViewer* v = viewer()) {
        v->setCursor(tool == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        if (tool == Tool::Convert && previous != Tool::Convert) {
            v->setSelectionFilter(true, true, false);
            v->setModelPickable(true);
        } else if (tool != Tool::Convert && previous == Tool::Convert) {
            v->setModelPickable(false);
            v->setSelectionFilter(true, true, true);
        }
    }
    refreshAnalysis();
    rebuildPanel();
    redraw();
    emit stateChanged();
}

void SketchEditor::cancelGesture() {
    m_clicks.clear();
    m_dimPicks.clear();
    m_chainStart = 0;
    m_chainStartTarget = 0;
    m_arcSweep = 0.0;
    m_drag = Drag::None;
    m_pressed = false;
    m_dragging = false;
}

void SketchEditor::endChain() {
    m_clicks.clear();
    m_chainStart = 0;
    m_chainStartTarget = 0;
    redraw();
}

bool SketchEditor::enterPressed() {
    if (m_valueEditor) {
        closeValueEditor(true);
    } else if (!m_clicks.empty()) {
        endChain();
    } else if (m_tool == Tool::Offset) {
        applyOffset();
    } else if (m_tool == Tool::Mirror) {
        applyMirror();
    }
    return true; // Enter never leaves the sketch; use the corner or the Sketch button
}

bool SketchEditor::escapePressed() {
    if (m_valueEditor) {
        closeValueEditor(false);
    } else if (m_tool != Tool::Select) {
        if (!m_clicks.empty() || !m_dimPicks.empty()) {
            cancelGesture();
            redraw();
        } else {
            setTool(Tool::Select);
        }
    } else if (!m_selected.empty()) {
        m_selected.clear();
        rebuildPanel();
        redraw();
    }
    return true; // Esc steps back but never discards the sketch
}

void SketchEditor::deletePressed() {
    if (m_valueEditor || m_selected.empty()) {
        return;
    }
    beginEdit();
    for (Id id : m_selected) {
        sk().remove(id);
    }
    m_selected.clear();
    m_hover = {};
    commitEdit();
}

void SketchEditor::toggleConstruction() {
    std::vector<Curve*> curves;
    for (Id id : m_selected) {
        if (Curve* c = sk().curve(id)) {
            curves.push_back(c);
        }
    }
    if (curves.empty()) {
        m_host.showStatus(tr("Select sketch curves to turn into construction geometry (or back)."));
        return;
    }
    const bool on = std::any_of(curves.begin(), curves.end(), [](const Curve* c) { return !c->construction; });
    beginEdit();
    for (Id id : m_selected) {
        if (Curve* c = sk().curve(id)) {
            c->construction = on;
        }
    }
    commitEdit();
}

void SketchEditor::showRelations(bool on) {
    m_showRelations = on;
    redraw();
}

void SketchEditor::shadeRegions(bool on) {
    m_shadeRegions = on;
    redraw();
}

void SketchEditor::setPolygonSides(int sides) {
    m_polygonSides = std::clamp(sides, 3, 64);
    redraw();
}

void SketchEditor::setFilletRadius(double mm) {
    if (mm > 0.0) {
        m_filletRadius = mm;
    }
}

void SketchEditor::setOffsetDistance(double mm, bool reverse) {
    if (mm > 0.0) {
        m_offsetDistance = mm;
    }
    m_offsetReverse = reverse;
    redraw();
}

void SketchEditor::select(const std::vector<Id>& ids) {
    m_selected = std::set<Id>(ids.begin(), ids.end());
    rebuildPanel();
    redraw();
}

void SketchEditor::undo() {
    if (m_undo.empty()) {
        return;
    }
    closeValueEditor(false);
    cancelGesture();
    m_redo.push_back(sketch());
    sk() = std::move(m_undo.back());
    m_undo.pop_back();
    m_selected.clear();
    commitEdit();
}

void SketchEditor::redo() {
    if (m_redo.empty()) {
        return;
    }
    closeValueEditor(false);
    cancelGesture();
    m_undo.push_back(sketch());
    sk() = std::move(m_redo.back());
    m_redo.pop_back();
    m_selected.clear();
    commitEdit();
}

void SketchEditor::beginEdit() {
    m_undo.push_back(sketch());
    if (m_undo.size() > 200) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
}

void SketchEditor::commitEdit() {
    solveAll();
    rebuildPanel();
    redraw();
    emit stateChanged();
}

void SketchEditor::solveAll() {
    m_solve = solve(sk());
    refreshAnalysis();
    updateStatus();
}

void SketchEditor::refreshAnalysis() {
    m_contours = findRegions(sketch());
    if (m_tool == Tool::Doctor) {
        m_issues = diagnose(sketch(), 10.0 * m_px);
        if (m_issueFocus >= static_cast<int>(m_issues.size())) {
            m_issueFocus = -1;
        }
    } else {
        m_issues.clear();
    }
}

bool SketchEditor::applyEdit(const std::function<bool(Sketch&)>& op, const QString& failure) {
    Sketch before = sketch();
    if (!op(sk())) {
        sk() = std::move(before);
        if (!failure.isEmpty()) {
            m_host.showStatus(failure, 6000);
        }
        redraw();
        return false;
    }
    m_undo.push_back(std::move(before));
    if (m_undo.size() > 200) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    commitEdit();
    return true;
}

void SketchEditor::updateStatus() {
    QString text;
    switch (m_solve.status) {
    case SketchStatus::UnderDefined: text = tr("Under Defined (%n DOF)", nullptr, m_solve.dof); break;
    case SketchStatus::FullyDefined: text = tr("Fully Defined"); break;
    case SketchStatus::OverDefined: text = tr("Over Defined"); break;
    }
    m_host.setStateText(tr("Editing %1  |  %2").arg(m_feature.name, text));
}

// --- Coordinates, picking, snapping ------------------------------------------------------------------

std::optional<P2> SketchEditor::toSketch(const QPointF& pos) const {
    render::OcctViewer* v = viewer();
    if (!v) {
        return std::nullopt;
    }
    const std::optional<gp_Pnt> p = v->pointOnPlane(pos, m_plane);
    if (!p) {
        return std::nullopt;
    }
    const gp_Vec d(m_feature.plane.Location(), *p);
    return P2{d.Dot(gp_Vec(m_feature.plane.XDirection())), d.Dot(gp_Vec(m_feature.plane.YDirection()))};
}

gp_Pnt SketchEditor::to3d(P2 p) const {
    const gp_Ax3& a = m_feature.plane;
    return a.Location().Translated(gp_Vec(a.XDirection()) * p.x + gp_Vec(a.YDirection()) * p.y);
}

SketchEditor::Hit SketchEditor::hitAt(P2 at, bool annotations) const {
    const Sketch& s = sketch();
    const double tol = 7.0 * m_px;
    Hit best;
    double bestDistance = tol;
    for (const Point& p : s.points()) {
        const double d = length(P2{p.x, p.y} - at);
        if (d < bestDistance) {
            bestDistance = d;
            best = {Hit::Point, p.id};
        }
    }
    if (best.kind != Hit::None) {
        return best;
    }
    if (annotations) {
        for (const Constraint& c : s.constraints()) {
            if (isDimension(c.type)) {
                const double halfWidth = (valueText(c).size() * 4.5 + 6.0) * m_px;
                if (std::abs(at.x - c.labelX) < halfWidth && std::abs(at.y - c.labelY) < 10.0 * m_px) {
                    return {Hit::Dimension, c.id};
                }
            }
        }
        if (m_showRelations) {
            for (const Glyph& g : layoutGlyphs()) {
                if (length(g.at - at) < 9.0 * m_px) {
                    return {Hit::Relation, g.constraint};
                }
            }
        }
    }
    bestDistance = tol;
    for (const Curve& c : s.curves()) {
        const double d = distanceToCurve(s, c, at);
        if (d < bestDistance) {
            bestDistance = d;
            best = {Hit::Curve, c.id};
        }
    }
    return best;
}

SketchEditor::Snap SketchEditor::snapAt(P2 at, const std::optional<P2>& anchor) const {
    const Sketch& s = sketch();
    const double tol = 8.0 * m_px;
    Snap snap;
    snap.at = at;

    double best = tol;
    for (const Point& p : s.points()) {
        const double d = length(P2{p.x, p.y} - at);
        if (d < best) {
            best = d;
            snap = {};
            snap.at = {p.x, p.y};
            snap.kind = Snap::Point;
            snap.target = p.id;
        }
    }
    if (snap.kind == Snap::Point) {
        return snap;
    }
    best = tol;
    for (const Curve& c : s.curves()) {
        if (c.type == CurveType::Line) {
            const P2 m = midpointOf(s, c);
            if (const double d = length(m - at); d < best) {
                best = d;
                snap.at = m;
                snap.kind = Snap::Midpoint;
                snap.target = c.id;
            }
        }
    }
    if (snap.kind == Snap::Midpoint) {
        return snap;
    }
    best = tol;
    for (const Curve& c : s.curves()) {
        const P2 q = closestOnCurve(s, c, at);
        if (const double d = length(q - at); d < best) {
            best = d;
            snap.at = q;
            snap.kind = Snap::OnCurve;
            snap.target = c.id;
        }
    }
    if (snap.kind == Snap::OnCurve) {
        return snap;
    }

    // Tangent inference: a line that leaves the end of an arc along the arc's direction.
    if (anchor && !m_clicks.empty() && m_clicks.back().kind == Snap::Point &&
        (m_tool == Tool::Line || m_tool == Tool::Centerline)) {
        const Point* p = s.point(m_clicks.back().target);
        const Curve* owner = p ? s.curve(p->owner) : nullptr;
        if (owner && owner->type == CurveType::Arc) {
            if (const std::optional<P2> dir = exitDirection(s, *owner, p->id)) {
                const double along = dot(at - *anchor, *dir);
                const P2 foot = *anchor + *dir * along;
                if (along > tol && length(at - foot) < tol) {
                    snap.at = foot;
                    snap.tangent = owner->id;
                    snap.horizontal = std::abs(dir->y) < 1e-9;
                    snap.vertical = std::abs(dir->x) < 1e-9;
                    return snap;
                }
            }
        }
    }
    if (anchor) {
        if (std::abs(at.y - anchor->y) < tol) {
            snap.at.y = anchor->y;
            snap.horizontal = true;
        } else if (std::abs(at.x - anchor->x) < tol) {
            snap.at.x = anchor->x;
            snap.vertical = true;
        }
    }
    double bestX = tol, bestY = tol;
    for (const Point& p : s.points()) {
        if (!snap.vertical && std::abs(at.x - p.x) < bestX) {
            bestX = std::abs(at.x - p.x);
            snap.alignX = P2{p.x, p.y};
        }
        if (!snap.horizontal && std::abs(at.y - p.y) < bestY) {
            bestY = std::abs(at.y - p.y);
            snap.alignY = P2{p.x, p.y};
        }
    }
    if (snap.alignX) {
        snap.at.x = snap.alignX->x;
    }
    if (snap.alignY) {
        snap.at.y = snap.alignY->y;
    }
    return snap;
}

void SketchEditor::applySnap(Id point, const Snap& snap) {
    switch (snap.kind) {
    case Snap::Point:
        if (snap.target != point) {
            sk().addRelation(ConstraintType::Coincident, {snap.target, point});
        }
        break;
    case Snap::Midpoint: sk().addRelation(ConstraintType::Midpoint, {point, snap.target}); break;
    case Snap::OnCurve: sk().addRelation(ConstraintType::PointOnCurve, {point, snap.target}); break;
    case Snap::Free: break;
    }
}

// --- Drawing tools ------------------------------------------------------------------------------------

void SketchEditor::clickAt(double u, double v) {
    if (render::OcctViewer* view = viewer()) {
        m_px = view->pixelSize();
    }
    const P2 at{u, v};
    if (m_tool == Tool::Select) {
        const Hit h = hitAt(at, true);
        m_selected.clear();
        if (h.kind != Hit::None) {
            m_selected.insert(h.id);
        }
        rebuildPanel();
        redraw();
        return;
    }
    if (m_tool == Tool::Dimension) {
        dimensionClick(at);
        return;
    }
    if (isPickTool()) {
        pickClick(at);
        return;
    }
    if (m_tool == Tool::Convert || m_tool == Tool::Doctor) {
        return;
    }
    const bool anchored = ((m_tool == Tool::Line || m_tool == Tool::Centerline) && !m_clicks.empty()) ||
                          (m_tool == Tool::Slot && m_clicks.size() == 1);
    const std::optional<P2> anchor = anchored ? std::optional<P2>(m_clicks.back().at) : std::nullopt;
    m_cursor = snapAt(at, anchor);
    m_haveCursor = true;
    toolClick(m_cursor);
}

void SketchEditor::toolClick(const Snap& snap) {
    Sketch& s = sk();
    const double tiny = 1e-6;
    switch (m_tool) {
    case Tool::Line:
    case Tool::Centerline: {
        if (m_clicks.empty()) {
            m_clicks = {snap};
            m_chainStartTarget = snap.kind == Snap::Point ? snap.target : 0;
            redraw();
            return;
        }
        const Snap start = m_clicks.back();
        if (length(snap.at - start.at) < tiny || (snap.kind == Snap::Point && snap.target == start.target && start.kind == Snap::Point)) {
            return;
        }
        beginEdit();
        const Id line = s.addLine(start.at.x, start.at.y, snap.at.x, snap.at.y);
        s.curve(line)->construction = m_tool == Tool::Centerline;
        const Id p1 = s.curve(line)->p1, p2 = s.curve(line)->p2;
        applySnap(p1, start);
        applySnap(p2, snap);
        // Automatic relations, as the inference showed them. A segment that ends on an existing
        // point (closing a profile) still gets horizontal/vertical when it is exactly so. Each one
        // is dropped again if it would over-define the sketch.
        auto keepIfConsistent = [&s](Id relation) {
            if (!relation) {
                return;
            }
            Sketch trial = s;
            const SolveResult r = solve(trial);
            if (!r.converged || !r.conflicting.empty() || !r.redundant.empty()) {
                s.remove(relation);
            }
        };
        const P2 delta = snap.at - start.at;
        const double flat = 1e-9 * std::max(1.0, length(delta));
        if (snap.horizontal || std::abs(delta.y) <= flat) {
            keepIfConsistent(s.addRelation(ConstraintType::Horizontal, {line}));
        } else if (snap.vertical || std::abs(delta.x) <= flat) {
            keepIfConsistent(s.addRelation(ConstraintType::Vertical, {line}));
        }
        if (snap.tangent && s.curve(snap.tangent)) {
            keepIfConsistent(s.addRelation(ConstraintType::Tangent, {line, snap.tangent}));
        }
        if (!m_chainStart) {
            m_chainStart = p1;
        }
        const bool closes = snap.kind == Snap::Point && (snap.target == m_chainStart || (m_chainStartTarget && snap.target == m_chainStartTarget));
        Snap next;
        next.at = snap.at;
        next.kind = Snap::Point;
        next.target = p2;
        commitEdit();
        if (closes) {
            endChain();
        } else {
            m_clicks = {next};
        }
        return;
    }
    case Tool::Rectangle:
    case Tool::CenterRectangle: {
        if (m_clicks.empty()) {
            m_clicks = {snap};
            redraw();
            return;
        }
        const Snap first = m_clicks.front();
        const P2 d = snap.at - first.at;
        if (std::abs(d.x) < tiny || std::abs(d.y) < tiny) {
            return;
        }
        beginEdit();
        if (m_tool == Tool::Rectangle) {
            const auto lines = addRectangle(s, first.at, snap.at);
            applySnap(s.curve(lines[0])->p1, first);
            applySnap(s.curve(lines[2])->p1, snap);
        } else {
            const auto ids = addCenterRectangle(s, first.at, snap.at);
            applySnap(ids[6], first);
            applySnap(s.curve(ids[2])->p1, snap);
        }
        m_clicks.clear();
        commitEdit();
        return;
    }
    case Tool::Circle: {
        if (m_clicks.empty()) {
            m_clicks = {snap};
            redraw();
            return;
        }
        const Snap center = m_clicks.front();
        const double r = length(snap.at - center.at);
        if (r < tiny) {
            return;
        }
        beginEdit();
        const Id circle = s.addCircle(center.at.x, center.at.y, r);
        applySnap(s.curve(circle)->center, center);
        if (snap.kind == Snap::Point) {
            s.addRelation(ConstraintType::PointOnCurve, {snap.target, circle});
        }
        m_clicks.clear();
        commitEdit();
        return;
    }
    case Tool::Arc: {
        if (m_clicks.size() < 2) {
            if (m_clicks.size() == 1 && length(snap.at - m_clicks[0].at) < tiny) {
                return;
            }
            m_clicks.push_back(snap);
            if (m_clicks.size() == 2) {
                const P2 d = snap.at - m_clicks[0].at;
                m_arcLast = std::atan2(d.y, d.x);
                m_arcSweep = 0.0;
            }
            redraw();
            return;
        }
        const Snap center = m_clicks[0], start = m_clicks[1];
        if (std::abs(m_arcSweep) < 1e-3) {
            return;
        }
        beginEdit();
        const bool ccw = m_arcSweep > 0.0;
        const P2 a = ccw ? start.at : snap.at, b = ccw ? snap.at : start.at;
        const Id arc = s.addArc(center.at.x, center.at.y, a.x, a.y, b.x, b.y);
        const Curve* k = s.curve(arc);
        const Id startPoint = ccw ? k->p1 : k->p2, endPoint = ccw ? k->p2 : k->p1;
        applySnap(k->center, center);
        applySnap(startPoint, start);
        applySnap(endPoint, snap);
        m_clicks.clear();
        commitEdit();
        return;
    }
    case Tool::TangentArc: {
        if (m_clicks.empty()) {
            const Point* p = snap.kind == Snap::Point ? s.point(snap.target) : nullptr;
            const Curve* owner = p ? s.curve(p->owner) : nullptr;
            if (!owner || !exitDirection(s, *owner, p->id)) {
                m_host.showStatus(tr("Start a tangent arc on the end of a line or an arc."));
                return;
            }
            m_clicks = {snap};
            redraw();
            return;
        }
        const Snap from = m_clicks.front();
        const Point* p = s.point(from.target);
        const Curve* owner = p ? s.curve(p->owner) : nullptr;
        const std::optional<P2> dir = owner ? exitDirection(s, *owner, p->id) : std::nullopt;
        const std::optional<TangentArc> fit = dir ? tangentArc(from.at, *dir, snap.at) : std::nullopt;
        if (!fit) {
            return;
        }
        beginEdit();
        const Id ownerId = owner->id;
        const P2 a = fit->ccw ? from.at : snap.at, b = fit->ccw ? snap.at : from.at;
        const Id arc = s.addArc(fit->center.x, fit->center.y, a.x, a.y, b.x, b.y);
        const Curve* k = s.curve(arc);
        const Id atStart = fit->ccw ? k->p1 : k->p2, atEnd = fit->ccw ? k->p2 : k->p1;
        s.addRelation(ConstraintType::Coincident, {from.target, atStart});
        s.addRelation(ConstraintType::Tangent, {ownerId, arc});
        applySnap(atEnd, snap);
        m_clicks.clear();
        commitEdit();
        return;
    }
    case Tool::Slot: {
        if (m_clicks.size() < 2) {
            if (m_clicks.size() == 1 && length(snap.at - m_clicks[0].at) < tiny) {
                return;
            }
            m_clicks.push_back(snap);
            redraw();
            return;
        }
        const Snap a = m_clicks[0], b = m_clicks[1];
        const double half = std::abs(cross(normalized(b.at - a.at), snap.at - a.at));
        if (half < tiny) {
            return;
        }
        beginEdit();
        const auto ids = addSlot(s, a.at, b.at, half);
        const Curve axis = *s.curve(ids[4]);
        applySnap(axis.p1, a);
        applySnap(axis.p2, b);
        if (b.kind == Snap::Free && b.horizontal) {
            s.addRelation(ConstraintType::Horizontal, {ids[4]});
        } else if (b.kind == Snap::Free && b.vertical) {
            s.addRelation(ConstraintType::Vertical, {ids[4]});
        }
        m_clicks.clear();
        commitEdit();
        return;
    }
    case Tool::Polygon: {
        if (m_clicks.empty()) {
            m_clicks = {snap};
            redraw();
            return;
        }
        const Snap center = m_clicks[0];
        if (length(snap.at - center.at) < tiny) {
            return;
        }
        beginEdit();
        const std::vector<Id> ids = addPolygon(s, center.at, snap.at, m_polygonSides);
        applySnap(s.curve(ids.back())->center, center);
        if (snap.kind != Snap::Free) {
            applySnap(s.curve(ids.front())->p1, snap);
        }
        m_clicks.clear();
        commitEdit();
        return;
    }
    case Tool::Point: {
        beginEdit();
        const Id point = s.addPoint(snap.at.x, snap.at.y);
        if (snap.kind != Snap::Point) {
            applySnap(point, snap);
        } else {
            s.addRelation(ConstraintType::Coincident, {snap.target, point});
        }
        commitEdit();
        return;
    }
    default: return;
    }
}

// --- Fillet, trim, extend, offset, mirror, convert ---------------------------------------------------

bool SketchEditor::isPickTool() const {
    return m_tool == Tool::Fillet || m_tool == Tool::Trim || m_tool == Tool::Extend || m_tool == Tool::Offset ||
           m_tool == Tool::Mirror;
}

std::vector<Id> SketchEditor::chainOf(Id curve) const {
    // Curves joined end to end with `curve` (by position), as Offset picks a whole contour.
    const Sketch& s = sketch();
    const double tol = 1e-6 * std::max(1.0, m_px * 1000.0);
    std::vector<Id> chain{curve};
    for (size_t i = 0; i < chain.size(); ++i) {
        const Curve* c = s.curve(chain[i]);
        if (!c || c->type == CurveType::Circle) {
            continue;
        }
        for (Id end : {c->p1, c->p2}) {
            const P2 at = pointOf(s, end);
            for (const Curve& o : s.curves()) {
                if (o.type == CurveType::Circle || o.construction != c->construction ||
                    std::find(chain.begin(), chain.end(), o.id) != chain.end()) {
                    continue;
                }
                if (length(pointOf(s, o.p1) - at) < tol || length(pointOf(s, o.p2) - at) < tol) {
                    chain.push_back(o.id);
                }
            }
        }
    }
    return chain;
}

void SketchEditor::pickClick(P2 at) {
    const Hit h = hitAt(at, false);
    const Sketch& s = sketch();
    switch (m_tool) {
    case Tool::Fillet: {
        Id corner = 0;
        if (h.kind == Hit::Point) {
            corner = h.id;
        } else if (const Curve* c = h.kind == Hit::Curve ? s.curve(h.id) : nullptr; c && c->type == CurveType::Line) {
            corner = length(pointOf(s, c->p1) - at) <= length(pointOf(s, c->p2) - at) ? c->p1 : c->p2;
        }
        if (!corner) {
            m_host.showStatus(tr("Click the corner where two lines meet."));
            return;
        }
        const double radius = m_filletRadius;
        applyEdit([corner, radius](Sketch& k) { return filletCorner(k, corner, radius).has_value(); },
                  tr("No fillet there: pick a corner of two lines long enough for the radius."));
        return;
    }
    case Tool::Trim:
    case Tool::Extend: {
        if (h.kind != Hit::Curve) {
            return;
        }
        const Id id = h.id;
        if (m_tool == Tool::Trim) {
            applyEdit([id, at](Sketch& k) { return trim(k, id, at); }, QString());
        } else {
            applyEdit([id, at](Sketch& k) { return extend(k, id, at); }, tr("Nothing to extend to in that direction."));
        }
        m_hover = {};
        redraw();
        return;
    }
    case Tool::Offset:
    case Tool::Mirror: {
        const bool curve = h.kind == Hit::Curve;
        const bool point = h.kind == Hit::Point && s.point(h.id) && s.point(h.id)->owner == 0 && h.id != s.origin();
        if (!curve && !(point && m_tool == Tool::Mirror)) {
            return;
        }
        if (m_tool == Tool::Mirror && curve) {
            const Curve* c = s.curve(h.id);
            const bool line = c && c->type == CurveType::Line;
            if (line && (m_pickAxis || (!m_mirrorAxis && c->construction))) {
                m_mirrorAxis = h.id;
                m_toolPicks.erase(h.id);
                m_pickAxis = false;
                rebuildPanel();
                redraw();
                return;
            }
            if (h.id == m_mirrorAxis) {
                m_mirrorAxis = 0;
                rebuildPanel();
                redraw();
                return;
            }
        }
        std::vector<Id> ids{h.id};
        if (m_tool == Tool::Offset && m_offsetChain) {
            ids = chainOf(h.id);
        }
        const bool remove = m_toolPicks.count(h.id) > 0;
        for (Id id : ids) {
            if (remove) {
                m_toolPicks.erase(id);
            } else {
                m_toolPicks.insert(id);
            }
        }
        rebuildPanel();
        redraw();
        return;
    }
    default: return;
    }
}

bool SketchEditor::applyOffset() {
    if (m_toolPicks.empty()) {
        m_host.showStatus(tr("Click the entities to offset first."));
        return false;
    }
    const std::vector<Id> picks(m_toolPicks.begin(), m_toolPicks.end());
    const double distance = signedOffset();
    const bool dims = m_offsetDimensions;
    const bool ok = applyEdit([&](Sketch& k) { return !offset(k, picks, distance, dims).empty(); },
                              tr("The offset collapses at that distance; try a smaller one or reverse it."));
    if (ok) {
        m_toolPicks.clear();
        rebuildPanel();
        redraw();
    }
    return ok;
}

bool SketchEditor::applyMirror() {
    if (!m_mirrorAxis || m_toolPicks.empty()) {
        m_host.showStatus(tr("Pick the entities to mirror and the line to mirror about."));
        return false;
    }
    const std::vector<Id> picks(m_toolPicks.begin(), m_toolPicks.end());
    const Id axis = m_mirrorAxis;
    const bool ok = applyEdit([&](Sketch& k) { return !mirror(k, picks, axis).empty(); }, tr("Nothing to mirror."));
    if (ok) {
        m_toolPicks.clear();
        rebuildPanel();
        redraw();
    }
    return ok;
}

int SketchEditor::convertEdge(const TopoDS_Edge& edge) {
    Sketch& s = sk();
    const gp_Ax3& a = m_feature.plane;
    auto flat = [&a](const gp_Pnt& p) {
        const gp_Vec d(a.Location(), p);
        return P2{d.Dot(gp_Vec(a.XDirection())), d.Dot(gp_Vec(a.YDirection()))};
    };
    auto fix = [&s](Id id) { s.addRelation(ConstraintType::Fix, {id}); };
    BRepAdaptor_Curve c(edge);
    const double f = c.FirstParameter(), l = c.LastParameter();
    if (c.GetType() == GeomAbs_Line) {
        const P2 p = flat(c.Value(f)), q = flat(c.Value(l));
        if (length(q - p) < 1e-9) {
            return 0;
        }
        fix(s.addLine(p.x, p.y, q.x, q.y));
        return 1;
    }
    if (c.GetType() == GeomAbs_Circle && c.Circle().Axis().Direction().IsParallel(a.Direction(), 1e-6)) {
        const gp_Circ circle = c.Circle();
        const P2 center = flat(circle.Location());
        if (std::abs(l - f - 2.0 * kPi) < 1e-9 || BRep_Tool::IsClosed(edge)) {
            fix(s.addCircle(center.x, center.y, circle.Radius()));
            return 1;
        }
        const P2 p = flat(c.Value(f)), q = flat(c.Value(l)), m = flat(c.Value(0.5 * (f + l)));
        auto angle = [&center](P2 x) { return std::atan2(x.y - center.y, x.x - center.x); };
        auto wrap = [](double x) { return std::fmod(std::fmod(x, 2.0 * kPi) + 2.0 * kPi, 2.0 * kPi); };
        const bool ccw = wrap(angle(m) - angle(p)) < wrap(angle(q) - angle(p)); // p to q the short way round passes m
        fix(ccw ? s.addArc(center.x, center.y, p.x, p.y, q.x, q.y) : s.addArc(center.x, center.y, q.x, q.y, p.x, p.y));
        return 1;
    }
    // Anything else (splines, ellipses, circles seen at an angle): a polyline close to it.
    GCPnts_QuasiUniformDeflection points(c, 0.02 * std::max(1.0, m_px * 20.0));
    if (!points.IsDone() || points.NbPoints() < 2) {
        return 0;
    }
    int added = 0;
    P2 previous = flat(points.Value(1));
    for (int i = 2; i <= points.NbPoints(); ++i) {
        const P2 next = flat(points.Value(i));
        if (length(next - previous) > 1e-9) {
            fix(s.addLine(previous.x, previous.y, next.x, next.y));
            ++added;
            previous = next;
        }
    }
    return added;
}

void SketchEditor::convertShape(const TopoDS_Shape& shape) {
    if (shape.IsNull()) {
        return;
    }
    Sketch before = sketch();
    int added = 0;
    if (shape.ShapeType() == TopAbs_EDGE) {
        added = convertEdge(TopoDS::Edge(shape));
    } else {
        for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
            added += convertEdge(TopoDS::Edge(ex.Current()));
        }
    }
    if (added == 0) {
        m_host.showStatus(tr("Nothing to convert there: pick a model edge or face."));
        return;
    }
    m_undo.push_back(std::move(before));
    m_redo.clear();
    m_host.showStatus(tr("Converted %n entity(s); they follow the model and are held fixed.", nullptr, added));
    commitEdit();
}

void SketchEditor::repairAll() {
    const std::vector<Issue> issues = m_issues;
    applyEdit(
        [&issues](Sketch& k) {
            bool any = false;
            for (const Issue& i : issues) {
                if (i.repairable) {
                    any = repair(k, i) || any;
                }
            }
            return any;
        },
        tr("Nothing the Sketch Doctor can repair by itself."));
}

QString SketchEditor::issueText(const Issue& issue) const {
    QStringList names;
    for (Id id : issue.ids) {
        names << QString::fromStdString(sketch().name(id));
    }
    QString text = tr(issueName(issue.kind));
    if (!names.isEmpty()) {
        text += QStringLiteral(": ") + names.join(QStringLiteral(", "));
    }
    return text;
}

// --- Smart Dimension ------------------------------------------------------------------------------------

void SketchEditor::dimensionClick(P2 at) {
    const Hit h = hitAt(at, false);
    if (h.kind == Hit::Point || h.kind == Hit::Curve) {
        if (!m_dimPicks.empty() && m_dimPicks.back() == h) {
            return;
        }
        if (m_dimPicks.size() >= 2) {
            m_dimPicks.clear();
        }
        m_dimPicks.push_back(h);
        if (m_dimPicks.size() == 2 && !pendingDimension(at)) {
            m_dimPicks = {h}; // not a pair we can dimension: start again from this one
        }
        redraw();
        return;
    }
    if (const std::optional<Constraint> c = pendingDimension(at)) {
        m_dimPicks.clear();
        placeDimension(*c);
    }
}

std::optional<Constraint> SketchEditor::pendingDimension(P2 label) const {
    const Sketch& s = sketch();
    if (m_dimPicks.empty()) {
        return std::nullopt;
    }
    Constraint c;
    c.labelX = label.x;
    c.labelY = label.y;
    // A circle or arc stands for its centre in distances.
    auto pointish = [&s](const Hit& h) -> Id {
        if (h.kind == Hit::Point) {
            return h.id;
        }
        const Curve* k = s.curve(h.id);
        return isRound(k) ? k->center : 0;
    };
    auto lineOf = [&s](const Hit& h) -> const Curve* {
        const Curve* k = h.kind == Hit::Curve ? s.curve(h.id) : nullptr;
        return isLine(k) ? k : nullptr;
    };

    if (m_dimPicks.size() == 1) {
        const Hit& h = m_dimPicks[0];
        const Curve* k = h.kind == Hit::Curve ? s.curve(h.id) : nullptr;
        if (!k) {
            return std::nullopt;
        }
        c.refs = {k->id};
        c.type = k->type == CurveType::Line ? ConstraintType::Distance
                 : k->type == CurveType::Circle ? ConstraintType::Diameter
                                                : ConstraintType::Radius;
    } else {
        const Hit& a = m_dimPicks[0];
        const Hit& b = m_dimPicks[1];
        const Curve* la = lineOf(a);
        const Curve* lb = lineOf(b);
        if (la && lb) {
            if (parallel(s, *la, *lb, 1e-9)) {
                c.type = ConstraintType::Distance;
                c.refs = {la->id, lb->id};
            } else {
                const std::optional<P2> v = lineIntersection(s, *la, *lb);
                if (!v) {
                    return std::nullopt;
                }
                const P2 da0 = normalized(pointOf(s, la->p2) - pointOf(s, la->p1));
                const P2 db0 = normalized(pointOf(s, lb->p2) - pointOf(s, lb->p1));
                const int flips = (dot(label - *v, da0) < 0 ? 1 : 0) + (dot(label - *v, db0) < 0 ? 1 : 0);
                c.type = ConstraintType::Angle;
                c.refs = {la->id, lb->id};
                c.variant = flips == 1 ? 1 : 0;
            }
        } else if (la || lb) {
            const Id p = pointish(la ? b : a);
            if (!p) {
                return std::nullopt;
            }
            c.type = ConstraintType::Distance;
            c.refs = {p, (la ? la : lb)->id};
        } else {
            const Id p = pointish(a), q = pointish(b);
            if (!p || !q || p == q) {
                return std::nullopt;
            }
            const P2 pa = pointOf(s, p), pb = pointOf(s, q);
            const bool insideX = label.x >= std::min(pa.x, pb.x) && label.x <= std::max(pa.x, pb.x);
            const bool insideY = label.y >= std::min(pa.y, pb.y) && label.y <= std::max(pa.y, pb.y);
            c.type = insideX && !insideY   ? ConstraintType::HorizontalDistance
                     : insideY && !insideX ? ConstraintType::VerticalDistance
                                           : ConstraintType::Distance;
            c.refs = {p, q};
        }
    }
    const std::optional<double> value = s.measure(c);
    if (!value) {
        return std::nullopt;
    }
    c.value = *value;
    return c;
}

void SketchEditor::placeDimension(Constraint c) {
    beginEdit();
    const Id id = sk().addConstraint(std::move(c));
    m_solve = solve(sk());
    if (!m_solve.converged || !m_solve.redundant.empty()) {
        sk().constraint(id)->driving = false;
        m_host.showStatus(tr("That dimension would over-define the sketch, so it was added as a driven (reference) "
                             "dimension."),
                          6000);
    }
    commitEdit();
    if (sk().constraint(id)->driving) {
        openValueEditor(id);
    }
}

bool SketchEditor::applyValue(Id dimension, double value) {
    Constraint* c = sk().constraint(dimension);
    if (!c) {
        return false;
    }
    if (std::abs(c->value - value) < 1e-12) {
        return true;
    }
    beginEdit();
    sk().constraint(dimension)->value = value;
    const SolveResult r = solve(sk());
    if (!r.converged) {
        sk() = std::move(m_undo.back());
        m_undo.pop_back();
        solveAll();
        rebuildPanel();
        redraw();
        m_host.showStatus(tr("%1 cannot take that value: it conflicts with the other dimensions and relations.")
                              .arg(QString::fromStdString(sketch().name(dimension))),
                          6000);
        return false;
    }
    commitEdit();
    return true;
}

void SketchEditor::setDimensionValue(Id dimension, double value) { applyValue(dimension, value); }

void SketchEditor::openValueEditor(Id dimension) {
    closeValueEditor(false);
    const Constraint* c = sketch().constraint(dimension);
    render::OcctViewer* v = viewer();
    if (!c || !c->driving || !v || !m_view) {
        return;
    }
    const Units units = m_host.units();
    auto* editor = new QLineEdit(m_view);
    editor->setObjectName(QStringLiteral("SketchValueEditor"));
    editor->setText(c->type == ConstraintType::Angle
                        ? QString::number(c->value * 180.0 / kPi, 'f', units.angleDecimals)
                        : QString::fromStdString(units::formatLength(c->value, units, false)));
    editor->setToolTip(tr("Type a value (units and + - * / allowed), Enter to apply, Esc to keep the old value."));
    const QPointF at = v->toWidget(to3d({c->labelX, c->labelY}));
    const QPoint inView = v->mapTo(m_view.data(), at.toPoint());
    editor->resize(104, 26);
    editor->move(inView - QPoint(52, 40));
    editor->show();
    editor->selectAll();
    editor->setFocus(Qt::OtherFocusReason);
    m_valueEditor = editor;
    m_valueDimension = dimension;
    connect(editor, &QLineEdit::editingFinished, this, [this] { closeValueEditor(true); });
}

void SketchEditor::closeValueEditor(bool apply) {
    if (!m_valueEditor) {
        return;
    }
    QLineEdit* editor = m_valueEditor;
    m_valueEditor = nullptr;
    const QString text = editor->text();
    const Id dimension = m_valueDimension;
    m_valueDimension = 0;
    editor->hide();
    editor->deleteLater();
    if (render::OcctViewer* v = viewer()) {
        v->setFocus();
    }
    const Constraint* c = sketch().constraint(dimension);
    if (!apply || !c) {
        return;
    }
    const std::optional<double> value = c->type == ConstraintType::Angle
                                            ? units::parseAngle(text.toStdString())
                                            : units::parseLength(text.toStdString(), m_host.units());
    if (!value || *value < 0.0 || (*value == 0.0 && c->type != ConstraintType::Angle)) {
        m_host.showStatus(tr("\"%1\" is not a valid value.").arg(text));
        return;
    }
    applyValue(dimension, *value);
}

// --- Relations ------------------------------------------------------------------------------------------

std::vector<ConstraintType> SketchEditor::applicableRelations() const {
    const Sketch& s = sketch();
    int points = 0, lines = 0, rounds = 0;
    for (Id id : m_selected) {
        if (s.point(id)) {
            ++points;
        } else if (const Curve* c = s.curve(id)) {
            ++(c->type == CurveType::Line ? lines : rounds);
        } else {
            return {}; // a relation or dimension is selected
        }
    }
    using T = ConstraintType;
    std::vector<T> out;
    if (points == 0 && rounds == 0 && lines >= 1) {
        out = {T::Horizontal, T::Vertical};
        if (lines == 2) {
            out.insert(out.end(), {T::Parallel, T::Perpendicular, T::Collinear, T::Equal});
        } else if (lines > 2) {
            out.insert(out.end(), {T::Parallel, T::Equal});
        }
    } else if (points == 0 && lines == 0 && rounds >= 2) {
        out = {T::Equal};
        if (rounds == 2) {
            out.insert(out.end(), {T::Concentric, T::Tangent});
        }
    } else if (points == 0 && lines == 1 && rounds == 1) {
        out = {T::Tangent};
    } else if (points == 2 && lines == 0 && rounds == 0) {
        out = {T::Coincident, T::Horizontal, T::Vertical};
    } else if (points == 1 && lines == 1 && rounds == 0) {
        out = {T::Midpoint, T::PointOnCurve};
    } else if (points == 1 && rounds == 1 && lines == 0) {
        out = {T::PointOnCurve};
    } else if (points == 2 && lines == 1 && rounds == 0) {
        out = {T::Symmetric};
    }
    if (points + lines + rounds >= 1) {
        out.push_back(T::Fix);
    }
    // Leave out relations the selection already has (same type over exactly these entities).
    std::vector<Id> chosen(m_selected.begin(), m_selected.end());
    std::sort(chosen.begin(), chosen.end());
    std::erase_if(out, [&](T type) {
        return std::any_of(s.constraints().begin(), s.constraints().end(), [&](const Constraint& c) {
            if (c.type != type) {
                return false;
            }
            std::vector<Id> refs = c.refs;
            std::sort(refs.begin(), refs.end());
            return refs == chosen;
        });
    });
    return out;
}

void SketchEditor::addRelation(ConstraintType type) {
    const Sketch& s = sketch();
    std::vector<Id> points, lines, rounds, all;
    for (Id id : m_selected) {
        all.push_back(id);
        if (s.point(id)) {
            points.push_back(id);
        } else if (const Curve* c = s.curve(id)) {
            (c->type == CurveType::Line ? lines : rounds).push_back(id);
        }
    }
    std::vector<std::vector<Id>> sets;
    using T = ConstraintType;
    switch (type) {
    case T::Horizontal:
    case T::Vertical:
        if (!lines.empty()) {
            for (Id l : lines) {
                sets.push_back({l});
            }
        } else if (points.size() == 2) {
            sets.push_back(points);
        }
        break;
    case T::Fix:
        for (Id id : all) {
            sets.push_back({id});
        }
        break;
    case T::Coincident: sets.push_back(points); break;
    case T::Parallel:
    case T::Equal:
        if (lines.size() >= 2) {
            for (size_t i = 1; i < lines.size(); ++i) {
                sets.push_back({lines[0], lines[i]});
            }
        } else {
            for (size_t i = 1; i < rounds.size(); ++i) {
                sets.push_back({rounds[0], rounds[i]});
            }
        }
        break;
    case T::Perpendicular:
    case T::Collinear: sets.push_back(lines); break;
    case T::Concentric: sets.push_back(rounds); break;
    case T::Tangent:
        if (!lines.empty() && !rounds.empty()) {
            sets.push_back({lines[0], rounds[0]});
        } else {
            sets.push_back(rounds);
        }
        break;
    case T::Midpoint:
        if (!points.empty() && !lines.empty()) {
            sets.push_back({points[0], lines[0]});
        }
        break;
    case T::PointOnCurve:
        if (!points.empty() && (!lines.empty() || !rounds.empty())) {
            sets.push_back({points[0], lines.empty() ? rounds[0] : lines[0]});
        }
        break;
    case T::Symmetric:
        if (points.size() >= 2 && !lines.empty()) {
            sets.push_back({points[0], points[1], lines[0]});
        }
        break;
    default: return;
    }
    beginEdit();
    int added = 0;
    for (const std::vector<Id>& refs : sets) {
        added += sk().addRelation(type, refs) ? 1 : 0;
    }
    const SolveResult r = solve(sk());
    if (added == 0 || !r.converged || !r.redundant.empty()) {
        sk() = std::move(m_undo.back());
        m_undo.pop_back();
        solveAll();
        redraw();
        m_host.showStatus(added == 0      ? tr("%1 does not suit the selection.").arg(tr(typeName(type)))
                          : !r.converged  ? tr("%1 conflicts with the existing relations and dimensions.").arg(tr(typeName(type)))
                                          : tr("%1 is already implied by the existing relations.").arg(tr(typeName(type))),
                          6000);
        return;
    }
    commitEdit();
}

// --- Select tool: dragging --------------------------------------------------------------------------------

void SketchEditor::startDrag() {
    m_beforeDrag = sketch();
    m_dragPoints.clear();
    m_drag = Drag::None;
    const Sketch& s = sketch();
    auto addPoint = [this, &s](Id id) {
        if (const Point* p = s.point(id); p && !p->fixed) {
            m_dragPoints.push_back({id, {p->x, p->y}});
        }
    };
    switch (m_pressHit.kind) {
    case Hit::Point: addPoint(m_pressHit.id); break;
    case Hit::Curve:
        if (const Curve* c = s.curve(m_pressHit.id)) {
            if (c->type == CurveType::Circle) {
                m_drag = Drag::Radius;
                m_dragCurve = c->id;
                return;
            }
            for (Id p : {c->p1, c->p2, c->center}) {
                if (p) {
                    addPoint(p);
                }
            }
        }
        break;
    case Hit::Dimension:
        m_drag = Drag::Label;
        m_dragCurve = m_pressHit.id;
        return;
    case Hit::Relation: return;
    case Hit::None:
        m_drag = Drag::Box;
        m_boxEnd = m_pressAt;
        return;
    }
    if (!m_dragPoints.empty()) {
        m_drag = Drag::Points;
    }
}

void SketchEditor::dragTo(P2 at) {
    switch (m_drag) {
    case Drag::Points: {
        const P2 delta = at - m_pressAt;
        SolveOptions o;
        o.analyse = false;
        o.maxIterations = 30;
        o.dragPoint = m_dragPoints[0].first;
        o.dragX = m_dragPoints[0].second.x + delta.x;
        o.dragY = m_dragPoints[0].second.y + delta.y;
        for (size_t i = 1; i < m_dragPoints.size(); ++i) {
            o.moreDrags.push_back({m_dragPoints[i].first, m_dragPoints[i].second.x + delta.x, m_dragPoints[i].second.y + delta.y});
        }
        solve(sk(), o);
        m_contours = findRegions(sketch());
        break;
    }
    case Drag::Radius: {
        Curve* c = sk().curve(m_dragCurve);
        if (!c) {
            break;
        }
        const Sketch keep = sketch();
        c->radius = std::max(1e-6, length(at - centerOf(sketch(), *c)));
        SolveOptions o;
        o.analyse = false;
        if (!solve(sk(), o).converged) {
            sk() = keep;
        }
        m_contours = findRegions(sketch());
        break;
    }
    case Drag::Label:
        if (Constraint* c = sk().constraint(m_dragCurve)) {
            c->labelX = at.x;
            c->labelY = at.y;
        }
        break;
    case Drag::Box: m_boxEnd = at; break;
    case Drag::None: break;
    }
    redraw();
}

void SketchEditor::endDrag(bool additive) {
    if (m_drag == Drag::Box) {
        const P2 lo{std::min(m_pressAt.x, m_boxEnd.x), std::min(m_pressAt.y, m_boxEnd.y)};
        const P2 hi{std::max(m_pressAt.x, m_boxEnd.x), std::max(m_pressAt.y, m_boxEnd.y)};
        auto inside = [&](P2 q) { return q.x >= lo.x && q.x <= hi.x && q.y >= lo.y && q.y <= hi.y; };
        if (!additive) {
            m_selected.clear();
        }
        const Sketch& s = sketch();
        for (const Curve& c : s.curves()) {
            const std::vector<P2> pts = tessellate(s, c, 0.2);
            if (std::all_of(pts.begin(), pts.end(), inside)) {
                m_selected.insert(c.id);
            }
        }
        for (const Point& p : s.points()) {
            if (p.owner == 0 && p.id != s.origin() && inside({p.x, p.y})) {
                m_selected.insert(p.id);
            }
        }
        m_drag = Drag::None;
        rebuildPanel();
        redraw();
        return;
    }
    if (m_drag != Drag::None && !(sketch() == m_beforeDrag)) {
        m_undo.push_back(m_beforeDrag);
        m_redo.clear();
    }
    m_drag = Drag::None;
    commitEdit();
}

// --- Mouse ------------------------------------------------------------------------------------------------

bool SketchEditor::mousePress(QMouseEvent* event) {
    if (m_valueEditor) {
        closeValueEditor(true);
    }
    const std::optional<P2> at = toSketch(event->position());
    if (!at) {
        return true;
    }
    m_px = viewer()->pixelSize();
    if (event->button() == Qt::RightButton) {
        if (m_tool != Tool::Select && (!m_clicks.empty() || !m_dimPicks.empty())) {
            cancelGesture();
            redraw();
        } else if (m_tool != Tool::Select) {
            setTool(Tool::Select);
        }
        return true;
    }
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_tool == Tool::Convert) {
        if (const auto pick = viewer()->detectAt(event->position())) {
            convertShape(pick->shape);
        }
        return true;
    }
    m_pressed = true;
    m_dragging = false;
    m_pressPos = event->position();
    m_pressAt = *at;
    if (m_tool == Tool::Select) {
        m_pressHit = hitAt(*at, true);
        return true;
    }
    clickAt(at->x, at->y);
    return true;
}

void SketchEditor::mouseMove(QMouseEvent* event) {
    const std::optional<P2> at = toSketch(event->position());
    if (!at || !viewer()) {
        return;
    }
    m_px = viewer()->pixelSize();
    if (m_tool == Tool::Select) {
        if (m_pressed && (event->buttons() & Qt::LeftButton)) {
            if (!m_dragging && QLineF(event->position(), m_pressPos).length() > 4.0) {
                m_dragging = true;
                startDrag();
            }
            if (m_dragging) {
                dragTo(*at);
            }
            return;
        }
        const Hit h = hitAt(*at, true);
        if (!(h == m_hover)) {
            m_hover = h;
            redraw();
        }
        return;
    }
    if (m_tool == Tool::Dimension || isPickTool() || m_tool == Tool::Doctor) {
        const Hit h = hitAt(*at, false);
        const bool usable = m_tool == Tool::Dimension || m_tool == Tool::Doctor ||
                            (m_tool == Tool::Fillet ? h.kind != Hit::None
                                                    : h.kind == Hit::Curve || (m_tool == Tool::Mirror && h.kind == Hit::Point));
        m_hover = usable ? h : Hit{};
        m_cursor = {};
        m_cursor.at = *at;
        m_haveCursor = true;
        redraw();
        return;
    }
    if (m_tool == Tool::Convert) {
        return; // the viewer highlights model edges and faces under the pointer
    }
    const bool anchored = ((m_tool == Tool::Line || m_tool == Tool::Centerline) && !m_clicks.empty()) ||
                          (m_tool == Tool::Slot && m_clicks.size() == 1);
    const std::optional<P2> anchor = anchored ? std::optional<P2>(m_clicks.back().at) : std::nullopt;
    m_cursor = snapAt(*at, anchor);
    m_haveCursor = true;
    if (m_tool == Tool::Arc && m_clicks.size() == 2) {
        const P2 d = m_cursor.at - m_clicks[0].at;
        const double a = std::atan2(d.y, d.x);
        m_arcSweep += wrapPi(a - m_arcLast);
        m_arcLast = a;
    }
    redraw();
}

bool SketchEditor::mouseRelease(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return true;
    }
    const bool wasPressed = m_pressed;
    m_pressed = false;
    if (!wasPressed) {
        return true;
    }
    const bool additive = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    if (m_tool == Tool::Select) {
        if (m_dragging) {
            m_dragging = false;
            endDrag(additive);
            return true;
        }
        const Hit h = m_pressHit;
        if (h.kind == Hit::None) {
            if (!additive) {
                m_selected.clear();
            }
        } else if (additive) {
            if (!m_selected.erase(h.id)) {
                m_selected.insert(h.id);
            }
        } else {
            m_selected = {h.id};
        }
        rebuildPanel();
        redraw();
        return true;
    }
    // Press, drag and release draws a line, rectangle or circle in one go.
    const std::optional<P2> at = toSketch(event->position());
    const bool dragTool = m_tool == Tool::Line || m_tool == Tool::Centerline || m_tool == Tool::Rectangle ||
                          m_tool == Tool::CenterRectangle || m_tool == Tool::Circle;
    if (at && dragTool && m_clicks.size() == 1 && QLineF(event->position(), m_pressPos).length() > 8.0) {
        const bool lineTool = m_tool == Tool::Line || m_tool == Tool::Centerline;
        clickAt(at->x, at->y);
        if (lineTool) {
            endChain();
        }
    }
    return true;
}

bool SketchEditor::mouseDoubleClick(QMouseEvent* event) {
    const std::optional<P2> at = toSketch(event->position());
    if (!at) {
        return true;
    }
    if (m_tool == Tool::Select) {
        const Hit h = hitAt(*at, true);
        if (h.kind == Hit::Dimension) {
            openValueEditor(h.id);
        }
    } else if (m_tool == Tool::Line || m_tool == Tool::Centerline) {
        endChain();
    }
    return true;
}

// --- Panel ---------------------------------------------------------------------------------------------

void SketchEditor::rebuildPanel() {
    if (m_tornDown) {
        return;
    }
    PropertyPanel& panel = m_host.panel();
    panel.clearContent();
    const Sketch& s = sketch();
    const Units units = m_host.units();

    auto statusLine = [this] {
        switch (m_solve.status) {
        case SketchStatus::UnderDefined: return tr("Under Defined: %n degree(s) of freedom left.", nullptr, m_solve.dof);
        case SketchStatus::FullyDefined: return tr("Fully Defined.");
        case SketchStatus::OverDefined: {
            QStringList names;
            for (Id id : m_solve.converged ? m_solve.redundant : m_solve.conflicting) {
                names << QString::fromStdString(sketch().name(id));
            }
            return tr("Over Defined: %1").arg(names.join(QStringLiteral(", ")));
        }
        }
        return QString();
    };

    switch (m_tool) {
    case Tool::Select: {
        if (m_selected.empty()) {
            panel.setMessage(tr("Pick a drawing tool, or select entities to see and add relations. Drag entities to "
                                "see how they can still move. Double-click a dimension to change it."));
            PanelGroup* g = panel.addGroup(tr("Sketch"));
            g->addText(statusLine())->setWordWrap(true);
            QCheckBox* relations = g->addCheck(tr("Show relations"), m_showRelations);
            connect(relations, &QCheckBox::toggled, this, &SketchEditor::showRelations);
            QCheckBox* shade = g->addCheck(tr("Shade closed regions"), m_shadeRegions);
            connect(shade, &QCheckBox::toggled, this, &SketchEditor::shadeRegions);
            g->addText(tr("%n closed region(s)", nullptr, static_cast<int>(m_contours.regions.size())));
            break;
        }
        // One dimension: its value and driving state.
        if (m_selected.size() == 1) {
            const Constraint* c = s.constraint(*m_selected.begin());
            if (c && isDimension(c->type)) {
                const Id id = c->id;
                PanelGroup* g = panel.addGroup(tr("Dimension %1").arg(QString::fromStdString(s.name(id))));
                if (c->type == ConstraintType::Angle) {
                    auto* field = new QLineEdit(QString::number(c->value * 180.0 / kPi, 'f', units.angleDecimals));
                    field->setEnabled(c->driving);
                    connect(field, &QLineEdit::editingFinished, this, [this, field, id] {
                        if (const auto v = units::parseAngle(field->text().toStdString())) {
                            applyValue(id, *v);
                        }
                    });
                    g->addRow(tr("Angle (°)"), field);
                } else {
                    LengthField* field = g->addLength(tr("Value"), c->value, units);
                    field->setRange(1e-6, 1e9);
                    field->setEnabled(c->driving);
                    connect(field, &LengthField::valueChanged, this, [this, id](double mm) { applyValue(id, mm); });
                }
                QCheckBox* driven = g->addCheck(tr("Driven (reference only)"), !c->driving);
                connect(driven, &QCheckBox::toggled, this, [this, id](bool on) {
                    beginEdit();
                    Constraint* k = sk().constraint(id);
                    k->driving = !on;
                    if (on) {
                        k->value = sketch().measure(*k).value_or(k->value);
                    } else {
                        k->value = sketch().measure(*k).value_or(k->value);
                        const SolveResult r = solve(sk());
                        if (!r.converged || !r.redundant.empty()) {
                            sk() = std::move(m_undo.back());
                            m_undo.pop_back();
                            m_host.showStatus(tr("Making it driving would over-define the sketch."), 6000);
                            solveAll();
                            rebuildPanel();
                            redraw();
                            return;
                        }
                    }
                    commitEdit();
                });
                break;
            }
        }

        PanelGroup* existing = panel.addGroup(tr("Existing Relations"));
        auto* list = new QListWidget;
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        list->setMaximumHeight(130);
        for (const Constraint& c : s.constraints()) {
            const bool related = m_selected.count(c.id) ||
                                 std::any_of(c.refs.begin(), c.refs.end(), [this](Id r) { return m_selected.count(r) > 0; });
            if (!related) {
                continue;
            }
            QStringList refs;
            for (Id r : c.refs) {
                refs << QString::fromStdString(s.name(r));
            }
            auto* item = new QListWidgetItem(QStringLiteral("%1  (%2)").arg(QString::fromStdString(s.name(c.id)), refs.join(QStringLiteral(", "))));
            item->setData(Qt::UserRole, c.id);
            if (std::find(m_solve.conflicting.begin(), m_solve.conflicting.end(), c.id) != m_solve.conflicting.end() ||
                std::find(m_solve.redundant.begin(), m_solve.redundant.end(), c.id) != m_solve.redundant.end()) {
                item->setForeground(QColor(225, 30, 30));
            }
            list->addItem(item);
        }
        if (list->count() == 0) {
            list->addItem(tr("(none)"));
            list->setEnabled(false);
        }
        existing->addRow(QString(), list);
        auto* remove = new QPushButton(icon(QStringLiteral("delete")), tr("Delete Selected"));
        remove->setEnabled(list->isEnabled());
        connect(remove, &QPushButton::clicked, this, [this, list] {
            std::vector<Id> ids;
            for (QListWidgetItem* item : list->selectedItems()) {
                ids.push_back(item->data(Qt::UserRole).toInt());
            }
            if (ids.empty()) {
                return;
            }
            beginEdit();
            for (Id id : ids) {
                sk().remove(id);
                m_selected.erase(id);
            }
            commitEdit();
        });
        existing->addRow(QString(), remove);

        const std::vector<ConstraintType> relations = applicableRelations();
        if (!relations.empty()) {
            PanelGroup* add = panel.addGroup(tr("Add Relations"));
            for (ConstraintType type : relations) {
                auto* button = new QPushButton(glyphText(type).isEmpty() ? tr(typeName(type))
                                                                         : QStringLiteral("%1   [%2]").arg(tr(typeName(type)), glyphText(type)));
                button->setStyleSheet(QStringLiteral("text-align: left; padding: 3px 8px;"));
                connect(button, &QPushButton::clicked, this, [this, type] { addRelation(type); });
                add->addRow(QString(), button);
            }
        }
        const bool curves = std::any_of(m_selected.begin(), m_selected.end(), [&s](Id id) { return s.curve(id) != nullptr; });
        if (curves) {
            PanelGroup* options = panel.addGroup(tr("Options"));
            const bool allConstruction = std::all_of(m_selected.begin(), m_selected.end(), [&s](Id id) {
                const Curve* c = s.curve(id);
                return !c || c->construction;
            });
            QCheckBox* construction = options->addCheck(tr("For construction"), allConstruction);
            connect(construction, &QCheckBox::toggled, this, [this] { toggleConstruction(); });
        }
        break;
    }
    case Tool::Line:
    case Tool::Centerline: {
        panel.setMessage(tr("Click to start, click to end each segment (the next one starts there). Double-click, "
                            "right-click or Esc ends the chain. Drag for a single line. Snaps add coincident, "
                            "midpoint and horizontal/vertical relations."));
        PanelGroup* g = panel.addGroup(tr("Options"));
        QCheckBox* construction = g->addCheck(tr("For construction"), m_tool == Tool::Centerline);
        connect(construction, &QCheckBox::toggled, this, [this](bool on) { setTool(on ? Tool::Centerline : Tool::Line); });
        break;
    }
    case Tool::Rectangle:
        panel.setMessage(tr("Click one corner, then the opposite corner (or drag). The sides get horizontal and "
                            "vertical relations."));
        break;
    case Tool::CenterRectangle:
        panel.setMessage(tr("Click the centre, then a corner (or drag)."));
        break;
    case Tool::Circle: panel.setMessage(tr("Click the centre, then a point on the circle (or drag).")); break;
    case Tool::Arc:
        panel.setMessage(tr("Click the centre, then the start point, then the end point. Move the pointer the way "
                            "the arc should go before the last click."));
        break;
    case Tool::TangentArc:
        panel.setMessage(tr("Click the end of a line or arc, then where the arc should end. The arc is tangent to "
                            "the entity it starts from."));
        break;
    case Tool::Point: panel.setMessage(tr("Click to place sketch points.")); break;
    case Tool::Slot:
        panel.setMessage(tr("Click the centre of one end, then the other end, then how wide the slot is. Lines "
                            "and end arcs stay tangent; the centerline gets horizontal or vertical when snapped."));
        break;
    case Tool::Polygon: {
        panel.setMessage(tr("Click the centre, then a corner. The polygon is inscribed in a construction circle "
                            "with equal sides."));
        PanelGroup* g = panel.addGroup(tr("Parameters"));
        auto* sides = new QSpinBox;
        sides->setRange(3, 64);
        sides->setValue(m_polygonSides);
        connect(sides, &QSpinBox::valueChanged, this, [this](int n) { setPolygonSides(n); });
        g->addRow(tr("Sides"), sides);
        break;
    }
    case Tool::Fillet: {
        panel.setMessage(tr("Click the corner where two lines meet (or a line near the corner). The corner is "
                            "kept as a virtual sharp, so its dimensions stay."));
        PanelGroup* g = panel.addGroup(tr("Fillet Parameters"));
        LengthField* radius = g->addLength(tr("Radius"), m_filletRadius, units);
        radius->setRange(1e-4, 1e6);
        connect(radius, &LengthField::valueChanged, this, [this](double mm) { setFilletRadius(mm); });
        break;
    }
    case Tool::Trim:
        panel.setMessage(tr("Click the piece of a curve to remove, up to where other curves cross it. A curve "
                            "that crosses nothing is deleted."));
        break;
    case Tool::Extend:
        panel.setMessage(tr("Click a line or arc near the end to extend; it grows to the next curve it meets."));
        break;
    case Tool::Offset: {
        panel.setMessage(tr("Click the entities to offset (a whole contour with Select chain), set the distance "
                            "and press Apply or Enter. Closed contours go outwards; Reverse goes inwards."));
        PanelGroup* g = panel.addGroup(tr("Parameters"));
        g->addText(tr("%n entity(s) picked", nullptr, static_cast<int>(m_toolPicks.size())));
        LengthField* distance = g->addLength(tr("Distance"), m_offsetDistance, units);
        distance->setRange(1e-4, 1e6);
        connect(distance, &LengthField::valueChanged, this, [this](double mm) { setOffsetDistance(mm, m_offsetReverse); });
        QCheckBox* reverse = g->addCheck(tr("Reverse"), m_offsetReverse);
        connect(reverse, &QCheckBox::toggled, this, [this](bool on) { setOffsetDistance(m_offsetDistance, on); });
        QCheckBox* chain = g->addCheck(tr("Select chain"), m_offsetChain);
        connect(chain, &QCheckBox::toggled, this, [this](bool on) { m_offsetChain = on; });
        QCheckBox* dims = g->addCheck(tr("Add dimensions"), m_offsetDimensions);
        connect(dims, &QCheckBox::toggled, this, [this](bool on) { m_offsetDimensions = on; });
        auto* apply = new QPushButton(tr("Apply"));
        apply->setEnabled(!m_toolPicks.empty());
        connect(apply, &QPushButton::clicked, this, [this] { applyOffset(); });
        g->addRow(QString(), apply);
        break;
    }
    case Tool::Mirror: {
        panel.setMessage(tr("Click the entities to mirror and a centerline to mirror about (or use Pick mirror "
                            "line for a solid line), then press Apply or Enter. Copies stay symmetric."));
        PanelGroup* g = panel.addGroup(tr("Options"));
        g->addText(tr("%n entity(s) to mirror", nullptr, static_cast<int>(m_toolPicks.size())));
        g->addText(m_mirrorAxis ? tr("Mirror about: %1").arg(QString::fromStdString(s.name(m_mirrorAxis)))
                                : tr("Mirror about: (pick a line)"));
        auto* pickAxis = new QPushButton(tr("Pick mirror line"));
        pickAxis->setCheckable(true);
        pickAxis->setChecked(m_pickAxis);
        connect(pickAxis, &QPushButton::toggled, this, [this](bool on) { m_pickAxis = on; });
        g->addRow(QString(), pickAxis);
        auto* apply = new QPushButton(tr("Apply"));
        apply->setEnabled(m_mirrorAxis && !m_toolPicks.empty());
        connect(apply, &QPushButton::clicked, this, [this] { applyMirror(); });
        g->addRow(QString(), apply);
        break;
    }
    case Tool::Convert:
        panel.setMessage(tr("Click a model edge, or a face to take all its edges. The copies are projected onto "
                            "the sketch plane and held fixed."));
        break;
    case Tool::Doctor: {
        const int closed = static_cast<int>(m_contours.regions.size());
        panel.setMessage(m_issues.empty() ? tr("No problems found. %n closed region(s) ready for features.", nullptr, closed)
                                          : tr("%n problem(s) found. Repair fixes gaps, tiny and duplicate entities "
                                               "and repeated relations; the others are shown in red.",
                                               nullptr, static_cast<int>(m_issues.size())));
        PanelGroup* g = panel.addGroup(tr("Problems"));
        auto* list = new QListWidget;
        list->setMaximumHeight(180);
        for (size_t i = 0; i < m_issues.size(); ++i) {
            const Issue& issue = m_issues[i];
            auto* item = new QListWidgetItem(QStringLiteral("%1. %2%3").arg(i + 1).arg(issueText(issue),
                                                                               issue.repairable ? tr("  (can repair)") : QString()));
            list->addItem(item);
        }
        if (m_issues.empty()) {
            list->addItem(tr("(none)"));
            list->setEnabled(false);
        }
        if (m_issueFocus >= 0) {
            list->setCurrentRow(m_issueFocus);
        }
        connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
            m_issueFocus = row;
            redraw();
        });
        g->addRow(QString(), list);
        auto* repairOne = new QPushButton(tr("Repair Selected"));
        repairOne->setEnabled(m_issueFocus >= 0 && m_issueFocus < static_cast<int>(m_issues.size()) &&
                              m_issues[static_cast<size_t>(m_issueFocus)].repairable);
        connect(repairOne, &QPushButton::clicked, this, [this] {
            if (m_issueFocus < 0 || m_issueFocus >= static_cast<int>(m_issues.size())) {
                return;
            }
            const Issue issue = m_issues[static_cast<size_t>(m_issueFocus)];
            m_issueFocus = -1;
            applyEdit([&issue](Sketch& k) { return repair(k, issue); }, tr("That problem cannot be repaired automatically."));
        });
        g->addRow(QString(), repairOne);
        auto* repairEvery = new QPushButton(tr("Repair All"));
        repairEvery->setEnabled(std::any_of(m_issues.begin(), m_issues.end(), [](const Issue& i) { return i.repairable; }));
        connect(repairEvery, &QPushButton::clicked, this, [this] { repairAll(); });
        g->addRow(QString(), repairEvery);
        PanelGroup* summary = panel.addGroup(tr("Contours"));
        summary->addText(tr("Closed regions: %1\nOpen ends: %2").arg(closed).arg(m_contours.openEnds.size()));
        break;
    }
    case Tool::Dimension:
        panel.setMessage(tr("Click an entity, then click where the dimension goes; type the value and press Enter. "
                            "Click two entities for a distance or an angle. Placing a dimension left or right of "
                            "two points makes it vertical, above or below makes it horizontal."));
        break;
    }
}

// --- Drawing ---------------------------------------------------------------------------------------------

QString SketchEditor::valueText(const Constraint& c) const {
    const Units units = m_host.units();
    const double v = c.driving ? c.value : sketch().measure(c).value_or(c.value);
    QString text;
    auto len = [&units](double mm) { return QString::fromStdString(units::formatLength(mm, units, false)); };
    switch (c.type) {
    case ConstraintType::Angle: text = QString::fromUtf8(units::formatAngle(v, units).c_str()); break;
    case ConstraintType::Radius: text = QStringLiteral("R") + len(v); break;
    case ConstraintType::Diameter: text = QStringLiteral("Ø") + len(v); break;
    default: text = len(v); break;
    }
    return c.driving ? text : QStringLiteral("(%1)").arg(text);
}

std::vector<SketchEditor::Glyph> SketchEditor::layoutGlyphs() const {
    const Sketch& s = sketch();
    std::vector<Glyph> out;
    std::map<Id, int> stack;
    const double offset = 14.0 * m_px, step = 26.0 * m_px;
    for (const Constraint& c : s.constraints()) {
        if (isDimension(c.type) || c.type == ConstraintType::Coincident) {
            continue;
        }
        const QString text = glyphText(c.type);
        for (Id r : c.refs) {
            const int i = stack[r]++;
            if (const Curve* k = s.curve(r)) {
                const P2 mid = midpointOf(s, *k);
                P2 n, t;
                if (k->type == CurveType::Line) {
                    t = normalized(pointOf(s, k->p2) - pointOf(s, k->p1));
                    n = perpendicular(t);
                } else {
                    n = normalized(mid - centerOf(s, *k));
                    t = perpendicular(n);
                }
                out.push_back({c.id, mid + n * offset + t * (i * step), text});
            } else if (s.point(r)) {
                out.push_back({c.id, pointOf(s, r) + P2{offset + i * step, offset}, text});
            }
        }
    }
    return out;
}

void SketchEditor::drawDimension(render::Drawing2d& d, const Constraint& c, const Quantity_Color& color) const {
    const Sketch& s = sketch();
    const double arrowSize = 10.0 * m_px, gap = 3.0 * m_px, over = 6.0 * m_px;
    const P2 label{c.labelX, c.labelY};
    const std::vector<Id>& r = c.refs;
    auto lineOf = [&s](Id id) {
        const Curve* k = s.curve(id);
        return isLine(k) ? k : nullptr;
    };
    const QString shown = valueText(c);
    auto text = [&] { d.labels.push_back({g2(label), ext(shown), color, std::nullopt, 15.0f}); };
    // Half the extent of the value text along direction `u` (the text is 15 px high, ~8 px per
    // character), so dimension lines can stop short of it instead of striking it through.
    const double halfW = (shown.size() * 8.0 / 2.0 + 4.0) * m_px, halfH = (15.0 / 2.0 + 3.0) * m_px;
    auto gapAlong = [&](P2 u) { return std::abs(u.x) * halfW + std::abs(u.y) * halfH; };
    // The straight line origin + u*[lo, hi], broken where the text sits (at origin + u*t).
    auto brokenLine = [&](P2 origin, P2 u, double lo, double hi, double t) {
        const double g = gapAlong(u);
        if (t + g <= lo || t - g >= hi) {
            d.line(g2(origin + u * lo), g2(origin + u * hi), color, 1.0f);
            return;
        }
        if (t - g > lo) {
            d.line(g2(origin + u * lo), g2(origin + u * (t - g)), color, 1.0f);
        }
        if (t + g < hi) {
            d.line(g2(origin + u * (t + g)), g2(origin + u * hi), color, 1.0f);
        }
    };

    switch (c.type) {
    case ConstraintType::Distance:
    case ConstraintType::HorizontalDistance:
    case ConstraintType::VerticalDistance: {
        P2 a, b, dir;
        if (r.size() == 1 && lineOf(r[0])) {
            a = pointOf(s, lineOf(r[0])->p1);
            b = pointOf(s, lineOf(r[0])->p2);
        } else if (r.size() == 2 && s.point(r[0]) && s.point(r[1])) {
            a = pointOf(s, r[0]);
            b = pointOf(s, r[1]);
        } else if (r.size() == 2 && lineOf(r[1])) {
            a = s.point(r[0]) ? pointOf(s, r[0]) : lineOf(r[0]) ? pointOf(s, lineOf(r[0])->p1) : P2{};
            const Curve* l = lineOf(r[1]);
            const P2 p = pointOf(s, l->p1), q = pointOf(s, l->p2);
            const P2 u = normalized(q - p);
            b = p + u * dot(a - p, u); // foot of the perpendicular
            if (length(b - a) < 1e-9) {
                dir = u;
            }
        } else {
            text();
            return;
        }
        if (c.type == ConstraintType::HorizontalDistance) {
            dir = {1.0, 0.0};
        } else if (c.type == ConstraintType::VerticalDistance) {
            dir = {0.0, 1.0};
        } else if (length(b - a) > 1e-9) {
            dir = normalized(b - a);
        } else if (length(dir) < 0.5) {
            dir = {1.0, 0.0};
        }
        const P2 n = perpendicular(dir);
        const double oa = dot(label - a, n), ob = dot(label - b, n);
        const P2 a2 = a + n * oa, b2 = b + n * ob;
        auto extension = [&](P2 from, double off) {
            if (std::abs(off) > gap) {
                const double sgn = off > 0 ? 1.0 : -1.0;
                d.line(g2(from + n * (sgn * gap)), g2(from + n * (off + sgn * over)), color, 1.0f);
            }
        };
        extension(a, oa);
        extension(b, ob);
        const P2 u = length(b2 - a2) > 1e-12 ? normalized(b2 - a2) : dir;
        const double len = length(b2 - a2);
        const double t = dot(label - a2, u);
        brokenLine(a2, u, std::min(0.0, t), std::max(len, t), t);
        arrow(d, a2, u * -1.0, arrowSize, color);
        arrow(d, b2, u, arrowSize, color);
        text();
        return;
    }
    case ConstraintType::Radius:
    case ConstraintType::Diameter: {
        const Curve* k = r.size() == 1 ? s.curve(r[0]) : nullptr;
        if (!isRound(k)) {
            text();
            return;
        }
        const P2 center = centerOf(s, *k);
        const double radius = radiusOf(s, *k);
        const P2 dir = length(label - center) > 1e-12 ? normalized(label - center) : P2{1.0, 0.0};
        const double reach = std::max(radius, length(label - center));
        const double at = length(label - center);
        if (c.type == ConstraintType::Radius) {
            brokenLine(center, dir, 0.0, reach, at);
            arrow(d, center + dir * radius, dir, arrowSize, color);
        } else {
            brokenLine(center, dir, -radius, reach, at);
            arrow(d, center + dir * radius, dir, arrowSize, color);
            arrow(d, center - dir * radius, dir * -1.0, arrowSize, color);
        }
        text();
        return;
    }
    case ConstraintType::Angle: {
        const Curve* la = r.size() == 2 ? lineOf(r[0]) : nullptr;
        const Curve* lb = r.size() == 2 ? lineOf(r[1]) : nullptr;
        const std::optional<P2> v = la && lb ? lineIntersection(s, *la, *lb) : std::nullopt;
        if (!v) {
            text();
            return;
        }
        P2 da = normalized(pointOf(s, la->p2) - pointOf(s, la->p1));
        P2 db = normalized(pointOf(s, lb->p2) - pointOf(s, lb->p1));
        if (dot(label - *v, da) < 0) {
            da = da * -1.0;
        }
        if (dot(label - *v, db) < 0) {
            db = db * -1.0;
        }
        double start = std::atan2(da.y, da.x);
        double sweep = std::atan2(db.y, db.x) - start;
        sweep = std::fmod(sweep + 4.0 * kPi, 2.0 * kPi);
        if (sweep > kPi) {
            start += sweep;
            sweep = 2.0 * kPi - sweep;
        }
        const double radius = std::max(20.0 * m_px, length(label - *v));
        const double rel = std::fmod(std::atan2(label.y - v->y, label.x - v->x) - start + 4.0 * kPi, 2.0 * kPi);
        const double ga = gapAlong(perpendicular(normalized(label - *v))) / radius;
        if (length(label - *v) > 1e-12 && rel < sweep) { // break the arc around the text
            if (rel - ga > 0.0) {
                d.strokes.push_back({arcPoints(*v, radius, start, rel - ga), color, 1.0f});
            }
            if (rel + ga < sweep) {
                d.strokes.push_back({arcPoints(*v, radius, start + rel + ga, sweep - rel - ga), color, 1.0f});
            }
        } else {
            d.strokes.push_back({arcPoints(*v, radius, start, sweep), color, 1.0f});
        }
        const P2 e0{std::cos(start), std::sin(start)}, e1{std::cos(start + sweep), std::sin(start + sweep)};
        arrow(d, *v + e0 * radius, perpendicular(e0) * -1.0, arrowSize, color);
        arrow(d, *v + e1 * radius, perpendicular(e1), arrowSize, color);
        text();
        return;
    }
    default: text(); return;
    }
}

void SketchEditor::drawPreview(render::Drawing2d& d) const {
    const Palette pal = palette();
    const Sketch& s = sketch();
    if (m_tool == Tool::Select) {
        if (m_drag == Drag::Box) {
            const P2 a = m_pressAt, b = m_boxEnd;
            d.strokes.push_back({{g2(a), g2({b.x, a.y}), g2(b), g2({a.x, b.y}), g2(a)}, pal.selected, 1.0f, Aspect_TOL_DASH});
        }
        return;
    }
    if (!m_haveCursor) {
        return;
    }
    const P2 cur = m_cursor.at;
    const Quantity_Color& col = pal.preview;

    // Snap and inference feedback
    if (m_tool != Tool::Dimension) {
        if (m_cursor.kind != Snap::Free) {
            d.markers.push_back({g2(cur), pal.snap, Aspect_TOM_RING1, 2.5f});
            const QString tag = m_cursor.kind == Snap::Midpoint ? QStringLiteral("Mid")
                                : m_cursor.kind == Snap::OnCurve ? QStringLiteral("On")
                                                                 : QString();
            if (!tag.isEmpty()) {
                d.labels.push_back({g2(cur + P2{18 * m_px, -16 * m_px}), ext(tag), pal.glyphText, pal.glyphBox, 12.0f});
            }
        }
        if (m_cursor.tangent || m_cursor.horizontal || m_cursor.vertical) {
            const QString tag = m_cursor.tangent      ? QStringLiteral("T")
                                : m_cursor.horizontal ? QStringLiteral("H")
                                                      : QStringLiteral("V");
            d.labels.push_back({g2(cur + P2{18 * m_px, -16 * m_px}), ext(tag), pal.glyphText, pal.glyphBox, 12.0f});
        }
        if (m_cursor.alignX) {
            d.line(g2(*m_cursor.alignX), g2(cur), pal.inference, 1.0f, Aspect_TOL_DOT);
        }
        if (m_cursor.alignY) {
            d.line(g2(*m_cursor.alignY), g2(cur), pal.inference, 1.0f, Aspect_TOL_DOT);
        }
    }

    const Units units = m_host.units();
    auto lengthLabel = [&](double mm) {
        d.labels.push_back({g2(cur + P2{22 * m_px, 18 * m_px}), ext(QString::fromStdString(units::formatLength(mm, units, false))),
                            col, std::nullopt, 13.0f});
    };
    auto rect = [&](P2 a, P2 b, Aspect_TypeOfLine type = Aspect_TOL_SOLID) {
        d.strokes.push_back({{g2(a), g2({b.x, a.y}), g2(b), g2({a.x, b.y}), g2(a)}, col, 1.8f, type});
    };

    switch (m_tool) {
    case Tool::Line:
    case Tool::Centerline:
        if (!m_clicks.empty()) {
            d.line(g2(m_clicks.back().at), g2(cur), col, 1.8f, m_tool == Tool::Centerline ? Aspect_TOL_DOTDASH : Aspect_TOL_SOLID);
            lengthLabel(length(cur - m_clicks.back().at));
        }
        break;
    case Tool::Rectangle:
        if (!m_clicks.empty()) {
            rect(m_clicks[0].at, cur);
        }
        break;
    case Tool::CenterRectangle:
        if (!m_clicks.empty()) {
            const P2 c = m_clicks[0].at, h = cur - c;
            rect(c - h, c + h);
            d.line(g2(c - h), g2(c + h), col, 1.0f, Aspect_TOL_DASH);
            d.line(g2({c.x + h.x, c.y - h.y}), g2({c.x - h.x, c.y + h.y}), col, 1.0f, Aspect_TOL_DASH);
        }
        break;
    case Tool::Circle:
        if (!m_clicks.empty()) {
            const double r = length(cur - m_clicks[0].at);
            d.strokes.push_back({arcPoints(m_clicks[0].at, r, 0.0, 2.0 * kPi), col, 1.8f});
            lengthLabel(r);
        }
        break;
    case Tool::Arc:
        if (m_clicks.size() == 1) {
            d.line(g2(m_clicks[0].at), g2(cur), col, 1.0f, Aspect_TOL_DASH);
            lengthLabel(length(cur - m_clicks[0].at));
        } else if (m_clicks.size() == 2) {
            const P2 c = m_clicks[0].at, st = m_clicks[1].at - c;
            d.strokes.push_back({arcPoints(c, length(st), std::atan2(st.y, st.x), m_arcSweep), col, 1.8f});
            d.line(g2(c), g2(cur), col, 1.0f, Aspect_TOL_DASH);
        }
        break;
    case Tool::TangentArc:
        if (!m_clicks.empty()) {
            const Point* p = s.point(m_clicks[0].target);
            const Curve* owner = p ? s.curve(p->owner) : nullptr;
            const std::optional<P2> dir = owner ? exitDirection(s, *owner, p->id) : std::nullopt;
            if (const std::optional<TangentArc> fit = dir ? tangentArc(m_clicks[0].at, *dir, cur) : std::nullopt) {
                const P2 a = m_clicks[0].at - fit->center, b = cur - fit->center;
                const double a0 = std::atan2(a.y, a.x);
                double sweep = std::atan2(b.y, b.x) - a0;
                sweep = fit->ccw ? std::fmod(sweep + 4.0 * kPi, 2.0 * kPi) : -std::fmod(-sweep + 4.0 * kPi, 2.0 * kPi);
                d.strokes.push_back({arcPoints(fit->center, length(a), a0, sweep), col, 1.8f});
            } else {
                d.line(g2(m_clicks[0].at), g2(cur), col, 1.8f);
            }
        }
        break;
    case Tool::Dimension:
        if (const std::optional<Constraint> c = pendingDimension(cur)) {
            drawDimension(d, *c, col);
        }
        break;
    case Tool::Slot:
        if (m_clicks.size() == 1) {
            d.line(g2(m_clicks[0].at), g2(cur), col, 1.0f, Aspect_TOL_DOTDASH);
            lengthLabel(length(cur - m_clicks[0].at));
        } else if (m_clicks.size() == 2) {
            const P2 a = m_clicks[0].at, b = m_clicks[1].at, u = normalized(b - a);
            const double half = std::abs(cross(u, cur - a));
            if (half > 1e-9) {
                Sketch preview;
                addSlot(preview, a, b, half);
                for (const Curve& c : preview.curves()) {
                    std::vector<gp_Pnt2d> pts;
                    for (P2 p : tessellate(preview, c)) {
                        pts.push_back(g2(p));
                    }
                    d.strokes.push_back({std::move(pts), col, c.construction ? 1.0f : 1.8f,
                                         c.construction ? Aspect_TOL_DOTDASH : Aspect_TOL_SOLID});
                }
                lengthLabel(2.0 * half);
            }
        }
        break;
    case Tool::Polygon:
        if (!m_clicks.empty() && length(cur - m_clicks[0].at) > 1e-9) {
            const P2 c = m_clicks[0].at, v = cur - c;
            const double r = length(v), a0 = std::atan2(v.y, v.x);
            std::vector<gp_Pnt2d> pts;
            for (int i = 0; i <= m_polygonSides; ++i) {
                const double a = a0 + 2.0 * kPi * i / m_polygonSides;
                pts.push_back(g2(c + P2{std::cos(a), std::sin(a)} * r));
            }
            d.strokes.push_back({std::move(pts), col, 1.8f});
            d.strokes.push_back({arcPoints(c, r, 0.0, 2.0 * kPi), col, 1.0f, Aspect_TOL_DASH});
            lengthLabel(r);
        }
        break;
    case Tool::Offset:
    case Tool::Mirror: {
        // What Apply would add, drawn from a scratch copy.
        if (m_toolPicks.empty() || (m_tool == Tool::Mirror && !m_mirrorAxis)) {
            break;
        }
        Sketch scratch = s;
        const std::vector<Id> picks(m_toolPicks.begin(), m_toolPicks.end());
        const std::vector<Id> added =
            m_tool == Tool::Offset ? offset(scratch, picks, signedOffset(), false) : mirror(scratch, picks, m_mirrorAxis);
        for (Id id : added) {
            if (const Curve* c = scratch.curve(id)) {
                std::vector<gp_Pnt2d> pts;
                for (P2 p : tessellate(scratch, *c)) {
                    pts.push_back(g2(p));
                }
                d.strokes.push_back({std::move(pts), col, 1.6f, Aspect_TOL_DASH});
            } else if (const Point* p = scratch.point(id)) {
                d.markers.push_back({gp_Pnt2d(p->x, p->y), col, Aspect_TOM_BALL, 2.0f});
            }
        }
        break;
    }
    default: break;
    }
}

void SketchEditor::redraw() {
    render::OcctViewer* v = viewer();
    if (m_prs.IsNull() || !v || m_tornDown) {
        return;
    }
    m_px = v->pixelSize();
    const Palette pal = palette();
    const Sketch& s = sketch();
    render::Drawing2d d;

    if (m_shadeRegions) {
        for (const Region& r : m_contours.regions) {
            render::Drawing2d::Fill fill;
            fill.color = pal.region;
            fill.transparency = 0.72f;
            for (const Loop* loop : [&r] {
                     std::vector<const Loop*> all{&r.outer};
                     for (const Loop& h : r.holes) {
                         all.push_back(&h);
                     }
                     return all;
                 }()) {
                std::vector<gp_Pnt2d> pts;
                for (P2 p : loop->polygon) {
                    pts.push_back(g2(p));
                }
                fill.loops.push_back(std::move(pts));
            }
            d.fills.push_back(std::move(fill));
        }
    }

    std::set<Id> bad;
    auto markBad = [&s, &bad](Id cid) {
        if (const Constraint* c = s.constraint(cid)) {
            bad.insert(cid);
            bad.insert(c->refs.begin(), c->refs.end());
        }
    };
    for (Id c : m_solve.conflicting) {
        markBad(c);
    }
    for (Id c : m_solve.redundant) {
        markBad(c);
    }
    std::set<Id> picked;
    for (const Hit& h : m_dimPicks) {
        picked.insert(h.id);
    }
    if (m_tool == Tool::Doctor) {
        for (int i = 0; i < static_cast<int>(m_issues.size()); ++i) {
            for (Id id : m_issues[static_cast<size_t>(i)].ids) {
                if (i == m_issueFocus || m_issueFocus < 0) {
                    bad.insert(id);
                }
            }
        }
    }
    auto colorFor = [&](Id id, bool troubled, bool defined) {
        if (m_selected.count(id) || picked.count(id) || m_toolPicks.count(id)) {
            return pal.selected;
        }
        if (id == m_mirrorAxis) {
            return pal.hover;
        }
        if (m_hover.kind != Hit::None && m_hover.id == id) {
            return pal.hover;
        }
        if (troubled) {
            return pal.over;
        }
        return defined ? pal.defined : pal.under;
    };

    // Sketch origin: two short red arrows.
    const double axis = 22.0 * m_px;
    const Quantity_Color originColor = m_selected.count(s.origin()) || picked.count(s.origin()) ? pal.selected
                                       : m_hover.kind == Hit::Point && m_hover.id == s.origin() ? pal.hover
                                                                                               : pal.origin;
    d.line(gp_Pnt2d(0, 0), gp_Pnt2d(axis, 0), originColor, 1.5f);
    d.line(gp_Pnt2d(0, 0), gp_Pnt2d(0, axis), originColor, 1.5f);
    arrow(d, {axis, 0}, {1, 0}, 7.0 * m_px, originColor);
    arrow(d, {0, axis}, {0, 1}, 7.0 * m_px, originColor);

    for (const Curve& c : s.curves()) {
        const bool troubled = bad.count(c.id) || (c.p1 && bad.count(c.p1)) || (c.p2 && bad.count(c.p2)) || (c.center && bad.count(c.center));
        const Quantity_Color color = colorFor(c.id, troubled, m_solve.isDefined(c.id));
        const double r = std::max(radiusOf(s, c), 1e-9);
        const double step = std::clamp(std::sqrt(4.0 * m_px / r), 0.005, 0.15);
        std::vector<gp_Pnt2d> pts;
        for (P2 p : tessellate(s, c, step)) {
            pts.push_back(g2(p));
        }
        const bool highlighted = m_selected.count(c.id) || (m_hover.kind != Hit::None && m_hover.id == c.id);
        d.strokes.push_back({std::move(pts), color, highlighted ? 2.6f : 2.0f,
                             c.construction ? (c.type == CurveType::Line ? Aspect_TOL_DOTDASH : Aspect_TOL_DASH) : Aspect_TOL_SOLID});
    }
    for (const Point& p : s.points()) {
        const bool standalone = p.owner == 0;
        if (p.id == s.origin() && !m_selected.count(p.id) && !(m_hover.kind == Hit::Point && m_hover.id == p.id)) {
            continue;
        }
        const Curve* owner = s.curve(p.owner);
        if (owner && owner->construction && !m_selected.count(p.id)) {
            continue;
        }
        const Quantity_Color color = colorFor(p.id, bad.count(p.id) > 0, m_solve.isDefined(p.id));
        if (standalone && p.construction) { // a fillet's virtual sharp
            d.markers.push_back({gp_Pnt2d(p.x, p.y), color, Aspect_TOM_PLUS, 1.6f});
            continue;
        }
        d.markers.push_back({gp_Pnt2d(p.x, p.y), color, Aspect_TOM_BALL, standalone ? 2.4f : 1.6f});
    }
    if (m_tool == Tool::Doctor) {
        for (int i = 0; i < static_cast<int>(m_issues.size()); ++i) {
            const Issue& issue = m_issues[static_cast<size_t>(i)];
            const bool focus = i == m_issueFocus;
            d.markers.push_back({g2(issue.at), pal.problem, Aspect_TOM_RING3, focus ? 4.0f : 3.0f});
            if (focus || m_issueFocus < 0) {
                d.labels.push_back({g2(issue.at + P2{0.0, 18.0 * m_px}), ext(QString::number(i + 1)), rgb(255, 255, 255), pal.problem, 11.0f});
            }
        }
    }

    for (const Constraint& c : s.constraints()) {
        if (!isDimension(c.type)) {
            continue;
        }
        Quantity_Color color = c.driving ? pal.dimension : pal.driven;
        if (bad.count(c.id)) {
            color = pal.over;
        }
        if (m_selected.count(c.id)) {
            color = pal.selected;
        } else if (m_hover.kind == Hit::Dimension && m_hover.id == c.id) {
            color = pal.hover;
        }
        drawDimension(d, c, color);
    }

    if (m_showRelations) {
        for (const Glyph& g : layoutGlyphs()) {
            const bool on = m_selected.count(g.constraint) || (m_hover.kind == Hit::Relation && m_hover.id == g.constraint);
            const Quantity_Color box = on ? pal.selected : bad.count(g.constraint) ? pal.over : pal.glyphBox;
            d.labels.push_back({g2(g.at), ext(g.text), pal.glyphText, box, 11.0f});
        }
    }

    drawPreview(d);
    m_prs->setDrawing(std::move(d));
    v->updateOverlay(m_prs);
}

} // namespace bl::ui
