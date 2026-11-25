#pragma once

#include <AIS_InteractiveContext.hxx>
#include <AIS_ViewController.hxx>
#include <NCollection_Vec2.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>

#include <QColor>
#include <QOpenGLWidget>
#include <QString>

namespace bl::render {

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
    void removeAll();

    void fitAll();
    void setStandardView(StandardView v);
    void setDisplayStyle(DisplayStyle style);
    DisplayStyle displayStyle() const { return m_style; }
    void setBackgroundGradient(const QColor& top, const QColor& bottom);
    void setReverseWheelZoom(bool reverse) { m_reverseWheel = reverse; }

    bool isGlReady() const { return m_glReady; }
    QString glInfo() const { return m_glInfo; }

signals:
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
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

    // AIS_ViewController
    void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& view) override;
    void OnSelectionChanged(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& view) override;

private:
    void updateView();
    void applyStyle(const Handle(AIS_InteractiveObject)& object);
    void configureNavigation();
    void configureLook();
    NCollection_Vec2<int> toPixels(const QPointF& logical) const;

    Handle(V3d_Viewer) m_viewer;
    Handle(V3d_View) m_view;
    Handle(AIS_InteractiveContext) m_context;

    DisplayStyle m_style = DisplayStyle::ShadedWithEdges;
    QString m_glInfo;
    bool m_glReady = false;
    bool m_firstFrameSent = false;
    bool m_reverseWheel = false;
};

} // namespace bl::render
