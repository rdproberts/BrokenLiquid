#include "ui/MainWindow.h"

#include "bl/Version.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "kernel/ShapeInfo.h"
#include "ui/CommandBar.h"
#include "ui/CommandRegistry.h"
#include "ui/FeatureTreePanel.h"
#include "ui/Icons.h"
#include "ui/ResourcePane.h"
#include "ui/ViewHud.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFPrs_AISObject.hxx>

#include <spdlog/spdlog.h>

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCompleter>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QStringListModel>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

namespace bl::ui {
namespace {

bool isStepFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("step") || suffix == QLatin1String("stp");
}

QString formatNumber(double v, int decimals = 3) { return QString::number(v, 'f', decimals); }

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_registry(std::make_unique<CommandRegistry>(this)) {
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QStringLiteral("BrokenLiquid"));
    setAcceptDrops(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
    resize(1500, 920);

    m_viewer = new render::OcctViewer();
    registerCommands();
    buildCentral();
    buildMenus();
    buildQuickAccess();
    buildCommandBar();
    buildDocks();
    buildStatusBar();
    restoreLayout();

    const QString theme = QString::fromStdString(Settings::global().get<std::string>("ui.theme", "light"));
    setTheme(theme == QLatin1String("dark") ? ThemeKind::Dark : ThemeKind::Light);

    connect(m_viewer, &render::OcctViewer::glReady, this, [this] { m_viewer->setStandardView(render::StandardView::Isometric); });
    connect(m_viewer, &render::OcctViewer::glFailed, this, [this](const QString& error) {
        QMessageBox::critical(this, tr("Graphics problem"),
                              tr("The 3D view could not start.\n\n%1\n\n"
                                 "Updating the graphics driver usually fixes this.")
                                  .arg(error));
    });
    connect(m_viewer, &render::OcctViewer::firstFrame, this, [this] {
        m_firstFrame = true;
        emit ready();
    });
    connect(m_viewer, &render::OcctViewer::selectionChanged, this, [this](int count) {
        m_statusSelection->setText(count > 0 ? tr("%n selected", nullptr, count) : QString());
    });
}

MainWindow::~MainWindow() = default;

// --- Commands --------------------------------------------------------------------------------------

void MainWindow::registerCommands() {
    CommandRegistry& r = *m_registry;
    using render::DisplayStyle;
    using render::StandardView;

    auto planned = [&r](const char* id, const QString& label, const char* phase, const char* iconName = nullptr,
                        const QKeySequence& key = {}) {
        CommandSpec s;
        s.id = QString::fromLatin1(id);
        s.label = label;
        s.icon = iconName ? QString::fromLatin1(iconName) : QString();
        s.shortcut = key;
        s.plannedFor = QString::fromLatin1(phase);
        r.add(s);
    };
    auto command = [&r](const char* id, const QString& label, const char* iconName, const QKeySequence& key,
                        const QString& tip, std::function<void()> fn) {
        CommandSpec s;
        s.id = QString::fromLatin1(id);
        s.label = label;
        s.icon = QString::fromLatin1(iconName);
        s.shortcut = key;
        s.tooltip = tip;
        return r.add(s, std::move(fn));
    };

    // File
    command("file.open", tr("Open…"), "open", QKeySequence::Open, tr("Open or import a STEP file"), [this] {
        const QString startDir = QString::fromStdString(Settings::global().get<std::string>("files.lastDir", ""));
        const QString path = QFileDialog::getOpenFileName(this, tr("Open"), startDir,
                                                          tr("STEP files (*.step *.stp);;All files (*)"));
        if (!path.isEmpty()) {
            openFile(path);
        }
    });
    planned("file.new.part", tr("New Part"), "Phase 3", "new-part", QKeySequence::New);
    planned("file.new.assembly", tr("New Assembly"), "Phase 4", "new-assembly");
    planned("file.new.drawing", tr("New Drawing"), "Phase 5", "new-drawing");
    planned("file.save", tr("Save"), "Phase 3", "save", QKeySequence::Save);
    planned("file.print", tr("Print…"), "Phase 5", "print", QKeySequence::Print);
    command("file.exit", tr("Exit"), "exit", QKeySequence(), tr("Close BrokenLiquid"), [this] { close(); });

    // Edit / tools
    planned("edit.undo", tr("Undo"), "Phase 1", "undo", QKeySequence::Undo);
    planned("edit.redo", tr("Redo"), "Phase 1", "redo", QKeySequence(Qt::CTRL | Qt::Key_Y));
    planned("edit.rebuild", tr("Rebuild"), "Phase 3", "rebuild", QKeySequence(Qt::CTRL | Qt::Key_B));
    planned("tools.options", tr("Options…"), "Phase 1", "options");

    // View
    command("view.fit", tr("Zoom to Fit"), "view-fit", QKeySequence(Qt::Key_F), tr("Fit the model in the window"),
            [this] { m_viewer->fitAll(); });
    planned("view.zoomArea", tr("Zoom to Area"), "Phase 1", "view-zoom-area");
    planned("view.previous", tr("Previous View"), "Phase 1", "view-previous");
    planned("view.section", tr("Section View"), "Phase 1", "view-section");

    struct ViewDef {
        const char* id;
        QString label;
        const char* icon;
        int key;
        StandardView view;
    };
    const ViewDef views[] = {
        {"view.front", tr("Front"), "view-front", Qt::Key_1, StandardView::Front},
        {"view.back", tr("Back"), "view-back", Qt::Key_2, StandardView::Back},
        {"view.left", tr("Left"), "view-left", Qt::Key_3, StandardView::Left},
        {"view.right", tr("Right"), "view-right", Qt::Key_4, StandardView::Right},
        {"view.top", tr("Top"), "view-top", Qt::Key_5, StandardView::Top},
        {"view.bottom", tr("Bottom"), "view-bottom", Qt::Key_6, StandardView::Bottom},
        {"view.isometric", tr("Isometric"), "view-iso", Qt::Key_7, StandardView::Isometric},
    };
    for (const ViewDef& v : views) {
        const StandardView sv = v.view;
        command(v.id, v.label, v.icon, QKeySequence(Qt::CTRL | v.key), tr("%1 view").arg(v.label),
                [this, sv] { m_viewer->setStandardView(sv); });
    }
    command("view.dimetric", tr("Dimetric"), "view-iso", QKeySequence(), tr("Dimetric view"),
            [this] { m_viewer->setStandardView(StandardView::Dimetric); });
    command("view.trimetric", tr("Trimetric"), "view-iso", QKeySequence(), tr("Trimetric view"),
            [this] { m_viewer->setStandardView(StandardView::Trimetric); });
    command("view.normalTo", tr("Normal To"), "view-normal", QKeySequence(Qt::CTRL | Qt::Key_8),
            tr("Look straight at the selected planar face"), [this] {
                const Handle(AIS_InteractiveContext)& ctx = m_viewer->context();
                for (ctx->InitSelected(); ctx->MoreSelected(); ctx->NextSelected()) {
                    if (!ctx->HasSelectedShape() || ctx->SelectedShape().ShapeType() != TopAbs_FACE) {
                        continue;
                    }
                    const TopoDS_Face face = TopoDS::Face(ctx->SelectedShape());
                    BRepAdaptor_Surface surface(face);
                    if (surface.GetType() != GeomAbs_Plane) {
                        continue;
                    }
                    gp_Dir n = surface.Plane().Axis().Direction();
                    if (face.Orientation() == TopAbs_REVERSED) {
                        n.Reverse();
                    }
                    const Handle(V3d_View)& view = m_viewer->view();
                    view->SetProj(n.X(), n.Y(), n.Z());
                    view->SetUp(std::abs(n.Y()) > 0.99 ? 0.0 : 0.0, std::abs(n.Y()) > 0.99 ? 0.0 : 1.0,
                                std::abs(n.Y()) > 0.99 ? -n.Y() : 0.0);
                    m_viewer->fitAll();
                    return;
                }
                statusBar()->showMessage(tr("Select a flat face first."), 4000);
            });

    struct StyleDef {
        const char* id;
        QString label;
        const char* icon;
        DisplayStyle style;
    };
    const StyleDef styles[] = {
        {"view.style.shadedEdges", tr("Shaded With Edges"), "style-shaded-edges", DisplayStyle::ShadedWithEdges},
        {"view.style.shaded", tr("Shaded"), "style-shaded", DisplayStyle::Shaded},
        {"view.style.hlr", tr("Hidden Lines Removed"), "style-hlr", DisplayStyle::HiddenLinesRemoved},
        {"view.style.hlv", tr("Hidden Lines Visible"), "style-hlv", DisplayStyle::HiddenLinesVisible},
        {"view.style.wireframe", tr("Wireframe"), "style-wireframe", DisplayStyle::Wireframe},
    };
    auto* styleGroup = new QActionGroup(this);
    for (const StyleDef& s : styles) {
        const DisplayStyle ds = s.style;
        QAction* a = command(s.id, s.label, s.icon, QKeySequence(), s.label, [this, ds] { m_viewer->setDisplayStyle(ds); });
        a->setCheckable(true);
        a->setChecked(ds == DisplayStyle::ShadedWithEdges);
        styleGroup->addAction(a);
    }

    auto* themeGroup = new QActionGroup(this);
    QAction* light = command("view.theme.light", tr("Light Theme"), "theme-light", QKeySequence(), tr("Light theme"),
                             [this] { setTheme(ThemeKind::Light); });
    QAction* dark = command("view.theme.dark", tr("Dark Theme"), "theme-dark", QKeySequence(), tr("Dark theme"),
                            [this] { setTheme(ThemeKind::Dark); });
    for (QAction* a : {light, dark}) {
        a->setCheckable(true);
        themeGroup->addAction(a);
    }

    // Features (Phase 3)
    const struct {
        const char* id;
        QString label;
        const char* icon;
    } features[] = {
        {"features.extrude", tr("Extruded Boss/Base"), "extrude"},
        {"features.revolve", tr("Revolved Boss/Base"), "revolve"},
        {"features.sweep", tr("Swept Boss/Base"), "sweep"},
        {"features.loft", tr("Lofted Boss/Base"), "loft"},
        {"features.extrudeCut", tr("Extruded Cut"), "extrude-cut"},
        {"features.revolveCut", tr("Revolved Cut"), "revolve-cut"},
        {"features.sweepCut", tr("Swept Cut"), "sweep-cut"},
        {"features.loftCut", tr("Lofted Cut"), "loft-cut"},
        {"features.hole", tr("Hole Builder"), "hole"},
        {"features.fillet", tr("Fillet"), "fillet"},
        {"features.chamfer", tr("Chamfer"), "chamfer"},
        {"features.linearPattern", tr("Linear Pattern"), "linear-pattern"},
        {"features.circularPattern", tr("Circular Pattern"), "circular-pattern"},
        {"features.mirror", tr("Mirror"), "mirror"},
        {"features.shell", tr("Shell"), "shell"},
        {"features.draft", tr("Draft"), "draft"},
        {"features.plane", tr("Plane"), "plane"},
        {"features.axis", tr("Axis"), "axis"},
        {"features.point", tr("Point"), "point"},
        {"features.csys", tr("Coordinate System"), "csys"},
        {"features.combine", tr("Combine"), "combine"},
        {"features.moveCopy", tr("Move/Copy Body"), "move-copy"},
        {"features.equations", tr("Equations"), "equations"},
    };
    for (const auto& f : features) {
        planned(f.id, f.label, "Phase 3 (Part modelling)", f.icon);
    }

    // Sketch (Phase 2)
    const struct {
        const char* id;
        QString label;
        const char* icon;
    } sketch[] = {
        {"sketch.sketch", tr("Sketch"), "sketch"},
        {"sketch.dimension", tr("Smart Dimension"), "smart-dimension"},
        {"sketch.line", tr("Line"), "line"},
        {"sketch.centerline", tr("Centerline"), "centerline"},
        {"sketch.rectangle", tr("Corner Rectangle"), "rectangle"},
        {"sketch.centerRectangle", tr("Center Rectangle"), "center-rectangle"},
        {"sketch.slot", tr("Straight Slot"), "slot"},
        {"sketch.circle", tr("Circle"), "circle"},
        {"sketch.arc", tr("Centerpoint Arc"), "arc"},
        {"sketch.tangentArc", tr("Tangent Arc"), "tangent-arc"},
        {"sketch.polygon", tr("Polygon"), "polygon"},
        {"sketch.point", tr("Point"), "sketch-point"},
        {"sketch.fillet", tr("Sketch Fillet"), "sketch-fillet"},
        {"sketch.offset", tr("Offset Entities"), "offset"},
        {"sketch.convert", tr("Convert Entities"), "convert"},
        {"sketch.trim", tr("Trim Entities"), "trim"},
        {"sketch.extend", tr("Extend Entities"), "extend"},
        {"sketch.mirror", tr("Mirror Entities"), "sketch-mirror"},
        {"sketch.relation", tr("Add Relation"), "relation"},
        {"sketch.construction", tr("Construction Geometry"), "construction"},
    };
    for (const auto& s : sketch) {
        planned(s.id, s.label, "Phase 2 (Sketcher)", s.icon);
    }

    // Evaluate
    planned("evaluate.measure", tr("Measure"), "Phase 1", "measure");
    command("evaluate.massProps", tr("Mass Properties"), "mass-properties", QKeySequence(),
            tr("Volume, surface area and size of the open model"), [this] { showMassProperties(); });
    command("evaluate.check", tr("Check Geometry"), "check", QKeySequence(),
            tr("Check the open model for geometry errors"), [this] { showMassProperties(); });
    planned("evaluate.issues", tr("Rebuild Issues"), "Phase 3", "issues");

    // Help
    command("help.about", tr("About BrokenLiquid"), "about", QKeySequence(), tr("Version and licences"),
            [this] { showAbout(); });
    command("help.graphics", tr("Graphics Information"), "graphics", QKeySequence(),
            tr("OpenGL driver details"), [this] { showGlInfo(); });
    command("help.logs", tr("Open Log Folder"), "logs", QKeySequence(), tr("Show BrokenLiquid's log files"), [] {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QString::fromStdWString(paths::ensure(paths::logDir()).wstring())));
    });
}

// --- Layout ------------------------------------------------------------------------------------------

void MainWindow::buildMenus() {
    CommandRegistry& r = *m_registry;
    auto addAll = [&r](QMenu* menu, std::initializer_list<const char*> ids) {
        for (const char* id : ids) {
            if (id == nullptr) {
                menu->addSeparator();
            } else if (QAction* a = r.action(QString::fromLatin1(id))) {
                menu->addAction(a);
            }
        }
    };

    QMenu* file = menuBar()->addMenu(tr("&File"));
    addAll(file, {"file.new.part", "file.new.assembly", "file.new.drawing", nullptr, "file.open", "file.save",
                  nullptr, "file.print", nullptr, "file.exit"});

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    addAll(edit, {"edit.undo", "edit.redo", nullptr, "edit.rebuild"});

    QMenu* view = menuBar()->addMenu(tr("&View"));
    addAll(view, {"view.fit", "view.zoomArea", "view.previous", nullptr});
    QMenu* orient = view->addMenu(icon(QStringLiteral("view-orientation"), tr("Orientation")), tr("Orientation"));
    addAll(orient, {"view.front", "view.back", "view.left", "view.right", "view.top", "view.bottom", nullptr,
                    "view.isometric", "view.dimetric", "view.trimetric", nullptr, "view.normalTo"});
    QMenu* style = view->addMenu(icon(QStringLiteral("view-style"), tr("Display Style")), tr("Display Style"));
    addAll(style, {"view.style.shadedEdges", "view.style.shaded", "view.style.hlr", "view.style.hlv",
                   "view.style.wireframe"});
    addAll(view, {"view.section", nullptr});
    QMenu* theme = view->addMenu(tr("Theme"));
    addAll(theme, {"view.theme.light", "view.theme.dark"});

    QMenu* insert = menuBar()->addMenu(tr("&Insert"));
    QMenu* boss = insert->addMenu(tr("Boss/Base"));
    addAll(boss, {"features.extrude", "features.revolve", "features.sweep", "features.loft"});
    QMenu* cut = insert->addMenu(tr("Cut"));
    addAll(cut, {"features.extrudeCut", "features.revolveCut", "features.sweepCut", "features.loftCut"});
    QMenu* feats = insert->addMenu(tr("Features"));
    addAll(feats, {"features.fillet", "features.chamfer", "features.hole", "features.shell", "features.draft",
                   nullptr, "features.combine", "features.moveCopy"});
    QMenu* pattern = insert->addMenu(tr("Pattern/Mirror"));
    addAll(pattern, {"features.linearPattern", "features.circularPattern", "features.mirror"});
    QMenu* ref = insert->addMenu(tr("Reference Geometry"));
    addAll(ref, {"features.plane", "features.axis", "features.point", "features.csys"});
    addAll(insert, {nullptr, "sketch.sketch", "features.equations"});

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    addAll(tools, {"evaluate.measure", "evaluate.massProps", "evaluate.check", nullptr, "tools.options"});

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    addAll(help, {"help.graphics", "help.logs", nullptr, "help.about"});
}

void MainWindow::buildQuickAccess() {
    auto* bar = new QToolBar(tr("Quick Access"), this);
    bar->setObjectName(QStringLiteral("QuickAccess"));
    bar->setMovable(false);
    bar->setIconSize(QSize(18, 18));
    for (const char* id : {"file.new.part", "file.open", "file.save", "file.print"}) {
        bar->addAction(m_registry->action(QString::fromLatin1(id)));
    }
    bar->addSeparator();
    for (const char* id : {"edit.undo", "edit.redo", "edit.rebuild", "tools.options"}) {
        bar->addAction(m_registry->action(QString::fromLatin1(id)));
    }

    auto* spacer = new QWidget(bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    // Command search (PLAN.md 7.6: Ctrl+K)
    m_search = new QLineEdit(bar);
    m_search->setObjectName(QStringLiteral("CommandSearch"));
    m_search->setPlaceholderText(tr("Search commands  (Ctrl+K)"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(260);
    QStringList labels;
    for (QAction* a : m_registry->actions()) {
        labels << a->text();
    }
    labels.removeDuplicates();
    auto* completer = new QCompleter(labels, m_search);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_search->setCompleter(completer);
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this, [this](const QString& text) {
        for (QAction* a : m_registry->actions()) {
            if (a->text() == text) {
                if (a->isEnabled()) {
                    a->trigger();
                } else {
                    statusBar()->showMessage(a->toolTip().section(QLatin1Char('\n'), -1), 5000);
                }
                break;
            }
        }
        QTimer::singleShot(0, m_search, &QLineEdit::clear);
    });
    bar->addWidget(m_search);
    auto* focusSearch = new QAction(this);
    focusSearch->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    connect(focusSearch, &QAction::triggered, this, [this] {
        m_search->setFocus();
        m_search->selectAll();
    });
    addAction(focusSearch);

    addToolBar(Qt::TopToolBarArea, bar);
    addToolBarBreak(Qt::TopToolBarArea);
}

void MainWindow::buildCommandBar() {
    m_commandBar = new CommandBar(*m_registry, this);
    const int features = m_commandBar->addTab(tr("Features"));
    m_commandBar->addGroup(features, {"features.extrude", "features.revolve", "features.sweep", "features.loft"});
    m_commandBar->addGroup(features, {"features.extrudeCut", "features.revolveCut", "features.hole"});
    m_commandBar->addFlyout(features, tr("Fillet"), {"features.fillet", "features.chamfer"});
    m_commandBar->addFlyout(features, tr("Pattern"),
                            {"features.linearPattern", "features.circularPattern", "features.mirror"});
    m_commandBar->addGroup(features, {"features.shell", "features.draft"});
    m_commandBar->addFlyout(features, tr("Reference"),
                            {"features.plane", "features.axis", "features.point", "features.csys"});
    m_commandBar->addGroup(features, {"features.combine", "features.equations"});

    const int sketch = m_commandBar->addTab(tr("Sketch"));
    m_commandBar->addGroup(sketch, {"sketch.sketch", "sketch.dimension"});
    m_commandBar->addFlyout(sketch, tr("Line"), {"sketch.line", "sketch.centerline"});
    m_commandBar->addFlyout(sketch, tr("Rectangle"), {"sketch.rectangle", "sketch.centerRectangle"});
    m_commandBar->addGroup(sketch, {"sketch.slot", "sketch.circle"});
    m_commandBar->addFlyout(sketch, tr("Arc"), {"sketch.arc", "sketch.tangentArc"});
    m_commandBar->addGroup(sketch, {"sketch.polygon", "sketch.point"});
    m_commandBar->addGroup(sketch, {"sketch.fillet", "sketch.offset", "sketch.convert", "sketch.trim",
                                    "sketch.mirror"});
    m_commandBar->addGroup(sketch, {"sketch.relation", "sketch.construction"});

    const int evaluate = m_commandBar->addTab(tr("Evaluate"));
    m_commandBar->addGroup(evaluate, {"evaluate.measure", "evaluate.massProps", "view.section"});
    m_commandBar->addGroup(evaluate, {"evaluate.check", "evaluate.issues"});

    auto* bar = new QToolBar(tr("Command Bar"), this);
    bar->setObjectName(QStringLiteral("CommandBarToolBar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->addWidget(m_commandBar);
    addToolBar(Qt::TopToolBarArea, bar);
}

void MainWindow::buildCentral() {
    auto* container = new QWidget(this);
    container->setObjectName(QStringLiteral("GraphicsArea"));
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    m_viewer->setParent(container);
    layout->addWidget(m_viewer);

    m_hud = new ViewHud(*m_registry, container);
    m_hud->addCommand(QStringLiteral("view.fit"));
    m_hud->addCommand(QStringLiteral("view.zoomArea"));
    m_hud->addCommand(QStringLiteral("view.previous"));
    m_hud->addSeparator();
    m_hud->addCommand(QStringLiteral("view.section"));
    m_hud->addFlyout(QStringLiteral("view-orientation"), tr("View Orientation"),
                     {QStringLiteral("view.front"), QStringLiteral("view.back"), QStringLiteral("view.left"),
                      QStringLiteral("view.right"), QStringLiteral("view.top"), QStringLiteral("view.bottom"),
                      QStringLiteral("view.isometric"), QStringLiteral("view.dimetric"),
                      QStringLiteral("view.trimetric"), QStringLiteral("view.normalTo")});
    m_hud->addFlyout(QStringLiteral("view-style"), tr("Display Style"),
                     {QStringLiteral("view.style.shadedEdges"), QStringLiteral("view.style.shaded"),
                      QStringLiteral("view.style.hlr"), QStringLiteral("view.style.hlv"),
                      QStringLiteral("view.style.wireframe")});

    setCentralWidget(container);
}

void MainWindow::buildDocks() {
    // Left: Feature Tree / Property Panel / Display (tabs, like the familiar manager pane)
    m_tree = new FeatureTreePanel(this);
    m_managerTabs = new QTabWidget(this);
    m_managerTabs->setObjectName(QStringLiteral("ManagerTabs"));
    m_managerTabs->setDocumentMode(true);
    m_managerTabs->setIconSize(QSize(18, 18));
    m_managerTabs->addTab(m_tree, icon(QStringLiteral("tab-tree"), tr("Feature Tree")), QString());
    m_managerTabs->setTabToolTip(0, tr("Feature Tree"));
    auto* property = new QLabel(tr("The Property Panel opens here when a command needs input.\n\nComing in Phase 1."),
                                m_managerTabs);
    property->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    property->setWordWrap(true);
    property->setMargin(10);
    property->setObjectName(QStringLiteral("PaneHint"));
    m_managerTabs->addTab(property, icon(QStringLiteral("tab-property"), tr("Property Panel")), QString());
    m_managerTabs->setTabToolTip(1, tr("Property Panel"));

    auto* left = new QDockWidget(tr("Feature Tree"), this);
    left->setObjectName(QStringLiteral("ManagerDock"));
    left->setWidget(m_managerTabs);
    left->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    left->setTitleBarWidget(new QWidget(left)); // no title bar, like the familiar layout
    addDockWidget(Qt::LeftDockWidgetArea, left);
    resizeDocks({left}, {270}, Qt::Horizontal);

    // Right: Resource Pane
    m_resources = new ResourcePane(*m_registry, this);
    connect(m_resources, &ResourcePane::openFileRequested, this, &MainWindow::openFile);
    auto* right = new QDockWidget(tr("Resources"), this);
    right->setObjectName(QStringLiteral("ResourceDock"));
    right->setWidget(m_resources);
    right->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                       QDockWidget::DockWidgetClosable);
    right->setTitleBarWidget(new QWidget(right));
    addDockWidget(Qt::RightDockWidgetArea, right);
    resizeDocks({right}, {250}, Qt::Horizontal);
}

void MainWindow::buildStatusBar() {
    QStatusBar* bar = statusBar();
    m_statusSelection = new QLabel(bar);
    m_statusState = new QLabel(tr("Phase 0 preview · viewer only"), bar);
    m_statusState->setObjectName(QStringLiteral("StatusState"));
    m_statusUnits = new QLabel(QStringLiteral("MMGS"), bar);
    m_statusUnits->setToolTip(tr("Unit system: millimetre, gram, second"));
    bar->addPermanentWidget(m_statusSelection);
    bar->addPermanentWidget(m_statusState);
    bar->addPermanentWidget(m_statusUnits);
    bar->showMessage(tr("Ready. Open a STEP file to view it."));
}

void MainWindow::setTheme(ThemeKind kind) {
    applyTheme(kind);
    const ThemeColors c = themeColors(kind);
    m_viewer->setBackgroundGradient(c.viewportTop, c.viewportBottom);
    if (QAction* a = m_registry->action(kind == ThemeKind::Dark ? QStringLiteral("view.theme.dark")
                                                                : QStringLiteral("view.theme.light"))) {
        a->setChecked(true);
    }
    Settings::global().set("ui.theme", std::string(kind == ThemeKind::Dark ? "dark" : "light"));
    m_hud->update();
}

// --- Files -----------------------------------------------------------------------------------------

void MainWindow::openFile(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    if (!isStepFile(path)) {
        QMessageBox::information(this, tr("Open"),
                                 tr("Phase 0 can open STEP files (.step, .stp) only.\n\n%1").arg(path));
        return;
    }
    if (m_importBusy) {
        statusBar()->showMessage(tr("Still importing the previous file…"), 3000);
        return;
    }
    m_importBusy = true;
    Settings::global().set("files.lastDir", QFileInfo(path).absolutePath().toStdString());
    statusBar()->showMessage(tr("Importing %1…").arg(QFileInfo(path).fileName()));
    QApplication::setOverrideCursor(Qt::BusyCursor);

    auto* watcher = new QFutureWatcher<kernel::ImportResult>(this);
    connect(watcher, &QFutureWatcher<kernel::ImportResult>::finished, this, [this, watcher, path] {
        QApplication::restoreOverrideCursor();
        m_importBusy = false;
        showImport(watcher->result(), path);
        watcher->deleteLater();
    });
    const std::filesystem::path file = path.toStdWString();
    watcher->setFuture(QtConcurrent::run([file] { return kernel::importStep(file); }));
}

void MainWindow::showImport(const kernel::ImportResult& result, const QString& path) {
    if (!result.ok) {
        statusBar()->showMessage(tr("Import failed."), 5000);
        QMessageBox::warning(this, tr("Import failed"),
                             tr("%1\n\n%2").arg(QFileInfo(path).fileName(), QString::fromStdString(result.error)));
        emit importFinished(false);
        return;
    }

    m_viewer->removeAll();
    m_tree->clearBodies();
    m_model = result;
    m_modelPath = path;

    const QString name = QFileInfo(path).completeBaseName();
    int index = 0;
    for (const kernel::ImportedRoot& root : m_model.roots) {
        ++index;
        Handle(XCAFPrs_AISObject) object = new XCAFPrs_AISObject(root.label);
        m_viewer->display(object, false);
        const kernel::ShapeStats s = kernel::computeStats(root.shape);
        // Exporters often write their own name as the product name; the file name is more useful then.
        QString bodyName = QString::fromStdString(root.name).trimmed();
        if (bodyName.isEmpty() || bodyName.startsWith(QLatin1String("Open CASCADE STEP translator"))) {
            bodyName = m_model.roots.size() == 1 ? name : QStringLiteral("%1-%2").arg(name).arg(index);
        }
        m_tree->addImportedBody(bodyName,
                                tr("%1 solid(s), %2 faces, %3 edges").arg(s.solids).arg(s.faces).arg(s.edges));
    }
    m_viewer->fitAll();

    m_tree->setDocumentName(name);
    setWindowTitle(QStringLiteral("%1 - BrokenLiquid").arg(name));
    statusBar()->showMessage(
        tr("Opened %1 in %2 s").arg(QFileInfo(path).fileName(), QString::number(result.seconds, 'f', 2)), 8000);
    emit importFinished(true);
}

void MainWindow::showMassProperties() {
    if (m_model.roots.empty()) {
        QMessageBox::information(this, tr("Mass Properties"), tr("Open a model first."));
        return;
    }
    BRep_Builder builder;
    TopoDS_Compound all;
    builder.MakeCompound(all);
    for (const kernel::ImportedRoot& root : m_model.roots) {
        builder.Add(all, root.shape);
    }
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const kernel::ShapeStats s = kernel::computeStats(all);
    QApplication::restoreOverrideCursor();

    const double sx = s.bbox[3] - s.bbox[0], sy = s.bbox[4] - s.bbox[1], sz = s.bbox[5] - s.bbox[2];
    const QString text =
        tr("<table cellspacing='6'>"
           "<tr><td>Volume</td><td align='right'><b>%1</b> mm³</td></tr>"
           "<tr><td>Surface area</td><td align='right'><b>%2</b> mm²</td></tr>"
           "<tr><td>Bounding box</td><td align='right'><b>%3 × %4 × %5</b> mm</td></tr>"
           "<tr><td>Solids / faces / edges</td><td align='right'>%6 / %7 / %8</td></tr>"
           "<tr><td>Geometry check</td><td align='right'>%9</td></tr>"
           "</table><p style='color:gray'>Mass needs a material (Phase 3).</p>")
            .arg(formatNumber(s.volume), formatNumber(s.area), formatNumber(sx, 2), formatNumber(sy, 2),
                 formatNumber(sz, 2))
            .arg(s.solids)
            .arg(s.faces)
            .arg(s.edges)
            .arg(s.valid ? tr("<span style='color:#1b8a3a'>No errors</span>")
                         : tr("<span style='color:#c0392b'>Problems found</span>"));
    QMessageBox box(this);
    box.setWindowTitle(tr("Mass Properties"));
    box.setTextFormat(Qt::RichText);
    box.setText(text);
    box.exec();
}

void MainWindow::showGlInfo() {
    QMessageBox box(this);
    box.setWindowTitle(tr("Graphics Information"));
    box.setText(m_viewer->isGlReady() ? m_viewer->glInfo() : tr("OpenGL is not initialised."));
    box.exec();
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this, tr("About BrokenLiquid"),
        tr("<h3>BrokenLiquid %1</h3>"
           "<p>Free, local, parametric 3D CAD.<br>Build %2 · Phase 0 preview</p>"
           "<p>Built with Qt %3 (LGPL-3.0) and Open CASCADE Technology (LGPL-2.1 with exception).<br>"
           "Licence texts ship in the <i>licenses</i> folder.</p>")
            .arg(QString::fromLatin1(kVersion), QString::fromLatin1(kBuildId), QString::fromLatin1(qVersion())));
}

// --- Window events / persistence ---------------------------------------------------------------------

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (isStepFile(url.toLocalFile())) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        if (isStepFile(url.toLocalFile())) {
            openFile(url.toLocalFile());
            return;
        }
    }
}

void MainWindow::restoreLayout() {
    const auto geometry = Settings::global().get<std::string>("ui.mainWindow.geometry", "");
    const auto state = Settings::global().get<std::string>("ui.mainWindow.state", "");
    if (!geometry.empty()) {
        restoreGeometry(QByteArray::fromBase64(QByteArray::fromStdString(geometry)));
    }
    if (!state.empty()) {
        restoreState(QByteArray::fromBase64(QByteArray::fromStdString(state)));
    }
}

void MainWindow::saveLayout() {
    Settings::global().set("ui.mainWindow.geometry", saveGeometry().toBase64().toStdString());
    Settings::global().set("ui.mainWindow.state", saveState().toBase64().toStdString());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveLayout();
    Settings::global().save();
    event->accept();
}

void MainWindow::runSmokeTest(const QString& pngPath) {
    auto finish = [this, pngPath] {
        QTimer::singleShot(800, this, [this, pngPath] {
            const QImage image = grab().toImage();
            const bool ok = !image.isNull() && image.save(pngPath);
            spdlog::info("smoke test screenshot {} -> {}", pngPath.toStdString(), ok ? "saved" : "FAILED");
            QApplication::exit(ok ? 0 : 2);
        });
    };
    auto check = [this, finish] {
        if (m_firstFrame && !m_importBusy) {
            finish();
        }
    };
    connect(this, &MainWindow::ready, this, check);
    connect(this, &MainWindow::importFinished, this, check);
    QTimer::singleShot(60000, this, [] {
        spdlog::error("smoke test timed out");
        QApplication::exit(3);
    });
}

} // namespace bl::ui
