#pragma once

#include "doc/PartModel.h"
#include "render/OcctViewer.h"
#include "render/SketchPresentation.h"
#include "sketch/Geometry.h"
#include "sketch/Operations.h"
#include "sketch/Solver.h"
#include "ui/InteractiveCommand.h"

#include <gp_Pln.hxx>

#include <QPointer>
#include <QPointF>

#include <functional>
#include <optional>
#include <set>
#include <vector>

class QLineEdit;
class TopoDS_Edge;
class TopoDS_Shape;

namespace bl::ui {

class DocumentView;

// Sketch coordinate systems for the default planes (0 Front, 1 Top, 2 Right) and for a planar face.
gp_Ax3 referencePlane(int which);
QString referencePlaneName(int which);
gp_Ax3 facePlane(const gp_Pln& face);

// How a finished sketch looks in the model: its curves only, in a neutral colour.
render::Drawing2d inactiveSketchDrawing(const sketch::Sketch& sketch, bool dark);

// "Sketch" without a plane picked yet: choose Front/Top/Right or click a planar face.
class SketchPlaneCommand : public InteractiveCommand {
    Q_OBJECT
public:
    // `face`: set when the plane is a model face (the sketch then follows the face).
    using Chosen = std::function<void(const gp_Ax3& plane, const QString& name, std::optional<kernel::FaceRef> face)>;
    SketchPlaneCommand(CommandHost& host, Chosen chosen);

    QString id() const override { return QStringLiteral("sketch.plane"); }
    QString title() const override { return tr("Sketch"); }
    QString iconName() const override { return QStringLiteral("sketch"); }
    bool confirms() const override { return false; }
    void start() override;
    void selectionChanged() override;

private:
    void choose(const gp_Ax3& plane, const QString& name, std::optional<kernel::FaceRef> face = std::nullopt);

    Chosen m_chosen;
    bool m_done = false;
};

// Sketch mode (PLAN.md 6.6, 7.5): edits a copy of one sketch feature in the graphics area, with
// drawing tools, snaps and automatic relations, Smart Dimension, relations and status colours.
// The sketch has its own undo history while open; OK (Exit Sketch) writes it back to the document
// as one undoable change, Cancel discards it.
class SketchEditor : public InteractiveCommand, public render::ViewerInput {
    Q_OBJECT
public:
    enum class Tool {
        Select,
        Line,
        Centerline,
        Rectangle,
        CenterRectangle,
        Slot,
        Circle,
        Arc,
        TangentArc,
        Polygon,
        Point,
        Dimension,
        Fillet,
        Trim,
        Extend,
        Offset,
        Mirror,
        Convert,
        Doctor,
    };

    SketchEditor(CommandHost& host, doc::SketchFeature feature, bool isNew);
    ~SketchEditor() override;

    QString id() const override { return QStringLiteral("sketch.edit"); }
    QString title() const override;
    QString iconName() const override { return QStringLiteral("sketch"); }
    void start() override;
    bool accept() override;
    void cancel() override;
    bool confirmCancel() override;
    bool keepOnInterrupt() const override { return true; }
    void unitsChanged() override;
    bool enterPressed() override;
    bool escapePressed() override;
    void deletePressed() override;
    bool handlesUndo() const override { return true; }
    bool canUndo() const override { return !m_undo.empty(); }
    bool canRedo() const override { return !m_redo.empty(); }
    void undo() override;
    void redo() override;

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    QUuid sketchId() const { return m_feature.id; }
    const sketch::Sketch& sketch() const { return m_feature.sketch; }
    const sketch::SolveResult& solveResult() const { return m_solve; }
    void toggleConstruction(); // selected curves
    void showRelations(bool on);
    void shadeRegions(bool on);

    // Tool options (also in the Property Panel while the tool is active).
    void setPolygonSides(int sides);
    void setFilletRadius(double mm);
    void setOffsetDistance(double mm, bool reverse);
    // Offset / Mirror: apply to the picked entities (Enter does the same).
    bool applyOffset();
    bool applyMirror();
    // Convert Entities: copies a model edge, or every edge of a face, into the sketch (held fixed).
    void convertShape(const TopoDS_Shape& shape);
    // Sketch Doctor: repairs every problem it can.
    void repairAll();
    const sketch::Contours& contours() const { return m_contours; }
    const std::vector<sketch::Issue>& issues() const { return m_issues; }

    // Scripted input (smoke demo, tests): the same as clicking at sketch coordinates with the
    // current tool, or pressing Enter in the value box.
    void clickAt(double u, double v);
    void endChain();
    void setDimensionValue(sketch::Id dimension, double value);
    void select(const std::vector<sketch::Id>& ids);

    // render::ViewerInput
    bool mousePress(QMouseEvent* event) override;
    void mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    bool mouseDoubleClick(QMouseEvent* event) override;
    void viewChanged() override { redraw(); }

private:
    using Id = sketch::Id;
    using P2 = sketch::P2;

    struct Hit {
        enum Kind { None, Point, Curve, Dimension, Relation } kind = None;
        Id id = 0;
        bool operator==(const Hit&) const = default;
    };
    struct Snap {
        P2 at;
        enum Kind { Free, Point, Midpoint, OnCurve } kind = Free;
        Id target = 0;
        bool horizontal = false, vertical = false;  // relative to the previous click
        Id tangent = 0;                             // line leaving this arc along its tangent
        std::optional<P2> alignX, alignY;           // inference lines through other points
    };
    struct Glyph {
        Id constraint = 0;
        P2 at;
        QString text;
    };
    enum class Drag { None, Points, Radius, Label, Box };

    sketch::Sketch& sk() { return m_feature.sketch; }
    render::OcctViewer* viewer() const;
    std::optional<P2> toSketch(const QPointF& pos) const;
    gp_Pnt to3d(P2 p) const;
    double px() const { return m_px; }

    Hit hitAt(P2 p, bool annotations) const;
    Snap snapAt(P2 p, const std::optional<P2>& anchor) const;
    void applySnap(Id point, const Snap& snap);

    // Editing steps: remember the sketch for undo, change it, then solve and show the result.
    void beginEdit();
    void commitEdit();
    void solveAll();
    void cancelGesture();

    void toolClick(const Snap& snap);
    void pickClick(P2 at);          // Fillet, Trim, Extend, Offset, Mirror
    bool isPickTool() const;
    // Runs an editing operation as one undo step; restores the sketch if it reports failure.
    bool applyEdit(const std::function<bool(sketch::Sketch&)>& op, const QString& failure);
    std::vector<Id> chainOf(Id curve) const;
    int convertEdge(const TopoDS_Edge& edge);
    void refreshAnalysis(); // regions, and the Sketch Doctor's issues while it is open
    double signedOffset() const { return m_offsetReverse ? -m_offsetDistance : m_offsetDistance; }
    QString issueText(const sketch::Issue& issue) const;
    void dimensionClick(P2 at);
    std::optional<sketch::Constraint> pendingDimension(P2 label) const;
    void placeDimension(sketch::Constraint c);
    bool applyValue(Id dimension, double value);
    void addRelation(sketch::ConstraintType type);
    std::vector<sketch::ConstraintType> applicableRelations() const;

    void startDrag();
    void dragTo(P2 at);
    void endDrag(bool additive);

    void openValueEditor(Id dimension);
    void closeValueEditor(bool apply);

    void redraw();
    void rebuildPanel();
    void updateStatus();
    QString valueText(const sketch::Constraint& c) const;
    std::vector<Glyph> layoutGlyphs() const;
    void drawDimension(render::Drawing2d& d, const sketch::Constraint& c, const Quantity_Color& color) const;
    void drawPreview(render::Drawing2d& d) const;
    void teardown();

    doc::SketchFeature m_feature;
    bool m_isNew;
    sketch::Sketch m_original;
    gp_Pln m_plane;
    QPointer<DocumentView> m_view;
    Handle(render::SketchPresentation) m_prs;
    std::vector<sketch::Sketch> m_undo, m_redo;
    sketch::SolveResult m_solve;
    bool m_tornDown = false;

    Tool m_tool = Tool::Select;
    std::set<Id> m_selected;
    Hit m_hover;
    bool m_showRelations = true;
    double m_px = 0.1;

    // Drawing tools
    std::vector<Snap> m_clicks;
    Snap m_cursor;
    bool m_haveCursor = false;
    Id m_chainStart = 0;      // first point of a line chain (clicking it again closes the chain)
    Id m_chainStartTarget = 0; // ...or the existing point that first click snapped to
    double m_arcSweep = 0.0;  // signed sweep followed while placing a centre-point arc's end
    double m_arcLast = 0.0;
    std::vector<Hit> m_dimPicks;

    // Select tool: press, drag
    bool m_pressed = false;
    bool m_dragging = false;
    QPointF m_pressPos;
    P2 m_pressAt;
    Hit m_pressHit;
    Drag m_drag = Drag::None;
    std::vector<std::pair<Id, P2>> m_dragPoints; // dragged points and where they started
    Id m_dragCurve = 0;
    sketch::Sketch m_beforeDrag;
    P2 m_boxEnd;

    QPointer<QLineEdit> m_valueEditor;
    Id m_valueDimension = 0;

    // Tool options and picks
    int m_polygonSides = 6;
    double m_filletRadius = 2.0;
    double m_offsetDistance = 2.0;
    bool m_offsetReverse = false;
    bool m_offsetChain = true;
    bool m_offsetDimensions = true;
    std::set<Id> m_toolPicks; // Offset / Mirror entities
    Id m_mirrorAxis = 0;
    bool m_pickAxis = false;
    bool m_shadeRegions = true;
    bool m_modelWasPickable = false;
    sketch::Contours m_contours;
    std::vector<sketch::Issue> m_issues;
    int m_issueFocus = -1;
};

} // namespace bl::ui
