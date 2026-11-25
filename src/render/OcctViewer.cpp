#include "render/OcctViewer.h"

#include <AIS_Shape.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Aspect_NeutralWindow.hxx>
#include <Graphic3d_Camera.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_List.hxx>
#include <OpenGl_Context.hxx>
#include <OpenGl_FrameBuffer.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <OpenGl_View.hxx>
#include <OpenGl_Window.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Quantity_Color.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <Standard_Failure.hxx>
#include <StdSelect_BRepOwner.hxx>

#include <spdlog/spdlog.h>

#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "render/NativeWindow.h"

namespace bl::render {
namespace {

// Wraps the FBO that QOpenGLWidget renders into. Qt's FBO uses GL_RGBA8 (not sRGB), so OCCT must
// apply gamma correction itself. Same technique as OCCT's samples/qt/OcctQtViewer.
class QtFrameBuffer : public OpenGl_FrameBuffer {
    DEFINE_STANDARD_RTTI_INLINE(QtFrameBuffer, OpenGl_FrameBuffer)
public:
    void BindBuffer(const Handle(OpenGl_Context)& ctx) override {
        OpenGl_FrameBuffer::BindBuffer(ctx);
        ctx->SetFrameBufferSRGB(true, false);
    }
    void BindDrawBuffer(const Handle(OpenGl_Context)& ctx) override {
        OpenGl_FrameBuffer::BindDrawBuffer(ctx);
        ctx->SetFrameBufferSRGB(true, false);
    }
    void BindReadBuffer(const Handle(OpenGl_Context)& ctx) override { OpenGl_FrameBuffer::BindReadBuffer(ctx); }
};

Handle(OpenGl_Context) glContextOf(const Handle(V3d_View)& view) {
    Handle(OpenGl_View) glView = Handle(OpenGl_View)::DownCast(view->View());
    return glView->GlWindow()->GetGlContext();
}

Aspect_Drawable currentNativeWindow(QWidget* widget) {
#ifdef _WIN32
    // QOpenGLWidget renders through the top-level window; use the one the current WGL context belongs to.
    Q_UNUSED(widget);
    return reinterpret_cast<Aspect_Drawable>(currentGlWindow());
#else
    return static_cast<Aspect_Drawable>(widget->winId());
#endif
}

Aspect_VKeyMouse toVKeys(Qt::MouseButtons buttons) {
    Aspect_VKeyMouse keys = Aspect_VKeyMouse_NONE;
    if (buttons & Qt::LeftButton) {
        keys |= Aspect_VKeyMouse_LeftButton;
    }
    if (buttons & Qt::MiddleButton) {
        keys |= Aspect_VKeyMouse_MiddleButton;
    }
    if (buttons & Qt::RightButton) {
        keys |= Aspect_VKeyMouse_RightButton;
    }
    return keys;
}

Aspect_VKeyFlags toVFlags(Qt::KeyboardModifiers mods) {
    Aspect_VKeyFlags flags = Aspect_VKeyFlags_NONE;
    if (mods & Qt::ShiftModifier) {
        flags |= Aspect_VKeyFlags_SHIFT;
    }
    if (mods & Qt::ControlModifier) {
        flags |= Aspect_VKeyFlags_CTRL;
    }
    if (mods & Qt::AltModifier) {
        flags |= Aspect_VKeyFlags_ALT;
    }
    return flags;
}

Quantity_Color toOcct(const QColor& c) {
    return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB);
}

Quantity_Color hex(const char* rgb) { return toOcct(QColor(QString::fromLatin1(rgb))); }

} // namespace

OcctViewer::OcctViewer(QWidget* parent) : QOpenGLWidget(parent) {
    Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
    Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display, false);
    driver->ChangeOptions().buffersNoSwap = true;      // Qt swaps buffers
    driver->ChangeOptions().buffersOpaqueAlpha = true; // don't write alpha into Qt's FBO
    driver->ChangeOptions().useSystemBuffer = false;   // always render offscreen

    m_viewer = new V3d_Viewer(driver);
    m_viewer->SetDefaultLights();
    m_viewer->SetLightOn();

    m_context = new AIS_InteractiveContext(m_viewer);
    m_view = m_viewer->CreateView();
    m_view->SetImmediateUpdate(false);
    m_view->ChangeRenderingParams().NbMsaaSamples = 4;
    m_view->ChangeRenderingParams().IsAntialiasingEnabled = true;

    configureLook();
    configureNavigation();

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setMinimumSize(200, 200);

    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(4, 5);
    // Compatibility, not core: core profiles reject line widths above 1 px, and sketch geometry
    // and highlighted edges need thicker lines.
    format.setProfile(QSurfaceFormat::CompatibilityProfile);
    setFormat(format);
}

OcctViewer::~OcctViewer() {
    // Release OCCT GL resources while Qt's context is current.
    makeCurrent();
    Handle(Aspect_DisplayConnection) display = m_viewer->Driver()->GetDisplayConnection();
    m_context->RemoveAll(false);
    m_context.Nullify();
    m_view->Remove();
    m_view.Nullify();
    m_viewer.Nullify();
    doneCurrent();
    display.Nullify();
}

void OcctViewer::configureLook() {
    // Background: light grey-blue gradient (familiar CAD look); themes override via setBackgroundGradient.
    m_view->SetBgGradientColors(hex("#F4F7FA"), hex("#B9C6D3"), Aspect_GradientFillMethod_Vertical, false);
    m_view->SetShadingModel(Graphic3d_TypeOfShadingModel_Phong);
    m_view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_GRAY40, 0.09, V3d_ZBUFFER);

    const Handle(Prs3d_Drawer)& drawer = m_context->DefaultDrawer();
    drawer->SetFaceBoundaryDraw(true);
    drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(hex("#1A1F26"), Aspect_TOL_SOLID, 1.0));
    drawer->SetDeviationAngle(0.2); // ~11 degrees: smoother curved faces than the default
    drawer->ShadingAspect()->SetColor(hex("#B8C2CE"));
    drawer->ShadingAspect()->SetMaterial(Graphic3d_NameOfMaterial_Plastified);
    drawer->SetHiddenLineAspect(new Prs3d_LineAspect(hex("#6B7787"), Aspect_TOL_DASH, 1.0));
    drawer->SetTypeOfHLR(Prs3d_TOH_PolyAlgo);

    // Hover = accent cyan, selection = deep blue.
    const Quantity_Color hover = hex("#22C3E6");
    const Quantity_Color selected = hex("#1E6FD9");
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_Dynamic)->SetColor(hover);
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)->SetColor(hover);
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_Selected)->SetColor(selected);
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)->SetColor(selected);
    // Draw selected faces in the Top layer (depth inherited) so they win over the coincident
    // shaded face instead of z-fighting with it; hidden faces stay hidden.
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_Selected)->SetZLayer(Graphic3d_ZLayerId_Top);
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)->SetZLayer(Graphic3d_ZLayerId_Top);
    // Picked faces highlight filled, not just their outline (display mode 1 = shaded).
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalSelected)->SetDisplayMode(AIS_Shaded);
    m_context->HighlightStyle(Prs3d_TypeOfHighlight_LocalDynamic)->SetDisplayMode(AIS_Shaded);
    m_context->SetPixelTolerance(4);
}

void OcctViewer::configureNavigation() {
    // Buttons and modifier flags are separate enums in OCCT; the gesture map key is their bit union.
    auto key = [](unsigned int button, unsigned int flags = Aspect_VKeyFlags_NONE) { return button | flags; };
    auto& gestures = ChangeMouseGestureMap();
    gestures.Clear();
    gestures.Bind(key(Aspect_VKeyMouse_MiddleButton), AIS_MouseGesture_RotateOrbit);
    gestures.Bind(key(Aspect_VKeyMouse_MiddleButton, Aspect_VKeyFlags_CTRL), AIS_MouseGesture_Pan);
    gestures.Bind(key(Aspect_VKeyMouse_MiddleButton, Aspect_VKeyFlags_SHIFT), AIS_MouseGesture_Zoom);
    gestures.Bind(key(Aspect_VKeyMouse_LeftButton), AIS_MouseGesture_SelectRectangle);
    gestures.Bind(key(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_CTRL), AIS_MouseGesture_SelectRectangle);

    SetRotationMode(AIS_RotationMode_BndBoxActive);
    SetNavigationMode(AIS_NavigationMode_Orbit);
}

NCollection_Vec2<int> OcctViewer::toPixels(const QPointF& logical) const {
    const qreal ratio = devicePixelRatioF();
    return NCollection_Vec2<int>(static_cast<int>(logical.x() * ratio), static_cast<int>(logical.y() * ratio));
}

void OcctViewer::initializeGL() {
    const qreal ratio = devicePixelRatioF();
    const NCollection_Vec2<int> size(static_cast<int>(width() * ratio), static_cast<int>(height() * ratio));
    const Aspect_Drawable native = currentNativeWindow(this);

    Handle(OpenGl_Context) glCtx = new OpenGl_Context();
    if (!glCtx->Init(true)) {
        m_glInfo = QStringLiteral("OpenGL context could not be wrapped by the geometry kernel.");
        spdlog::error("OpenGl_Context::Init failed");
        emit glFailed(m_glInfo);
        return;
    }

    Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(m_view->Window());
    const bool firstTime = window.IsNull();
    if (firstTime) {
        window = new Aspect_NeutralWindow();
        window->SetVirtual(true);
    }
    window->SetNativeHandle(native);
    window->SetSize(size.x(), size.y());
    m_view->SetWindow(window, glCtx->RenderingContext());

    NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString> info;
    m_view->DiagnosticInformation(info, Graphic3d_DiagnosticInfo_Basic);
    QStringList lines;
    for (NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>::Iterator it(info); it.More(); it.Next()) {
        lines << QStringLiteral("%1: %2").arg(QString::fromUtf8(it.Key().ToCString()),
                                               QString::fromUtf8(it.Value().ToCString()));
    }
    m_glInfo = lines.join(QLatin1Char('\n'));
    if (firstTime) {
        spdlog::info("OpenGL initialised:\n{}", m_glInfo.toStdString());
    }
    m_glReady = true;
    emit glReady(m_glInfo);
}

void OcctViewer::paintGL() {
    if (!m_glReady || m_view->Window().IsNull()) {
        return;
    }
    const Aspect_Drawable native = currentNativeWindow(this);
    if (m_view->Window()->NativeHandle() != native) {
        // Qt recreated the window (e.g. moved to another screen) - rebind.
        initializeGL();
        return;
    }

    Handle(OpenGl_Context) glCtx = glContextOf(m_view);
    Handle(OpenGl_FrameBuffer) fbo = glCtx->DefaultFrameBuffer();
    if (fbo.IsNull()) {
        fbo = new QtFrameBuffer();
        glCtx->SetDefaultFrameBuffer(fbo);
    }
    if (!fbo->InitWrapper(glCtx)) {
        spdlog::error("default FBO wrapper creation failed");
        emit glFailed(QStringLiteral("Could not attach the 3D view to the window's frame buffer."));
        m_glReady = false;
        return;
    }

    NCollection_Vec2<int> oldSize;
    const NCollection_Vec2<int> newSize = fbo->GetVPSize();
    Handle(Aspect_NeutralWindow) window = Handle(Aspect_NeutralWindow)::DownCast(m_view->Window());
    window->Size(oldSize.x(), oldSize.y());
    if (newSize != oldSize) {
        window->SetSize(newSize.x(), newSize.y());
        m_view->MustBeResized();
        m_view->Invalidate();
    }

    try {
        m_view->InvalidateImmediate();
        FlushViewEvents(m_context, m_view, true);
    } catch (const Standard_Failure& f) {
        spdlog::error("render error: {}", f.what());
    }

    // Pixel-sized overlay graphics (arrows, glyph offsets) follow zoom and orientation changes.
    const double scale = m_view->Camera()->Scale();
    const gp_Dir direction = m_view->Camera()->Direction();
    if (m_input && (std::abs(scale - m_lastScale) > 1e-9 * std::max(1.0, scale) || !direction.IsEqual(m_lastDirection, 1e-9))) {
        QMetaObject::invokeMethod(this, [this] {
            if (m_input) {
                m_input->viewChanged();
            }
        }, Qt::QueuedConnection);
    }
    m_lastScale = scale;
    m_lastDirection = direction;

    if (!m_firstFrameSent) {
        m_firstFrameSent = true;
        emit firstFrame();
    }
}

void OcctViewer::updateView() { update(); }

void OcctViewer::redraw() {
    // Programmatic changes (display, highlight, clipping) need a full redraw; input handling
    // only needs the immediate layer, which updateView() covers.
    m_view->Invalidate();
    update();
}

void OcctViewer::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& view) {
    AIS_ViewController::handleViewRedraw(ctx, view);
    if (myToAskNextFrame) {
        updateView(); // animation in progress (e.g. smooth zoom)
    }
}

void OcctViewer::OnSelectionChanged(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)&) {
    emit selectionChanged(ctx->NbSelected());
}

// --- Input ---------------------------------------------------------------------------------------

// While an input handler is set, the left and right buttons belong to it; OCCT only sees the middle
// button (navigation).
static Qt::MouseButtons navigationButtons(Qt::MouseButtons buttons, bool handled) {
    return handled ? (buttons & Qt::MiddleButton) : buttons;
}

void OcctViewer::mousePressEvent(QMouseEvent* event) {
    QOpenGLWidget::mousePressEvent(event);
    if (m_input && event->button() != Qt::MiddleButton) {
        setFocus(Qt::MouseFocusReason);
        if (m_input->mousePress(event)) {
            return;
        }
    }
    if (event->button() == Qt::MiddleButton && !m_navigating) {
        m_navigating = true;
        pushViewState(); // rotate/pan/zoom drag starts: remember where we were
    }
    if (UpdateMouseButtons(toPixels(event->position()), toVKeys(navigationButtons(event->buttons(), m_input)),
                           toVFlags(event->modifiers()), false)) {
        updateView();
    }
}

void OcctViewer::mouseDoubleClickEvent(QMouseEvent* event) {
    if (m_input && event->button() == Qt::LeftButton && m_input->mouseDoubleClick(event)) {
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void OcctViewer::mouseReleaseEvent(QMouseEvent* event) {
    QOpenGLWidget::mouseReleaseEvent(event);
    if (m_input && event->button() != Qt::MiddleButton && m_input->mouseRelease(event)) {
        return;
    }
    if (UpdateMouseButtons(toPixels(event->position()), toVKeys(navigationButtons(event->buttons(), m_input)),
                           toVFlags(event->modifiers()), false)) {
        updateView();
    }
    if (event->button() == Qt::LeftButton && m_zoomAreaArmed) {
        m_zoomAreaArmed = false;
        configureNavigation(); // left drag selects again
        unsetCursor();
    }
    if (event->button() == Qt::MiddleButton && m_navigating) {
        m_navigating = false;
        emit navigationFinished();
    }
}

void OcctViewer::mouseMoveEvent(QMouseEvent* event) {
    QOpenGLWidget::mouseMoveEvent(event);
    if (m_input && !(event->buttons() & Qt::MiddleButton)) {
        m_input->mouseMove(event);
    }
    if (UpdateMousePosition(toPixels(event->position()), toVKeys(navigationButtons(event->buttons(), m_input)),
                            toVFlags(event->modifiers()), false)) {
        updateView();
    }
}

void OcctViewer::wheelEvent(QWheelEvent* event) {
    QOpenGLWidget::wheelEvent(event);
    // One history entry per burst of wheel steps.
    if (!m_wheelTimer.isValid() || m_wheelTimer.elapsed() > 700) {
        pushViewState();
    }
    m_wheelTimer.restart();
    double delta = event->angleDelta().y() / 8.0;
    if (m_reverseWheel) {
        delta = -delta;
    }
    if (UpdateZoom(Aspect_ScrollDelta(toPixels(event->position()), delta))) {
        updateView();
    }
}

void OcctViewer::leaveEvent(QEvent* event) {
    QOpenGLWidget::leaveEvent(event);
    // Drop hover highlight when the pointer leaves the viewport.
    if (!m_context.IsNull()) {
        m_context->ClearDetected(false);
        updateView();
    }
}

// --- View history ----------------------------------------------------------------------------------

void OcctViewer::pushViewState() {
    Handle(Graphic3d_Camera) saved = new Graphic3d_Camera();
    saved->Copy(m_view->Camera());
    m_viewHistory.push_back(saved);
    constexpr size_t kMaxHistory = 40;
    if (m_viewHistory.size() > kMaxHistory) {
        m_viewHistory.erase(m_viewHistory.begin());
    }
}

bool OcctViewer::previousView() {
    if (m_viewHistory.empty()) {
        return false;
    }
    m_view->Camera()->Copy(m_viewHistory.back());
    m_viewHistory.pop_back();
    m_view->Invalidate();
    updateView();
    return true;
}

void OcctViewer::armZoomArea() {
    pushViewState();
    m_zoomAreaArmed = true;
    auto& gestures = ChangeMouseGestureMap();
    gestures.UnBind(Aspect_VKeyMouse_LeftButton);
    gestures.Bind(Aspect_VKeyMouse_LeftButton, AIS_MouseGesture_ZoomWindow);
    setCursor(Qt::CrossCursor);
}

// --- Scene ---------------------------------------------------------------------------------------

void OcctViewer::applyStyle(const Handle(AIS_InteractiveObject)& object) {
    const bool shaded = m_style == DisplayStyle::ShadedWithEdges || m_style == DisplayStyle::Shaded;
    const Handle(Prs3d_Drawer)& attrs = object->Attributes();
    attrs->SetFaceBoundaryDraw(m_style == DisplayStyle::ShadedWithEdges);
    if (m_style == DisplayStyle::HiddenLinesVisible) {
        attrs->EnableDrawHiddenLine();
    } else {
        attrs->DisableDrawHiddenLine();
    }
    m_context->SetDisplayMode(object, shaded ? AIS_Shaded : AIS_WireFrame, false);
}

bool OcctViewer::isTemporary(const Handle(AIS_InteractiveObject)& object) const {
    return std::find(m_temporary.begin(), m_temporary.end(), object) != m_temporary.end() ||
           std::find(m_overlays.begin(), m_overlays.end(), object) != m_overlays.end();
}

void OcctViewer::activateSelection(const Handle(AIS_InteractiveObject)& object) {
    m_context->Deactivate(object);
    if (!m_pickable) {
        return;
    }
    if (!m_pickFaces && !m_pickEdges && !m_pickVertices) {
        m_context->Activate(object, 0); // whole body
        return;
    }
    if (m_pickFaces) {
        m_context->Activate(object, AIS_Shape::SelectionMode(TopAbs_FACE));
    }
    if (m_pickEdges) {
        m_context->Activate(object, AIS_Shape::SelectionMode(TopAbs_EDGE));
    }
    if (m_pickVertices) {
        m_context->Activate(object, AIS_Shape::SelectionMode(TopAbs_VERTEX));
    }
}

void OcctViewer::display(const Handle(AIS_InteractiveObject)& object, bool fit) {
    m_context->Display(object, AIS_Shaded, 0, false);
    applyStyle(object);
    m_context->Redisplay(object, false);
    activateSelection(object);

    if (fit) {
        m_view->FitAll(0.08, false);
        m_view->Invalidate();
    }
    redraw();
}

void OcctViewer::remove(const Handle(AIS_InteractiveObject)& object) {
    m_context->Remove(object, false);
    std::erase(m_overlays, object);
    redraw();
}

void OcctViewer::removeAll() {
    m_context->RemoveAll(false);
    m_temporary.clear();
    m_overlays.clear();
    redraw();
}

void OcctViewer::fitAll() {
    pushViewState();
    m_view->FitAll(0.08, false);
    m_view->Invalidate();
    redraw();
}

void OcctViewer::setStandardView(StandardView v) {
    // SetProj takes the direction from the target towards the eye; Y is up except for Top/Bottom.
    double x = 0, y = 0, z = 1;
    double ux = 0, uy = 1, uz = 0;
    switch (v) {
    case StandardView::Front: x = 0, y = 0, z = 1; break;
    case StandardView::Back: x = 0, y = 0, z = -1; break;
    case StandardView::Left: x = -1, y = 0, z = 0; break;
    case StandardView::Right: x = 1, y = 0, z = 0; break;
    case StandardView::Top: x = 0, y = 1, z = 0, ux = 0, uy = 0, uz = -1; break;
    case StandardView::Bottom: x = 0, y = -1, z = 0, ux = 0, uy = 0, uz = 1; break;
    case StandardView::Isometric: x = 1, y = 1, z = 1; break;
    case StandardView::Dimetric: x = 0.9, y = 0.53, z = 1.0; break;
    case StandardView::Trimetric: x = 0.62, y = 0.42, z = 1.0; break;
    }
    pushViewState();
    m_view->SetProj(x, y, z);
    m_view->SetUp(ux, uy, uz);
    m_view->FitAll(0.08, false);
    m_view->Invalidate();
    redraw();
}

void OcctViewer::normalTo(const gp_Dir& n) {
    pushViewState();
    m_view->SetProj(n.X(), n.Y(), n.Z());
    if (std::abs(n.Y()) > 0.99) {
        m_view->SetUp(0.0, 0.0, -n.Y()); // looking along Y: keep -Z/+Z up like Top/Bottom
    } else {
        m_view->SetUp(0.0, 1.0, 0.0);
    }
    m_view->FitAll(0.08, false);
    m_view->Invalidate();
    redraw();
}

void OcctViewer::normalTo(const gp_Ax3& plane, bool fit) {
    pushViewState();
    const gp_Dir& n = plane.Direction();
    const gp_Dir& up = plane.YDirection();
    m_view->SetProj(n.X(), n.Y(), n.Z());
    m_view->SetUp(up.X(), up.Y(), up.Z());
    if (fit) {
        m_view->FitAll(0.08, false);
    }
    m_view->Invalidate();
    redraw();
}

std::optional<gp_Pnt> OcctViewer::pointOnPlane(const QPointF& pos, const gp_Pln& plane) const {
    const NCollection_Vec2<int> px = toPixels(pos);
    double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
    m_view->ConvertWithProj(px.x(), px.y(), x, y, z, vx, vy, vz);
    const gp_Vec ray(vx, vy, vz);
    const gp_Dir& n = plane.Axis().Direction();
    const double denom = ray.Dot(gp_Vec(n));
    if (ray.Magnitude() < 1e-12 || std::abs(denom) < 1e-9 * ray.Magnitude()) {
        return std::nullopt;
    }
    const gp_Pnt from(x, y, z);
    const double t = gp_Vec(from, plane.Location()).Dot(gp_Vec(n)) / denom;
    return from.Translated(ray * t);
}

double OcctViewer::pixelSize() const {
    if (m_view->Window().IsNull()) {
        return 0.1;
    }
    return m_view->Convert(1000) / 1000.0 * devicePixelRatioF();
}

QPointF OcctViewer::toWidget(const gp_Pnt& p) const {
    int xp = 0, yp = 0;
    m_view->Convert(p.X(), p.Y(), p.Z(), xp, yp);
    const qreal ratio = devicePixelRatioF();
    return QPointF(xp / ratio, yp / ratio);
}

void OcctViewer::setModelPickable(bool pickable) {
    if (pickable == m_pickable) {
        return;
    }
    m_pickable = pickable;
    if (!pickable) {
        m_context->ClearDetected(false);
        m_context->ClearSelected(false);
    }
    NCollection_List<Handle(AIS_InteractiveObject)> objects;
    m_context->DisplayedObjects(objects);
    for (const Handle(AIS_InteractiveObject)& object : objects) {
        if (!isTemporary(object)) {
            activateSelection(object);
        }
    }
    redraw();
}

void OcctViewer::showOverlay(const Handle(AIS_InteractiveObject)& object, bool alwaysOnTop) {
    m_context->Display(object, 0, -1, false);
    m_context->Deactivate(object);
    m_context->SetZLayer(object, alwaysOnTop ? Graphic3d_ZLayerId_Topmost : Graphic3d_ZLayerId_Top);
    if (std::find(m_overlays.begin(), m_overlays.end(), object) == m_overlays.end()) {
        m_overlays.push_back(object);
    }
    redraw();
}

void OcctViewer::updateOverlay(const Handle(AIS_InteractiveObject)& object) {
    m_context->Redisplay(object, false);
    m_view->Invalidate();
    update();
}

void OcctViewer::setDisplayStyle(DisplayStyle style) {
    m_style = style;
    const bool hlr = style == DisplayStyle::HiddenLinesRemoved || style == DisplayStyle::HiddenLinesVisible;
    m_context->DefaultDrawer()->SetFaceBoundaryDraw(style == DisplayStyle::ShadedWithEdges);

    NCollection_List<Handle(AIS_InteractiveObject)> objects;
    m_context->DisplayedObjects(objects);
    for (const Handle(AIS_InteractiveObject)& object : objects) {
        if (isTemporary(object)) {
            continue;
        }
        applyStyle(object);
        m_context->Redisplay(object, false);
    }
    m_view->SetComputedMode(hlr);
    m_view->Invalidate();
    redraw();
}

void OcctViewer::setBackgroundGradient(const QColor& top, const QColor& bottom) {
    m_view->SetBgGradientColors(toOcct(top), toOcct(bottom), Aspect_GradientFillMethod_Vertical, false);
    m_view->Invalidate();
    redraw();
}

// --- Selection -------------------------------------------------------------------------------------

std::vector<OcctViewer::Pick> OcctViewer::selection() const {
    std::vector<Pick> picks;
    for (m_context->InitSelected(); m_context->MoreSelected(); m_context->NextSelected()) {
        Pick p;
        p.object = m_context->SelectedInteractive();
        if (m_context->HasSelectedShape()) {
            p.shape = m_context->SelectedShape();
        }
        picks.push_back(p);
    }
    return picks;
}

void OcctViewer::clearSelection() {
    m_context->ClearSelected(false);
    redraw();
    emit selectionChanged(0);
}

void OcctViewer::deselect(int index) {
    int i = 0;
    for (m_context->InitSelected(); m_context->MoreSelected(); m_context->NextSelected(), ++i) {
        if (i == index) {
            const Handle(SelectMgr_EntityOwner) owner = m_context->SelectedOwner();
            m_context->AddOrRemoveSelected(owner, false);
            break;
        }
    }
    redraw();
    emit selectionChanged(m_context->NbSelected());
}

bool OcctViewer::select(const Handle(AIS_InteractiveObject)& object, const TopoDS_Shape& sub) {
    if (object.IsNull() || sub.IsNull()) {
        return false;
    }
    const Handle(SelectMgr_Selection)& sel = object->Selection(AIS_Shape::SelectionMode(sub.ShapeType()));
    if (sel.IsNull()) {
        return false;
    }
    for (const Handle(SelectMgr_SensitiveEntity)& entity : sel->Entities()) {
        const Handle(StdSelect_BRepOwner) owner =
            Handle(StdSelect_BRepOwner)::DownCast(entity->BaseSensitive()->OwnerId());
        if (!owner.IsNull() && owner->Shape().IsSame(sub)) {
            if (!m_context->IsSelected(owner)) {
                m_context->AddOrRemoveSelected(owner, false);
            }
            redraw();
            emit selectionChanged(m_context->NbSelected());
            return true;
        }
    }
    return false;
}

void OcctViewer::setSelectionFilter(bool faces, bool edges, bool vertices) {
    if (faces == m_pickFaces && edges == m_pickEdges && vertices == m_pickVertices) {
        return;
    }
    m_pickFaces = faces, m_pickEdges = edges, m_pickVertices = vertices;
    NCollection_List<Handle(AIS_InteractiveObject)> objects;
    m_context->DisplayedObjects(objects);
    for (const Handle(AIS_InteractiveObject)& object : objects) {
        if (!isTemporary(object)) {
            activateSelection(object);
        }
    }
}

// --- Temporary graphics ----------------------------------------------------------------------------

void OcctViewer::showTemporary(const Handle(AIS_InteractiveObject)& object) {
    m_context->Display(object, false);
    m_context->Deactivate(object);
    m_context->SetZLayer(object, Graphic3d_ZLayerId_Topmost);
    m_temporary.push_back(object);
    redraw();
}

void OcctViewer::clearTemporary() {
    for (const Handle(AIS_InteractiveObject)& object : m_temporary) {
        m_context->Remove(object, false);
    }
    m_temporary.clear();
    redraw();
}

// --- Section view ----------------------------------------------------------------------------------

void OcctViewer::setSection(const gp_Pln& cut, bool cap) {
    // Graphic3d_ClipPlane keeps what lies on the side its normal points to; we keep the material
    // behind the plane instead, so the capped cut face looks towards the viewer.
    const gp_Pln plane(cut.Location(), cut.Axis().Direction().Reversed());
    if (m_section.IsNull()) {
        m_section = new Graphic3d_ClipPlane(plane);
        m_view->AddClipPlane(m_section);
    } else {
        m_section->SetEquation(plane);
    }
    m_section->SetCapping(cap);
    m_section->SetUseObjectMaterial(true); // cut faces take the part's colour...
    m_section->SetCappingHatch(Aspect_HS_DIAGONAL_45);
    m_section->SetCappingHatchOn(); // ...with a hatch, like a drawing section
    m_section->SetOn(true);
    m_view->Invalidate();
    redraw();
}

void OcctViewer::clearSection() {
    if (m_section.IsNull()) {
        return;
    }
    m_view->RemoveClipPlane(m_section);
    m_section.Nullify();
    m_view->Invalidate();
    redraw();
}

} // namespace bl::render
