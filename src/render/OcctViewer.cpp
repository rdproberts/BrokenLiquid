#include "render/OcctViewer.h"

#include <AIS_Shape.hxx>
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
#include <Standard_Failure.hxx>

#include <spdlog/spdlog.h>

#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>

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
    format.setProfile(QSurfaceFormat::CoreProfile);
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

    if (!m_firstFrameSent) {
        m_firstFrameSent = true;
        emit firstFrame();
    }
}

void OcctViewer::updateView() { update(); }

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

void OcctViewer::mousePressEvent(QMouseEvent* event) {
    QOpenGLWidget::mousePressEvent(event);
    if (UpdateMouseButtons(toPixels(event->position()), toVKeys(event->buttons()),
                           toVFlags(event->modifiers()), false)) {
        updateView();
    }
}

void OcctViewer::mouseReleaseEvent(QMouseEvent* event) {
    QOpenGLWidget::mouseReleaseEvent(event);
    if (UpdateMouseButtons(toPixels(event->position()), toVKeys(event->buttons()),
                           toVFlags(event->modifiers()), false)) {
        updateView();
    }
}

void OcctViewer::mouseMoveEvent(QMouseEvent* event) {
    QOpenGLWidget::mouseMoveEvent(event);
    if (UpdateMousePosition(toPixels(event->position()), toVKeys(event->buttons()),
                            toVFlags(event->modifiers()), false)) {
        updateView();
    }
}

void OcctViewer::wheelEvent(QWheelEvent* event) {
    QOpenGLWidget::wheelEvent(event);
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

void OcctViewer::display(const Handle(AIS_InteractiveObject)& object, bool fit) {
    m_context->Display(object, AIS_Shaded, 0, false);
    applyStyle(object);
    m_context->Redisplay(object, false);

    // Pick faces, edges and vertices (mode numbers from AIS_Shape).
    m_context->Deactivate(object);
    m_context->Activate(object, AIS_Shape::SelectionMode(TopAbs_FACE));
    m_context->Activate(object, AIS_Shape::SelectionMode(TopAbs_EDGE));
    m_context->Activate(object, AIS_Shape::SelectionMode(TopAbs_VERTEX));

    if (fit) {
        fitAll();
    } else {
        updateView();
    }
}

void OcctViewer::removeAll() {
    m_context->RemoveAll(false);
    updateView();
}

void OcctViewer::fitAll() {
    m_view->FitAll(0.08, false);
    m_view->Invalidate();
    updateView();
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
    m_view->SetProj(x, y, z);
    m_view->SetUp(ux, uy, uz);
    fitAll();
}

void OcctViewer::setDisplayStyle(DisplayStyle style) {
    m_style = style;
    const bool hlr = style == DisplayStyle::HiddenLinesRemoved || style == DisplayStyle::HiddenLinesVisible;
    m_context->DefaultDrawer()->SetFaceBoundaryDraw(style == DisplayStyle::ShadedWithEdges);

    NCollection_List<Handle(AIS_InteractiveObject)> objects;
    m_context->DisplayedObjects(objects);
    for (const Handle(AIS_InteractiveObject)& object : objects) {
        applyStyle(object);
        m_context->Redisplay(object, false);
    }
    m_view->SetComputedMode(hlr);
    m_view->Invalidate();
    updateView();
}

void OcctViewer::setBackgroundGradient(const QColor& top, const QColor& bottom) {
    m_view->SetBgGradientColors(toOcct(top), toOcct(bottom), Aspect_GradientFillMethod_Vertical, false);
    m_view->Invalidate();
    updateView();
}

} // namespace bl::render
