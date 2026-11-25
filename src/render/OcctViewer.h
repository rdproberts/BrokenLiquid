#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <NCollection_Vec2.hxx>
#include <TopoDS_Shape.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>

#include <QColor>
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QString>

#include <optional>
#include <vector>

namespace bl::render {

// Takes over the left (and right) mouse button while set, e.g. for the sketch editor. Middle-button
// navigation and the wheel keep working. Return true from a press/release to swallow it.
class ViewerInput {
public:
    virtual ~ViewerInput() = default;
    virtual bool mousePress(QMouseEvent* event) = 0;
    virtual void mouseMove(QMouseEvent* event) = 0;
    virtual bool mouseRelease(QMouseEvent* event) = 0;
    virtual bool mouseDoubleClick(QMouseEvent* event) = 0;
    virtual void viewChanged() {} // zoom or orientation changed (pixel-sized graphics need a rebuild)
};

// Display styles matching the familiar CAD set (see PLAN.md 6.10).
enum class DisplayStyle { ShadedWithEdges, Shaded, HiddenLinesRemoved, HiddenLinesVisible, Wireframe };

// Y-up standard orientations (Front = looking along -Z at the XY plane, like SolidWorks).
enum class StandardView { Front, Back, Left, Right, Top, Bottom, Isometric, Dimetric, Trimetric };

// OCCT V3d viewer hosted in a QOpenGLWidget. Based on the approach of OCCT's own Qt sample
// (Qt owns the GL context and FBO; OCCT renders into it).
//
// Mouse (SolidWorks-style, see PLAN.md 7.6):
//   middle drag            rotate          ctrl + middle drag   pan
//   shift + middle drag    zoom            wheel                zoom at cursor
//   left click / drag      select / box select (ctrl toggles)
class OcctViewer : public QOpenGLWidget, public AIS_ViewController {
    Q_OBJECT
public:
    explicit OcctViewer(QWidget* parent = nullptr);
    ~OcctViewer() override;

    const Handle(AIS_InteractiveContext)& context() const { return m_context; }
    const Handle(V3d_View)& view() const { return m_view; }

    // Displays an object with the current display style and face/edge/vertex selection.
    void display(const Handle(AIS_InteractiveObject)& object, bool fit = true);
    void remove(const Handle(AIS_InteractiveObject)& object);
    void removeAll();

    void fitAll();
    void setStandardView(StandardView v);
    // Looks straight at a plane with the given outward normal.
    void normalTo(const gp_Dir& normal);
    // Looks straight at a plane with its Y direction up (sketch mode). `fit`: zoom to fit too.
    void normalTo(const gp_Ax3& plane, bool fit);

    // --- Input and coordinates ----------------------------------------------------------------------
    void setInput(ViewerInput* input) { m_input = input; }
    ViewerInput* input() const { return m_input; }
    // Point under a widget position (logical pixels) on a plane; nothing if the view looks along it.
    std::optional<gp_Pnt> pointOnPlane(const QPointF& pos, const gp_Pln& plane) const;
    // Model units (mm) per logical pixel.
    double pixelSize() const;
    // Widget position (logical pixels) of a model point.
    QPointF toWidget(const gp_Pnt& p) const;
    // false: model objects cannot be picked or hover-highlighted at all (sketch mode).
    void setModelPickable(bool pickable);
    bool isModelPickable() const { return m_pickable; }

    // --- Overlays (sketch graphics): displayed above the model, never selectable -----------------
    void showOverlay(const Handle(AIS_InteractiveObject)& object, bool alwaysOnTop);
    void updateOverlay(const Handle(AIS_InteractiveObject)& object);

    // Back to the previous orientation/zoom (View HUD "Previous View"). Returns false if none.
    bool previousView();
    // Next left-drag zooms to the dragged rectangle (View HUD "Zoom to Area").
    void armZoomArea();

    // --- Selection ------------------------------------------------------------------------------
    struct Pick {
        Handle(AIS_InteractiveObject) object;
        TopoDS_Shape shape; // picked sub-shape (face/edge/vertex) or the whole object shape
    };
    std::vector<Pick> selection() const;
    // The pickable model sub-shape (per the selection filter) under a widget position, without
    // selecting it (for commands that take model picks while they own the mouse).
    std::optional<Pick> detectAt(const QPointF& pos);
    void clearSelection();         // emits selectionChanged
    void deselect(int index);      // drops one pick (index into selection()); emits selectionChanged
    // Adds a sub-shape (face/edge/vertex) of a displayed object to the selection as if it had been
    // clicked. Returns false if that kind of sub-shape is not pickable right now.
    bool select(const Handle(AIS_InteractiveObject)& object, const TopoDS_Shape& sub);
    // Which sub-shape kinds can be picked (applies to all displayed model objects).
    void setSelectionFilter(bool faces, bool edges, bool vertices);

    // --- Temporary graphics (measure lines, previews): drawn on top, never selectable ------------
    void showTemporary(const Handle(AIS_InteractiveObject)& object);
    void clearTemporary();

    // --- Section view (PLAN.md 6.10: Graphic3d_ClipPlane with capping) ---------------------------
    // Keeps the material on the side opposite to the plane normal; the cut face is capped.
    void setSection(const gp_Pln& plane, bool cap);
    void clearSection();
    bool hasSection() const { return !m_section.IsNull(); }
    void setDisplayStyle(DisplayStyle style);
    DisplayStyle displayStyle() const { return m_style; }
    void setBackgroundGradient(const QColor& top, const QColor& bottom);
    void setReverseWheelZoom(bool reverse) { m_reverseWheel = reverse; }

    bool isGlReady() const { return m_glReady; }
    // Full redraw on the next frame (after changing objects, highlight or clipping directly).
    void redraw();
    QString glInfo() const { return m_glInfo; }

signals:
    void navigationFinished(); // a rotate/pan/zoom drag ended (view history)
    void glReady(const QString& info);
    void glFailed(const QString& error);
    void firstFrame();
    void selectionChanged(int count);

protected:
    void initializeGL() override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

    // AIS_ViewController
    void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& view) override;
    void OnSelectionChanged(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& view) override;

private:
    void updateView();
    void pushViewState();
    void activateSelection(const Handle(AIS_InteractiveObject)& object);
    bool isTemporary(const Handle(AIS_InteractiveObject)& object) const;
    void applyStyle(const Handle(AIS_InteractiveObject)& object);
    void configureNavigation();
    void configureLook();
    NCollection_Vec2<int> toPixels(const QPointF& logical) const;

    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(AIS_InteractiveContext) m_context;

    DisplayStyle m_style = DisplayStyle::ShadedWithEdges;
    std::vector<Handle(Graphic3d_Camera)> m_viewHistory;
    std::vector<Handle(AIS_InteractiveObject)> m_temporary;
    std::vector<Handle(AIS_InteractiveObject)> m_overlays;
    ViewerInput* m_input = nullptr;
    bool m_pickable = true;
    double m_lastScale = 0.0;
    gp_Dir m_lastDirection;
    Handle(Graphic3d_ClipPlane) m_section;
    bool m_pickFaces = true, m_pickEdges = true, m_pickVertices = true;
    bool m_zoomAreaArmed = false;
    bool m_navigating = false;
    QElapsedTimer m_wheelTimer;
    QString m_glInfo;
    bool m_glReady = false;
    bool m_firstFrameSent = false;
    bool m_reverseWheel = false;
};

} // namespace bl::render
