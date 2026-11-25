#include "ui/MainWindow.h"

#include "bl/Version.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "doc/Document.h"
#include "kernel/Measure.h"
#include "kernel/ShapeInfo.h"
#include "ui/CommandBar.h"
#include "ui/CommandRegistry.h"
#include "ui/ConfirmationCorner.h"
#include "ui/DocumentView.h"
#include "ui/FeatureTreePanel.h"
#include "ui/HomePage.h"
#include "ui/Icons.h"
#include "ui/MeasureCommand.h"
#include "ui/OptionsDialog.h"
#include "ui/PropertyPanel.h"
#include "ui/ResourcePane.h"
#include "ui/SectionCommand.h"
#include "ui/SketchEditor.h"
#include "ui/ViewHud.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRep_Builder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>

#include <spdlog/spdlog.h>

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <string>

namespace bl::ui {
namespace {

bool isStepFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("step") || suffix == QLatin1String("stp");
}

QString q(const std::string& s) { return QString::fromStdString(s); }

// Commands that act on an open document; disabled on the Home screen.
const char* const kDocumentCommands[] = {
    "file.close", "insert.body", "view.fit", "view.zoomArea", "view.previous", "view.section", "view.front",
    "view.back", "view.left", "view.right", "view.top", "view.bottom", "view.isometric", "view.dimetric",
    "view.trimetric", "view.normalTo", "view.style.shadedEdges", "view.style.shaded", "view.style.hlr",
    "view.style.hlv", "view.style.wireframe", "evaluate.measure", "evaluate.massProps", "evaluate.check",
    "window.next", "sketch.sketch"};

// Sketch tools: command id -> tool (enabled while a sketch is open).
struct SketchToolDef {
    const char* id;
    SketchEditor::Tool tool;
};
const SketchToolDef kSketchTools[] = {
    {"sketch.dimension", SketchEditor::Tool::Dimension},   {"sketch.line", SketchEditor::Tool::Line},
    {"sketch.centerline", SketchEditor::Tool::Centerline}, {"sketch.rectangle", SketchEditor::Tool::Rectangle},
    {"sketch.centerRectangle", SketchEditor::Tool::CenterRectangle}, {"sketch.circle", SketchEditor::Tool::Circle},
    {"sketch.arc", SketchEditor::Tool::Arc},               {"sketch.tangentArc", SketchEditor::Tool::TangentArc},
    {"sketch.point", SketchEditor::Tool::Point},           {"sketch.slot", SketchEditor::Tool::Slot},
    {"sketch.polygon", SketchEditor::Tool::Polygon},       {"sketch.fillet", SketchEditor::Tool::Fillet},
    {"sketch.trim", SketchEditor::Tool::Trim},             {"sketch.extend", SketchEditor::Tool::Extend},
    {"sketch.offset", SketchEditor::Tool::Offset},         {"sketch.mirror", SketchEditor::Tool::Mirror},
    {"sketch.convert", SketchEditor::Tool::Convert},       {"sketch.doctor", SketchEditor::Tool::Doctor},
};

Units defaultUnits() {
    const Settings& s = Settings::global();
    Units u;
    u.system = units::systemFromName(s.get<std::string>("units.default", "MMGS")).value_or(UnitSystem::MMGS);
    u.decimals = std::clamp(s.get<int>("units.decimals", 2), 0, 8);
    return u;
}

// Bodies for the roots of an imported STEP file.
std::vector<doc::Body> bodiesFrom(const kernel::ImportResult& result, const QString& path) {
    std::vector<doc::Body> bodies;
    const QString stem = QFileInfo(path).completeBaseName();
    int index = 0;
    for (const kernel::ImportedRoot& root : result.roots) {
        ++index;
        doc::Body body;
        // Exporters often write their own name as the product name; the file name is more useful then.
        body.name = q(root.name).trimmed();
        if (body.name.isEmpty() || body.name.startsWith(QLatin1String("Open CASCADE STEP translator"))) {
            body.name = result.roots.size() == 1 ? stem : QStringLiteral("%1-%2").arg(stem).arg(index);
        }
        body.shape = root.shape;
        body.xcaf = result.doc;
        body.label = root.label;
        body.sourceFile = path;
        bodies.push_back(std::move(body));
    }
    return bodies;
}

std::optional<gp_Pln> planarFace(const TopoDS_Shape& shape) {
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

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_registry(std::make_unique<CommandRegistry>(this)) {
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QStringLiteral("BrokenLiquid"));
    setAcceptDrops(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
    resize(1500, 920);

    registerCommands();
    buildDocks();
    buildCentral();
    buildMenus();
    buildQuickAccess();
    buildCommandBar();
    buildStatusBar();
    restoreLayout();

    const QString theme = q(Settings::global().get<std::string>("ui.theme", "light"));
    setTheme(theme == QLatin1String("dark") ? ThemeKind::Dark : ThemeKind::Light);

    // Enter / Esc / D drive the running command (PLAN.md 7.3); off while no command runs.
    auto key = [this](const QKeySequence& seq, std::function<void()> fn) {
        auto* shortcut = new QShortcut(seq, this);
        shortcut->setEnabled(false);
        connect(shortcut, &QShortcut::activated, this, std::move(fn));
        m_commandKeys.push_back(shortcut);
    };
    auto enter = [this] {
        if (!(m_command && m_command->enterPressed())) {
            finishCommand(true);
        }
    };
    key(QKeySequence(Qt::Key_Return), enter);
    key(QKeySequence(Qt::Key_Enter), enter);
    key(QKeySequence(Qt::Key_Escape), [this] {
        if (!(m_command && m_command->escapePressed())) {
            finishCommand(false);
        }
    });
    key(QKeySequence(Qt::Key_D), [this] {
        if (SketchEditor* editor = sketchEditor()) {
            editor->setTool(SketchEditor::Tool::Dimension); // D is Smart Dimension while sketching
        } else if (m_activeView) {
            m_activeView->corner()->moveToCursor();
        }
    });
    key(QKeySequence(QKeySequence::Delete), [this] {
        if (m_command) {
            m_command->deletePressed();
        }
    });

    setActiveView(nullptr);
}

MainWindow::~MainWindow() {
    m_command.reset();
    m_activeConnections.clear();
}

doc::Document* MainWindow::activeDocument() { return m_activeView ? m_activeView->document() : nullptr; }

Units MainWindow::units() const {
    return m_activeView ? m_activeView->document()->model().units : defaultUnits();
}

void MainWindow::showStatus(const QString& text, int ms) { statusBar()->showMessage(text, ms); }

std::vector<DocumentView*> MainWindow::views() const {
    std::vector<DocumentView*> result;
    for (int i = 0; i < m_documents->count(); ++i) {
        if (auto* view = qobject_cast<DocumentView*>(m_documents->widget(i))) {
            result.push_back(view);
        }
    }
    return result;
}

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
    // Runs `fn` on the active viewer, if any.
    auto onViewer = [this](std::function<void(render::OcctViewer&)> fn) {
        return [this, fn = std::move(fn)] {
            if (m_activeView) {
                fn(*m_activeView->viewer());
            }
        };
    };

    // File
    command("file.new.part", tr("New Part"), "new-part", QKeySequence::New, tr("Start a new, empty part"),
            [this] { newPart(); });
    planned("file.new.assembly", tr("New Assembly"), "Phase 4", "new-assembly");
    planned("file.new.drawing", tr("New Drawing"), "Phase 5", "new-drawing");
    command("file.open", tr("Open…"), "open", QKeySequence::Open, tr("Open a STEP file as a new part"), [this] {
        const QString startDir = q(Settings::global().get<std::string>("files.lastDir", ""));
        const QString path = QFileDialog::getOpenFileName(this, tr("Open"), startDir,
                                                          tr("STEP files (*.step *.stp);;All files (*)"));
        if (!path.isEmpty()) {
            openFile(path);
        }
    });
    command("file.close", tr("Close"), "exit", QKeySequence(Qt::CTRL | Qt::Key_W), tr("Close the active document"),
            [this] { closeDocument(m_documents->currentIndex()); });
    planned("file.save", tr("Save"), "Phase 3", "save", QKeySequence::Save);
    planned("file.print", tr("Print…"), "Phase 5", "print", QKeySequence::Print);
    command("file.exit", tr("Exit"), "exit", QKeySequence(), tr("Close BrokenLiquid"), [this] { close(); });
    command("insert.body", tr("Imported Body (STEP)…"), "open", QKeySequence(),
            tr("Add the solids of a STEP file to the active part"), [this] {
                const QString startDir = q(Settings::global().get<std::string>("files.lastDir", ""));
                const QString path = QFileDialog::getOpenFileName(this, tr("Insert Imported Body"), startDir,
                                                                  tr("STEP files (*.step *.stp)"));
                if (!path.isEmpty()) {
                    insertFile(path);
                }
            });

    // Edit / tools
    command("edit.undo", tr("Undo"), "undo", QKeySequence::Undo, tr("Undo the last change"), [this] {
        if (m_command && m_command->handlesUndo()) {
            m_command->undo();
            return;
        }
        finishCommand(false);
        if (doc::Document* d = activeDocument(); d && d->canUndo()) {
            const QString label = d->undoLabel();
            d->undo();
            showStatus(tr("Undo: %1").arg(label));
        }
    });
    command("edit.redo", tr("Redo"), "redo", QKeySequence(Qt::CTRL | Qt::Key_Y), tr("Redo the last undone change"),
            [this] {
                if (m_command && m_command->handlesUndo()) {
                    m_command->redo();
                    return;
                }
                finishCommand(false);
                if (doc::Document* d = activeDocument(); d && d->canRedo()) {
                    const QString label = d->redoLabel();
                    d->redo();
                    showStatus(tr("Redo: %1").arg(label));
                }
            });
    planned("edit.rebuild", tr("Rebuild"), "Phase 3", "rebuild", QKeySequence(Qt::CTRL | Qt::Key_B));
    command("tools.options", tr("Options…"), "options", QKeySequence(), tr("System options and document properties"),
            [this] { showOptions(); });
    command("window.next", tr("Next Document"), "pane-files", QKeySequence(Qt::CTRL | Qt::Key_Tab),
            tr("Switch to the next open document"), [this] {
                if (m_documents->count() > 1) {
                    m_documents->setCurrentIndex((m_documents->currentIndex() + 1) % m_documents->count());
                }
            });

    // View
    command("view.fit", tr("Zoom to Fit"), "view-fit", QKeySequence(Qt::Key_F), tr("Fit the model in the window"),
            onViewer([](render::OcctViewer& v) { v.fitAll(); }));
    command("view.zoomArea", tr("Zoom to Area"), "view-zoom-area", QKeySequence(),
            tr("Drag a rectangle to zoom into"), onViewer([this](render::OcctViewer& v) {
                v.armZoomArea();
                showStatus(tr("Drag a rectangle around the area to zoom into."));
            }));
    command("view.previous", tr("Previous View"), "view-previous", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z),
            tr("Go back to the previous view orientation and zoom"), onViewer([this](render::OcctViewer& v) {
                if (!v.previousView()) {
                    showStatus(tr("No previous view."));
                }
            }));
    QAction* section = command("view.section", tr("Section View"), "view-section", QKeySequence(),
                               tr("Cut the model open with a plane to see inside"), [this] { toggleSection(); });
    section->setCheckable(true);

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
                onViewer([sv](render::OcctViewer& viewer) { viewer.setStandardView(sv); }));
    }
    command("view.dimetric", tr("Dimetric"), "view-iso", QKeySequence(), tr("Dimetric view"),
            onViewer([](render::OcctViewer& v) { v.setStandardView(StandardView::Dimetric); }));
    command("view.trimetric", tr("Trimetric"), "view-iso", QKeySequence(), tr("Trimetric view"),
            onViewer([](render::OcctViewer& v) { v.setStandardView(StandardView::Trimetric); }));
    command("view.normalTo", tr("Normal To"), "view-normal", QKeySequence(Qt::CTRL | Qt::Key_8),
            tr("Look straight at the selected planar face"), onViewer([this](render::OcctViewer& v) {
                for (const auto& pick : v.selection()) {
                    if (const auto plane = planarFace(pick.shape)) {
                        v.normalTo(plane->Axis().Direction());
                        return;
                    }
                }
                showStatus(tr("Select a flat face first."));
            }));

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
        QAction* a = command(s.id, s.label, s.icon, QKeySequence(), s.label,
                             onViewer([ds](render::OcctViewer& v) { v.setDisplayStyle(ds); }));
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
    QAction* sketchAction = command("sketch.sketch", tr("Sketch"), "sketch", QKeySequence(),
                                    tr("Sketch on a plane or flat face (or a selected sketch); exits the open sketch"),
                                    [this] { startSketch(); });
    sketchAction->setCheckable(true);
    const struct {
        const char* id;
        QString label;
        const char* icon;
        QKeySequence key;
        QString tip;
    } tools[] = {
        {"sketch.dimension", tr("Smart Dimension"), "smart-dimension", QKeySequence(),
         tr("Dimension one or two entities; the type follows what you pick (D)")},
        {"sketch.line", tr("Line"), "line", QKeySequence(Qt::Key_L), tr("Draw lines, click by click (L)")},
        {"sketch.centerline", tr("Centerline"), "centerline", QKeySequence(), tr("Draw construction lines")},
        {"sketch.rectangle", tr("Corner Rectangle"), "rectangle", QKeySequence(Qt::Key_R),
         tr("Rectangle from two opposite corners (R)")},
        {"sketch.centerRectangle", tr("Center Rectangle"), "center-rectangle", QKeySequence(),
         tr("Rectangle from its centre and a corner")},
        {"sketch.circle", tr("Circle"), "circle", QKeySequence(Qt::Key_C), tr("Circle from its centre and radius (C)")},
        {"sketch.arc", tr("Centerpoint Arc"), "arc", QKeySequence(Qt::Key_A), tr("Arc from centre, start and end (A)")},
        {"sketch.tangentArc", tr("Tangent Arc"), "tangent-arc", QKeySequence(),
         tr("Arc tangent to the end of a line or arc")},
        {"sketch.point", tr("Point"), "sketch-point", QKeySequence(Qt::Key_P), tr("Sketch point (P)")},
        {"sketch.slot", tr("Straight Slot"), "slot", QKeySequence(),
         tr("Slot from the centres of its two ends and its width")},
        {"sketch.polygon", tr("Polygon"), "polygon", QKeySequence(), tr("Regular polygon from its centre and a corner")},
        {"sketch.fillet", tr("Sketch Fillet"), "sketch-fillet", QKeySequence(),
         tr("Round the corner between two lines; the corner's dimensions are kept")},
        {"sketch.trim", tr("Trim Entities"), "trim", QKeySequence(Qt::Key_T),
         tr("Remove the piece of a curve up to where other curves cross it (T)")},
        {"sketch.extend", tr("Extend Entities"), "extend", QKeySequence(), tr("Extend a line or arc to the next curve")},
        {"sketch.offset", tr("Offset Entities"), "offset", QKeySequence(Qt::Key_O),
         tr("Copy curves or a whole contour at a distance, dimensioned (O)")},
        {"sketch.mirror", tr("Mirror Entities"), "sketch-mirror", QKeySequence(),
         tr("Mirror entities about a line; the copies stay symmetric")},
        {"sketch.convert", tr("Convert Entities"), "convert", QKeySequence(),
         tr("Copy model edges, or all edges of a face, into the sketch")},
        {"sketch.doctor", tr("Sketch Doctor"), "issues", QKeySequence(),
         tr("Find gaps, open ends, overlaps and duplicates that stop a sketch making a clean profile")},
    };
    for (const auto& t : tools) {
        SketchEditor::Tool tool = SketchEditor::Tool::Select;
        for (const SketchToolDef& def : kSketchTools) {
            if (qstrcmp(def.id, t.id) == 0) {
                tool = def.tool;
            }
        }
        QAction* a = command(t.id, t.label, t.icon, t.key, t.tip, [this, tool] {
            if (SketchEditor* editor = sketchEditor()) {
                editor->setTool(editor->tool() == tool ? SketchEditor::Tool::Select : tool);
            }
        });
        a->setCheckable(true);
    }
    command("sketch.relation", tr("Add Relation"), "relation", QKeySequence(),
            tr("Select sketch entities to see their relations and add new ones"), [this] {
                if (SketchEditor* editor = sketchEditor()) {
                    editor->setTool(SketchEditor::Tool::Select);
                    showStatus(tr("Select the entities to relate; the Property Panel lists the relations that fit."));
                }
            });
    command("sketch.construction", tr("Construction Geometry"), "construction", QKeySequence(Qt::Key_X),
            tr("Turn the selected sketch curves into construction geometry, or back (X)"), [this] {
                if (SketchEditor* editor = sketchEditor()) {
                    editor->toggleConstruction();
                }
            });

    // Evaluate
    command("evaluate.measure", tr("Measure"), "measure", QKeySequence(),
            tr("Distances, angles, lengths and areas of picked entities"),
            [this] { startCommand(std::make_unique<MeasureCommand>(*this)); });
    command("evaluate.massProps", tr("Mass Properties"), "mass-properties", QKeySequence(),
            tr("Volume, surface area and size of the visible bodies"), [this] { showMassProperties(); });
    command("evaluate.check", tr("Check Geometry"), "check", QKeySequence(),
            tr("Check the visible bodies for geometry errors"), [this] { showMassProperties(); });
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
    addAll(file, {"file.new.part", "file.new.assembly", "file.new.drawing", nullptr, "file.open", "file.close",
                  "file.save", nullptr, "file.print", nullptr, "file.exit"});

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
    addAll(view, {nullptr, "window.next"});

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
    addAll(insert, {nullptr, "insert.body", nullptr, "sketch.sketch", "features.equations"});

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
    m_commandBar->addGroup(features, {"features.combine", "features.equations", "insert.body"});

    const int sketch = m_commandBar->addTab(tr("Sketch"));
    m_sketchTab = sketch;
    m_commandBar->addGroup(sketch, {"sketch.sketch", "sketch.dimension"});
    m_commandBar->addFlyout(sketch, tr("Line"), {"sketch.line", "sketch.centerline"});
    m_commandBar->addFlyout(sketch, tr("Rectangle"), {"sketch.rectangle", "sketch.centerRectangle"});
    m_commandBar->addGroup(sketch, {"sketch.slot", "sketch.circle"});
    m_commandBar->addFlyout(sketch, tr("Arc"), {"sketch.arc", "sketch.tangentArc"});
    m_commandBar->addGroup(sketch, {"sketch.polygon", "sketch.point"});
    m_commandBar->addGroup(sketch, {"sketch.fillet", "sketch.offset", "sketch.convert", "sketch.mirror"});
    m_commandBar->addFlyout(sketch, tr("Trim"), {"sketch.trim", "sketch.extend"});
    m_commandBar->addGroup(sketch, {"sketch.relation", "sketch.construction", "sketch.doctor"});

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
    m_central = new QStackedWidget(this);
    m_central->setObjectName(QStringLiteral("CentralStack"));

    m_home = new HomePage(*m_registry, m_central);
    connect(m_home, &HomePage::openRecentRequested, this, &MainWindow::openFile);
    connect(m_home, &HomePage::settingsChanged, this, [this] {
        const QString theme = q(Settings::global().get<std::string>("ui.theme", "light"));
        setTheme(theme == QLatin1String("dark") ? ThemeKind::Dark : ThemeKind::Light);
    });

    m_documents = new QTabWidget(m_central);
    m_documents->setObjectName(QStringLiteral("DocumentTabs"));
    m_documents->setDocumentMode(true);
    m_documents->setTabsClosable(true);
    m_documents->setMovable(true);
    m_documents->tabBar()->setExpanding(false);
    connect(m_documents, &QTabWidget::tabCloseRequested, this, [this](int index) { closeDocument(index); });
    connect(m_documents, &QTabWidget::currentChanged, this,
            [this](int index) { setActiveView(qobject_cast<DocumentView*>(m_documents->widget(index))); });

    m_central->addWidget(m_home);
    m_central->addWidget(m_documents);
    setCentralWidget(m_central);
}

void MainWindow::buildDocks() {
    // Left: Feature Tree / Property Panel (tabs, like the familiar manager pane)
    m_tree = new FeatureTreePanel(this);
    m_panel = new PropertyPanel(this);
    m_managerTabs = new QTabWidget(this);
    m_managerTabs->setObjectName(QStringLiteral("ManagerTabs"));
    m_managerTabs->setDocumentMode(true);
    m_managerTabs->setIconSize(QSize(18, 18));
    m_managerTabs->addTab(m_tree, icon(QStringLiteral("tab-tree"), tr("Feature Tree")), QString());
    m_managerTabs->setTabToolTip(0, tr("Feature Tree"));
    m_managerTabs->addTab(m_panel, icon(QStringLiteral("tab-property"), tr("Property Panel")), QString());
    m_managerTabs->setTabToolTip(1, tr("Property Panel"));

    connect(m_panel, &PropertyPanel::okClicked, this, [this] { finishCommand(true); });
    connect(m_panel, &PropertyPanel::cancelClicked, this, [this] { finishCommand(false); });

    connect(m_tree, &FeatureTreePanel::bodySelectionChanged, this, [this](const QList<QUuid>& ids) {
        if (m_activeView && !m_command) {
            m_activeView->selectBodies(ids);
            updateSelectionStatus();
        }
    });
    connect(m_tree, &FeatureTreePanel::renameRequested, this, &MainWindow::renameBody);
    connect(m_tree, &FeatureTreePanel::visibilityRequested, this, &MainWindow::setBodiesVisible);
    connect(m_tree, &FeatureTreePanel::colorRequested, this, &MainWindow::recolorBodies);
    connect(m_tree, &FeatureTreePanel::deleteRequested, this, &MainWindow::deleteBodies);
    connect(m_tree, &FeatureTreePanel::sketchEditRequested, this, &MainWindow::editSketch);
    connect(m_tree, &FeatureTreePanel::sketchRenameRequested, this, &MainWindow::renameSketch);
    connect(m_tree, &FeatureTreePanel::sketchVisibilityRequested, this, &MainWindow::setSketchVisible);
    connect(m_tree, &FeatureTreePanel::sketchDeleteRequested, this, &MainWindow::deleteSketch);

    auto* left = new QDockWidget(tr("Feature Tree"), this);
    left->setObjectName(QStringLiteral("ManagerDock"));
    left->setWidget(m_managerTabs);
    left->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    left->setTitleBarWidget(new QWidget(left)); // no title bar, like the familiar layout
    addDockWidget(Qt::LeftDockWidgetArea, left);
    resizeDocks({left}, {280}, Qt::Horizontal);

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
    m_statusSelection->setObjectName(QStringLiteral("StatusMeasure"));
    m_statusState = new QLabel(bar);
    m_statusState->setObjectName(QStringLiteral("StatusState"));
    m_statusUnits = new QToolButton(bar);
    m_statusUnits->setObjectName(QStringLiteral("StatusUnits"));
    m_statusUnits->setPopupMode(QToolButton::InstantPopup);
    m_statusUnits->setAutoRaise(true);
    auto* menu = new QMenu(m_statusUnits);
    for (UnitSystem s : {UnitSystem::MMGS, UnitSystem::CGS, UnitSystem::MKS, UnitSystem::IPS}) {
        QAction* a = menu->addAction(QStringLiteral("%1  (%2)").arg(QString::fromLatin1(units::systemName(s)),
                                                                   QString::fromLatin1(units::systemDescription(s))));
        a->setData(static_cast<int>(s));
        connect(a, &QAction::triggered, this, [this, s] {
            if (doc::Document* d = activeDocument()) {
                Units u = d->model().units;
                u.system = s;
                setDocumentUnits(u);
            }
        });
    }
    menu->addSeparator();
    connect(menu->addAction(tr("Edit Document Units…")), &QAction::triggered, this, [this] { showOptions(true); });
    m_statusUnits->setMenu(menu);

    bar->addPermanentWidget(m_statusSelection);
    bar->addPermanentWidget(m_statusState);
    bar->addPermanentWidget(m_statusUnits);
    bar->showMessage(tr("Ready."));
}

void MainWindow::setTheme(ThemeKind kind) {
    applyTheme(kind);
    const ThemeColors c = themeColors(kind);
    for (DocumentView* view : views()) {
        view->viewer()->setBackgroundGradient(c.viewportTop, c.viewportBottom);
        view->hud()->update();
        view->refreshSketches();
    }
    if (m_command) {
        m_command->unitsChanged(); // redraws sketch graphics in the new colours
    }
    if (QAction* a = m_registry->action(kind == ThemeKind::Dark ? QStringLiteral("view.theme.dark")
                                                                : QStringLiteral("view.theme.light"))) {
        a->setChecked(true);
    }
    Settings::global().set("ui.theme", std::string(kind == ThemeKind::Dark ? "dark" : "light"));
}

void MainWindow::applyViewerSettings(render::OcctViewer* viewer) {
    const ThemeColors c = themeColors(currentTheme());
    viewer->setBackgroundGradient(c.viewportTop, c.viewportBottom);
    viewer->setReverseWheelZoom(Settings::global().get<bool>("view.mouse.reverseZoom", false));
}

// --- Documents -------------------------------------------------------------------------------------

doc::Document* MainWindow::newPart() {
    doc::PartModel model;
    model.units = defaultUnits();
    auto document = std::make_unique<doc::Document>(doc::DocumentType::Part, tr("Part%1").arg(++m_partCounter),
                                                    std::move(model));
    DocumentView* view = addDocument(std::move(document));
    showStatus(tr("New part. Insert > Imported Body adds STEP solids to it."));
    return view->document();
}

DocumentView* MainWindow::addDocument(std::unique_ptr<doc::Document> document) {
    doc::Document* d = document.release();
    auto* view = new DocumentView(d, *m_registry, m_documents);
    d->setParent(view); // the document lives as long as its tab
    applyViewerSettings(view->viewer());

    render::OcctViewer* viewer = view->viewer();
    connect(viewer, &render::OcctViewer::firstFrame, this, [this] {
        if (!m_firstFrame) {
            m_firstFrame = true;
            emit ready();
        }
    });
    connect(viewer, &render::OcctViewer::glFailed, this, [this](const QString& error) {
        QMessageBox::critical(this, tr("Graphics problem"),
                              tr("The 3D view could not start.\n\n%1\n\n"
                                 "Updating the graphics driver usually fixes this.")
                                  .arg(error));
    });
    connect(view->corner(), &ConfirmationCorner::accepted, this, [this] { finishCommand(true); });
    connect(view->corner(), &ConfirmationCorner::cancelled, this, [this] { finishCommand(false); });
    connect(d, &doc::Document::historyChanged, this, [this, view] { updateTabText(view); });
    connect(d, &doc::Document::titleChanged, this, [this, view] { updateTabText(view); });

    const int index = m_documents->addTab(view, icon(QStringLiteral("tree-part"), d->title()), d->title());
    updateTabText(view);
    m_central->setCurrentWidget(m_documents);
    m_documents->setCurrentIndex(index);
    setActiveView(view);
    return view;
}

void MainWindow::updateTabText(DocumentView* view) {
    const int index = m_documents->indexOf(view);
    if (index < 0) {
        return;
    }
    doc::Document* d = view->document();
    m_documents->setTabText(index, d->isModified() ? d->title() + QStringLiteral(" *") : d->title());
    m_documents->setTabToolTip(index, d->filePath().isEmpty() ? d->title() : d->filePath());
    if (view == m_activeView) {
        updateDocumentState();
    }
}

bool MainWindow::closeDocument(int index) {
    auto* view = qobject_cast<DocumentView*>(m_documents->widget(index));
    if (!view) {
        return false;
    }
    if (view->document()->isModified()) {
        const auto answer = QMessageBox::question(
            this, tr("Close"),
            tr("%1 has changes that are not saved. Saving arrives in Phase 3.\n\nClose it anyway?")
                .arg(view->document()->title()));
        if (answer != QMessageBox::Yes) {
            return false;
        }
    }
    if (view == m_activeView) {
        finishCommand(false, true);
    }
    m_documents->removeTab(index);
    view->deleteLater();
    if (m_documents->count() == 0) {
        setActiveView(nullptr);
    }
    return true;
}

void MainWindow::setActiveView(DocumentView* view) {
    if (m_command && view != m_activeView) {
        finishCommand(m_command->keepOnInterrupt(), true);
    }
    for (const QMetaObject::Connection& c : m_activeConnections) {
        disconnect(c);
    }
    m_activeConnections.clear();
    m_activeView = view;

    if (view) {
        doc::Document* d = view->document();
        m_activeConnections.push_back(connect(view->viewer(), &render::OcctViewer::selectionChanged, this,
                                              &MainWindow::onViewerSelection));
        m_activeConnections.push_back(connect(d, &doc::Document::historyChanged, this, &MainWindow::updateDocumentState));
        m_activeConnections.push_back(connect(d, &doc::Document::modelChanged, this, [this] {
            if (m_command) {
                m_command->unitsChanged();
            }
            updateUnitsStatus();
            updateSelectionStatus();
        }));
        m_activeConnections.push_back(connect(d, &doc::Document::titleChanged, this, &MainWindow::updateDocumentState));
        m_central->setCurrentWidget(m_documents);
    } else {
        m_central->setCurrentWidget(m_home);
        m_home->refresh();
    }
    m_tree->setDocument(view ? view->document() : nullptr);
    m_managerTabs->setCurrentWidget(m_tree);
    updateDocumentState();
    updateUnitsStatus();
    updateSelectionStatus();
}

void MainWindow::updateDocumentState() {
    doc::Document* d = activeDocument();
    setWindowTitle(d ? QStringLiteral("%1%2 - BrokenLiquid").arg(d->title(), d->isModified() ? QStringLiteral(" *") : QString())
                     : QStringLiteral("BrokenLiquid"));

    for (const char* id : kDocumentCommands) {
        if (QAction* a = m_registry->action(QString::fromLatin1(id))) {
            a->setEnabled(d != nullptr);
        }
    }
    QAction* undo = m_registry->action(QStringLiteral("edit.undo"));
    QAction* redo = m_registry->action(QStringLiteral("edit.redo"));
    if (m_command && m_command->handlesUndo()) {
        undo->setEnabled(m_command->canUndo());
        redo->setEnabled(m_command->canRedo());
        undo->setText(tr("Undo (%1)").arg(m_command->title()));
        redo->setText(tr("Redo (%1)").arg(m_command->title()));
    } else {
        undo->setEnabled(d && d->canUndo());
        redo->setEnabled(d && d->canRedo());
        undo->setText(d && d->canUndo() ? tr("Undo %1").arg(d->undoLabel()) : tr("Undo"));
        redo->setText(d && d->canRedo() ? tr("Redo %1").arg(d->redoLabel()) : tr("Redo"));
    }
    undo->setToolTip(undo->text() + QStringLiteral(" (Ctrl+Z)"));
    redo->setToolTip(redo->text() + QStringLiteral(" (Ctrl+Y)"));

    if (m_activeView) {
        m_registry->action(QStringLiteral("view.section"))->setChecked(m_activeView->section().has_value());
        static const std::pair<render::DisplayStyle, const char*> styles[] = {
            {render::DisplayStyle::ShadedWithEdges, "view.style.shadedEdges"},
            {render::DisplayStyle::Shaded, "view.style.shaded"},
            {render::DisplayStyle::HiddenLinesRemoved, "view.style.hlr"},
            {render::DisplayStyle::HiddenLinesVisible, "view.style.hlv"},
            {render::DisplayStyle::Wireframe, "view.style.wireframe"},
        };
        for (const auto& [style, id] : styles) {
            if (style == m_activeView->viewer()->displayStyle()) {
                m_registry->action(QString::fromLatin1(id))->setChecked(true);
            }
        }
        const int bodies = static_cast<int>(m_activeView->document()->model().bodies.size());
        m_statusState->setText(m_stateText.isEmpty() ? tr("%n body(s)", nullptr, bodies) : m_stateText);
    } else {
        m_registry->action(QStringLiteral("view.section"))->setChecked(false);
        m_statusState->setText(tr("No document"));
    }
    updateSketchActions();
}

void MainWindow::openFile(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    if (!isStepFile(path)) {
        QMessageBox::information(this, tr("Open"), tr("BrokenLiquid can open STEP files (.step, .stp) so far.\n\n%1").arg(path));
        return;
    }
    importAsync(path, false);
}

void MainWindow::insertFile(const QString& path) {
    if (!activeDocument()) {
        openFile(path);
        return;
    }
    importAsync(path, true);
}

void MainWindow::importAsync(const QString& path, bool insert) {
    if (m_importsBusy > 0) {
        showStatus(tr("Still importing the previous file…"), 3000);
        return;
    }
    ++m_importsBusy;
    Settings::global().set("files.lastDir", QFileInfo(path).absolutePath().toStdString());
    statusBar()->showMessage(tr("Importing %1…").arg(QFileInfo(path).fileName()));
    QApplication::setOverrideCursor(Qt::BusyCursor);

    QPointer<DocumentView> target = insert ? m_activeView : nullptr;
    auto* watcher = new QFutureWatcher<kernel::ImportResult>(this);
    connect(watcher, &QFutureWatcher<kernel::ImportResult>::finished, this, [this, watcher, path, insert, target] {
        QApplication::restoreOverrideCursor();
        --m_importsBusy;
        if (insert && target) {
            setActiveView(target);
            m_documents->setCurrentWidget(target);
        }
        finishImport(watcher->result(), path, insert && target);
        watcher->deleteLater();
    });
    const std::filesystem::path file = path.toStdWString();
    watcher->setFuture(QtConcurrent::run([file] { return kernel::importStep(file); }));
}

void MainWindow::finishImport(const kernel::ImportResult& result, const QString& path, bool insert) {
    if (!result.ok || result.roots.empty()) {
        statusBar()->showMessage(tr("Import failed."), 5000);
        QMessageBox::warning(this, tr("Import failed"),
                             tr("%1\n\n%2").arg(QFileInfo(path).fileName(),
                                                result.ok ? tr("The file has no shapes.") : q(result.error)));
        emit importFinished(false);
        return;
    }
    std::vector<doc::Body> bodies = bodiesFrom(result, path);
    const int count = static_cast<int>(bodies.size());

    if (insert) {
        doc::Document* d = activeDocument();
        d->transact(tr("Import Body"), [&bodies](doc::PartModel& m) {
            for (doc::Body& b : bodies) {
                ++m.importCount;
                m.bodies.push_back(std::move(b));
            }
            return true;
        });
        m_activeView->viewer()->fitAll();
    } else {
        // An untouched new part (e.g. Part1 at start-up) is replaced rather than kept beside it.
        if (doc::Document* d = activeDocument(); d && d->isPristine() && d->filePath().isEmpty()) {
            const int index = m_documents->indexOf(m_activeView);
            closeDocument(index);
        }
        doc::PartModel model;
        model.units = defaultUnits();
        model.importCount = count;
        model.bodies = std::move(bodies);
        auto document = std::make_unique<doc::Document>(doc::DocumentType::Part, QFileInfo(path).completeBaseName(),
                                                        std::move(model));
        document->setFilePath(path);
        addDocument(std::move(document));
        addRecentFile(path);
    }

    statusBar()->showMessage(tr("%1 %2: %n body(s) in %3 s", nullptr, count)
                                 .arg(insert ? tr("Inserted") : tr("Opened"), QFileInfo(path).fileName(),
                                      QString::number(result.seconds, 'f', 2)),
                             8000);
    emit importFinished(true);
}

void MainWindow::addRecentFile(const QString& path) {
    Settings& s = Settings::global();
    auto recent = s.get<std::vector<std::string>>("files.recent", {});
    const std::string entry = QFileInfo(path).absoluteFilePath().toStdString();
    recent.erase(std::remove(recent.begin(), recent.end(), entry), recent.end());
    recent.insert(recent.begin(), entry);
    const auto max = static_cast<size_t>(std::max(0, s.get<int>("files.recentMax", 10)));
    if (recent.size() > max) {
        recent.resize(max);
    }
    s.set("files.recent", recent);
}

// --- Interactive commands ------------------------------------------------------------------------------

void MainWindow::startCommand(std::unique_ptr<InteractiveCommand> command) {
    if (!m_activeView) {
        showStatus(tr("Open or create a part first."));
        return;
    }
    if (m_command) {
        finishCommand(m_command->keepOnInterrupt());
        if (m_command) {
            return; // the running command could not finish (needs more input)
        }
    }
    m_command = std::move(command);
    InteractiveCommand* cmd = m_command.get();
    connect(cmd, &InteractiveCommand::finishRequested, this, [this] { finishCommand(true); }, Qt::QueuedConnection);
    connect(cmd, &InteractiveCommand::stateChanged, this, &MainWindow::updateDocumentState);
    if (sketchEditor() && m_sketchTab >= 0) {
        m_commandBar->setCurrentTab(m_sketchTab);
    }

    m_panel->begin(cmd->title(), cmd->iconName(), cmd->confirms());
    m_managerTabs->setCurrentWidget(m_panel);
    m_activeView->corner()->showFor(cmd->confirms());
    for (QShortcut* key : m_commandKeys) {
        key->setEnabled(true);
    }
    cmd->start();
    spdlog::info("command started: {}", cmd->id().toStdString());
}

void MainWindow::finishCommand(bool accept, bool force) {
    if (!m_command) {
        return;
    }
    if (!accept && !force && !m_command->confirmCancel()) {
        return;
    }
    // Commit a value still being typed (fields apply on focus-out).
    if (QWidget* focus = QApplication::focusWidget(); focus && m_panel->isAncestorOf(focus)) {
        focus->clearFocus();
    }
    if (accept && m_command->confirms()) {
        if (!m_command->accept()) {
            return; // the command wants more input
        }
    } else {
        m_command->cancel();
    }
    spdlog::info("command {}: {}", accept ? "ok" : "cancelled", m_command->id().toStdString());

    InteractiveCommand* done = m_command.release();
    done->deleteLater(); // may be in one of its own signals
    m_panel->clear();
    m_managerTabs->setCurrentWidget(m_tree);
    for (QShortcut* key : m_commandKeys) {
        key->setEnabled(false);
    }
    for (DocumentView* view : views()) {
        view->corner()->hide();
        view->viewer()->clearTemporary();
        view->viewer()->setSelectionFilter(true, true, true);
    }
    updateDocumentState();
    updateSelectionStatus();
}

// --- Sketches ----------------------------------------------------------------------------------------------

SketchEditor* MainWindow::sketchEditor() const { return dynamic_cast<SketchEditor*>(m_command.get()); }

void MainWindow::setStateText(const QString& text) {
    m_stateText = text;
    updateDocumentState();
}

void MainWindow::updateSketchActions() {
    SketchEditor* editor = sketchEditor();
    for (const SketchToolDef& def : kSketchTools) {
        if (QAction* a = m_registry->action(QString::fromLatin1(def.id))) {
            a->setEnabled(editor != nullptr);
            a->setChecked(editor && editor->tool() == def.tool);
        }
    }
    for (const char* id : {"sketch.relation", "sketch.construction"}) {
        if (QAction* a = m_registry->action(QString::fromLatin1(id))) {
            a->setEnabled(editor != nullptr);
        }
    }
    if (QAction* a = m_registry->action(QStringLiteral("sketch.sketch"))) {
        a->setChecked(editor != nullptr);
        a->setText(editor ? tr("Exit Sketch") : tr("Sketch"));
    }
}

void MainWindow::startSketch() {
    if (sketchEditor()) {
        finishCommand(true); // the Sketch button doubles as Exit Sketch
        return;
    }
    if (!activeDocument() || !m_activeView) {
        showStatus(tr("Open or create a part first."));
        return;
    }
    if (!m_command) {
        if (const QUuid id = m_tree->selectedSketch(); !id.isNull()) {
            editSketch(id);
            return;
        }
        for (const auto& pick : m_activeView->viewer()->selection()) {
            if (const auto plane = planarFace(pick.shape)) {
                const doc::Body* body = activeDocument()->model().body(m_activeView->bodyOf(pick.object));
                startSketchOn(facePlane(*plane), body ? tr("Face of %1").arg(body->name) : tr("Face"));
                return;
            }
        }
    }
    startCommand(std::make_unique<SketchPlaneCommand>(
        *this, [this](const gp_Ax3& plane, const QString& name) { startSketchOn(plane, name); }));
}

void MainWindow::startSketchOn(const gp_Ax3& plane, const QString& planeName) {
    doc::Document* d = activeDocument();
    if (!d) {
        return;
    }
    doc::SketchFeature feature;
    feature.name = tr("Sketch%1").arg(d->model().sketchCount + 1);
    feature.planeName = planeName;
    feature.plane = plane;
    startCommand(std::make_unique<SketchEditor>(*this, std::move(feature), true));
}

void MainWindow::editSketch(const QUuid& id) {
    doc::Document* d = activeDocument();
    const doc::SketchFeature* feature = d ? d->model().sketch(id) : nullptr;
    if (!feature) {
        return;
    }
    if (SketchEditor* editor = sketchEditor(); editor && editor->sketchId() == id) {
        return;
    }
    startCommand(std::make_unique<SketchEditor>(*this, *feature, false));
}

void MainWindow::renameSketch(const QUuid& id, const QString& name) {
    if (doc::Document* d = activeDocument()) {
        d->transact(tr("Rename Sketch"), [&](doc::PartModel& m) {
            doc::SketchFeature* s = m.sketch(id);
            if (!s) {
                return false;
            }
            s->name = name;
            return true;
        });
    }
}

void MainWindow::setSketchVisible(const QUuid& id, bool visible) {
    if (doc::Document* d = activeDocument()) {
        d->transact(visible ? tr("Show Sketch") : tr("Hide Sketch"), [&](doc::PartModel& m) {
            doc::SketchFeature* s = m.sketch(id);
            if (!s) {
                return false;
            }
            s->visible = visible;
            return true;
        });
    }
}

void MainWindow::deleteSketch(const QUuid& id) {
    if (SketchEditor* editor = sketchEditor(); editor && editor->sketchId() == id) {
        showStatus(tr("Exit the sketch before deleting it."));
        return;
    }
    if (doc::Document* d = activeDocument()) {
        d->transact(tr("Delete Sketch"), [&](doc::PartModel& m) {
            return std::erase_if(m.sketches, [&id](const doc::SketchFeature& s) { return s.id == id; }) > 0;
        });
    }
}

void MainWindow::toggleSection() {
    if (!m_activeView) {
        return;
    }
    if (m_command && m_command->id() == QLatin1String("view.section")) {
        finishCommand(false);
    } else if (m_activeView->section()) {
        finishCommand(m_command && m_command->keepOnInterrupt());
        m_activeView->setSection(std::nullopt);
        showStatus(tr("Section view off."));
    } else {
        startCommand(std::make_unique<SectionCommand>(*this));
    }
    updateDocumentState();
    // While the command runs the button stays down, so it reads as "section view in progress".
    if (m_command && m_command->id() == QLatin1String("view.section")) {
        m_registry->action(QStringLiteral("view.section"))->setChecked(true);
    }
}

// --- Edits -----------------------------------------------------------------------------------------

void MainWindow::renameBody(const QUuid& id, const QString& name) {
    if (doc::Document* d = activeDocument()) {
        d->transact(tr("Rename Body"), [&](doc::PartModel& m) {
            doc::Body* b = m.body(id);
            if (!b) {
                return false;
            }
            b->name = name;
            return true;
        });
    }
}

void MainWindow::setBodiesVisible(const QList<QUuid>& ids, bool visible) {
    if (doc::Document* d = activeDocument()) {
        d->transact(visible ? tr("Show Body") : tr("Hide Body"), [&](doc::PartModel& m) {
            for (const QUuid& id : ids) {
                if (doc::Body* b = m.body(id)) {
                    b->visible = visible;
                }
            }
            return true;
        });
    }
}

void MainWindow::recolorBodies(const QList<QUuid>& ids) {
    doc::Document* d = activeDocument();
    if (!d || ids.isEmpty()) {
        return;
    }
    const doc::Body* first = d->model().body(ids.front());
    const QColor initial = first && first->color ? *first->color : QColor(0xB8, 0xC2, 0xCC);
    const QColor color = QColorDialog::getColor(initial, this, tr("Body Appearance"));
    if (!color.isValid()) {
        return;
    }
    d->transact(tr("Change Appearance"), [&](doc::PartModel& m) {
        for (const QUuid& id : ids) {
            if (doc::Body* b = m.body(id)) {
                b->color = color;
            }
        }
        return true;
    });
}

void MainWindow::deleteBodies(const QList<QUuid>& ids) {
    if (doc::Document* d = activeDocument()) {
        const bool done = d->transact(ids.size() == 1 ? tr("Delete Body") : tr("Delete Bodies"), [&](doc::PartModel& m) {
            const auto before = m.bodies.size();
            std::erase_if(m.bodies, [&](const doc::Body& b) { return ids.contains(b.id); });
            return m.bodies.size() != before;
        });
        if (done) {
            showStatus(tr("Deleted. Ctrl+Z brings it back."));
        }
    }
}

void MainWindow::setDocumentUnits(const Units& u) {
    if (doc::Document* d = activeDocument()) {
        d->transact(tr("Change Units"), [&](doc::PartModel& m) {
            m.units = u;
            return true;
        });
    }
}

// --- Selection / status bar ----------------------------------------------------------------------------

void MainWindow::onViewerSelection() {
    if (m_command) {
        m_command->selectionChanged();
    } else if (m_activeView) {
        m_tree->setSelectedBodies(m_activeView->selectedBodies());
    }
    updateSelectionStatus();
}

void MainWindow::updateSelectionStatus() {
    // Quick measure in the status bar (PLAN.md 6.9): length / area / distance of the selection.
    QString text;
    if (m_activeView && !m_command) {
        const Units u = units();
        std::vector<TopoDS_Shape> shapes;
        for (const auto& pick : m_activeView->viewer()->selection()) {
            if (!pick.shape.IsNull()) {
                shapes.push_back(pick.shape);
            }
        }
        if (shapes.size() == 1) {
            const kernel::EntityInfo info = kernel::describe(shapes.front());
            switch (info.kind) {
            case kernel::EntityKind::Edge:
                text = info.radius ? tr("Radius: %1   Length: %2").arg(q(units::formatLength(*info.radius, u)),
                                                                       q(units::formatLength(info.length, u)))
                                   : tr("Length: %1").arg(q(units::formatLength(info.length, u)));
                break;
            case kernel::EntityKind::Face:
                text = info.radius ? tr("Radius: %1   Area: %2").arg(q(units::formatLength(*info.radius, u)),
                                                                     q(units::formatArea(info.area, u)))
                                   : tr("Area: %1").arg(q(units::formatArea(info.area, u)));
                break;
            case kernel::EntityKind::Vertex:
                if (info.point) {
                    text = tr("X: %1  Y: %2  Z: %3")
                               .arg(q(units::formatLength(info.point->X(), u)), q(units::formatLength(info.point->Y(), u)),
                                    q(units::formatLength(info.point->Z(), u)));
                }
                break;
            case kernel::EntityKind::Solid:
                text = tr("Volume: %1").arg(q(units::formatVolume(info.volume, u)));
                break;
            case kernel::EntityKind::Other: break;
            }
        } else if (shapes.size() == 2) {
            const kernel::PairMeasure m = kernel::measurePair(shapes[0], shapes[1]);
            if (m.valid) {
                text = tr("Distance: %1").arg(q(units::formatLength(m.distance, u)));
                if (m.angle && !m.parallel) {
                    text += tr("   Angle: %1").arg(q(units::formatAngle(*m.angle, u)));
                }
            }
        } else if (shapes.size() > 2) {
            text = tr("%n selected", nullptr, static_cast<int>(shapes.size()));
        }
    }
    m_statusSelection->setText(text);
}

void MainWindow::updateUnitsStatus() {
    const Units u = units();
    m_statusUnits->setText(QString::fromLatin1(units::systemName(u.system)));
    m_statusUnits->setToolTip(tr("Document units: %1. Click to change.").arg(QString::fromLatin1(units::systemDescription(u.system))));
    m_statusUnits->setEnabled(activeDocument() != nullptr);
    for (QAction* a : m_statusUnits->menu()->actions()) {
        if (a->data().isValid()) {
            a->setCheckable(true);
            a->setChecked(a->data().toInt() == static_cast<int>(u.system));
        }
    }
}

void MainWindow::showOptions(bool documentPage) {
    doc::Document* d = activeDocument();
    const Units current = d ? d->model().units : Units{};
    OptionsDialog dialog(d ? &current : nullptr, d ? d->title() : QString(), this);
    if (documentPage) {
        dialog.showDocumentProperties();
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString theme = q(Settings::global().get<std::string>("ui.theme", "light"));
    setTheme(theme == QLatin1String("dark") ? ThemeKind::Dark : ThemeKind::Light);
    for (DocumentView* view : views()) {
        applyViewerSettings(view->viewer());
    }
    if (const auto changed = dialog.changedDocumentUnits()) {
        setDocumentUnits(*changed);
    }
    m_home->refresh();
}

// --- Evaluate / help -------------------------------------------------------------------------------

void MainWindow::showMassProperties() {
    doc::Document* d = activeDocument();
    BRep_Builder builder;
    TopoDS_Compound all;
    builder.MakeCompound(all);
    int count = 0;
    if (d) {
        for (const doc::Body& body : d->model().bodies) {
            if (body.visible) {
                builder.Add(all, body.shape);
                ++count;
            }
        }
    }
    if (count == 0) {
        QMessageBox::information(this, tr("Mass Properties"), tr("There are no visible bodies to evaluate."));
        return;
    }
    QApplication::setOverrideCursor(Qt::BusyCursor);
    const kernel::ShapeStats s = kernel::computeStats(all);
    QApplication::restoreOverrideCursor();

    const Units u = units();
    const double sx = s.bbox[3] - s.bbox[0], sy = s.bbox[4] - s.bbox[1], sz = s.bbox[5] - s.bbox[2];
    const QString text =
        tr("<table cellspacing='6'>"
           "<tr><td>Bodies</td><td align='right'>%1</td></tr>"
           "<tr><td>Volume</td><td align='right'><b>%2</b></td></tr>"
           "<tr><td>Surface area</td><td align='right'><b>%3</b></td></tr>"
           "<tr><td>Bounding box</td><td align='right'><b>%4 × %5 × %6</b></td></tr>"
           "<tr><td>Solids / faces / edges</td><td align='right'>%7 / %8 / %9</td></tr>"
           "<tr><td>Geometry check</td><td align='right'>%10</td></tr>"
           "</table><p style='color:gray'>Mass needs a material (Phase 3).</p>")
            .arg(count)
            .arg(q(units::formatVolume(s.volume, u)), q(units::formatArea(s.area, u)),
                 q(units::formatLength(sx, u, false)), q(units::formatLength(sy, u, false)), q(units::formatLength(sz, u)))
            .arg(s.solids)
            .arg(s.faces)
            .arg(s.edges)
            .arg(s.valid ? tr("<span style='color:#1b8a3a'>No errors</span>")
                         : tr("<span style='color:#c0392b'>Problems found</span>"));
    QMessageBox box(this);
    box.setWindowTitle(tr("Mass Properties — %1").arg(d->title()));
    box.setTextFormat(Qt::RichText);
    box.setText(text);
    box.exec();
}

void MainWindow::showGlInfo() {
    QMessageBox box(this);
    box.setWindowTitle(tr("Graphics Information"));
    render::OcctViewer* viewer = m_activeView ? m_activeView->viewer() : nullptr;
    box.setText(viewer && viewer->isGlReady() ? viewer->glInfo() : tr("Open a document to start the 3D view."));
    box.exec();
}

void MainWindow::showAbout() {
    QMessageBox::about(
        this, tr("About BrokenLiquid"),
        tr("<h3>BrokenLiquid %1</h3>"
           "<p>Free, local, parametric 3D CAD.<br>Build %2 · Phase 1 preview</p>"
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
    QStringList modified;
    for (DocumentView* view : views()) {
        if (view->document()->isModified()) {
            modified << view->document()->title();
        }
    }
    if (!modified.isEmpty() &&
        QMessageBox::question(this, tr("Exit"),
                              tr("These documents have changes that are not saved:\n\n%1\n\nSaving arrives in "
                                 "Phase 3. Exit anyway?")
                                  .arg(modified.join(QLatin1Char('\n')))) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    finishCommand(false, true);
    saveLayout();
    Settings::global().save();
    event->accept();
}

// --- Smoke test ------------------------------------------------------------------------------------

void MainWindow::runDemo(const QString& demo) {
    if (demo.isEmpty() || !m_activeView) {
        return;
    }
    if (demo == QLatin1String("measure")) {
        // Two parallel planar faces of the first body, as far apart as possible.
        const doc::Body& body = m_activeView->document()->model().bodies.front();
        std::vector<std::pair<TopoDS_Shape, gp_Pln>> faces;
        for (TopExp_Explorer it(body.shape, TopAbs_FACE); it.More(); it.Next()) {
            if (const auto plane = planarFace(it.Current())) {
                faces.emplace_back(it.Current(), *plane);
            }
        }
        double best = -1.0;
        std::pair<int, int> pick{-1, -1};
        for (size_t i = 0; i < faces.size(); ++i) {
            for (size_t j = i + 1; j < faces.size(); ++j) {
                const gp_Dir& n = faces[i].second.Axis().Direction();
                if (std::abs(n.Dot(faces[j].second.Axis().Direction())) < 0.9999) {
                    continue;
                }
                const double gap = std::abs(gp_Vec(faces[i].second.Location(), faces[j].second.Location()).Dot(gp_Vec(n)));
                if (gap > best) {
                    best = gap;
                    pick = {static_cast<int>(i), static_cast<int>(j)};
                }
            }
        }
        startCommand(std::make_unique<MeasureCommand>(*this));
        if (pick.first >= 0) {
            const Handle(AIS_InteractiveObject) object = m_activeView->objectOf(body.id);
            const bool a = m_activeView->viewer()->select(object, faces[pick.first].first);
            const bool b = m_activeView->viewer()->select(object, faces[pick.second].first);
            spdlog::info("demo measure: picked faces {} / {}", a, b);
        }
    } else if (demo == QLatin1String("section") || demo == QLatin1String("section-right")) {
        startCommand(std::make_unique<SectionCommand>(*this));
        if (demo == QLatin1String("section-right")) {
            if (auto* plane = m_panel->findChild<QComboBox*>()) {
                plane->setCurrentIndex(SectionSpec::Right); // through both holes of the sample bracket
            }
        }
    } else if (demo == QLatin1String("options")) {
        QTimer::singleShot(0, this, [this] { showOptions(true); });
    } else if (demo == QLatin1String("sketch-convert")) {
        // Sketch on the largest flat face of the first body and convert that face's edges.
        const doc::Document* d = activeDocument();
        if (!d || d->model().bodies.empty()) {
            return;
        }
        TopoDS_Shape best;
        double bestArea = 0.0;
        for (TopExp_Explorer it(d->model().bodies.front().shape, TopAbs_FACE); it.More(); it.Next()) {
            if (!planarFace(it.Current())) {
                continue;
            }
            GProp_GProps props;
            BRepGProp::SurfaceProperties(it.Current(), props);
            if (props.Mass() > bestArea) {
                bestArea = props.Mass();
                best = it.Current();
            }
        }
        if (best.IsNull()) {
            return;
        }
        startSketchOn(facePlane(*planarFace(best)), tr("Face"));
        if (SketchEditor* e = sketchEditor()) {
            e->setTool(SketchEditor::Tool::Convert);
            e->convertShape(best);
            spdlog::info("demo sketch-convert: {} curves, {}", e->sketch().curves().size(),
                         sketch::statusName(e->solveResult().status));
        }
    } else if (demo.startsWith(QLatin1String("sketch"))) {
        // Draws an L bracket profile with the tools as a user would (clicks in sketch coordinates),
        // dimensions part of it and selects the bottom line (relations in the Property Panel).
        startSketchOn(referencePlane(0), referencePlaneName(0));
        SketchEditor* e = sketchEditor();
        if (!e) {
            return;
        }
        using Tool = SketchEditor::Tool;
        using Clicks = std::initializer_list<std::pair<double, double>>;
        auto clicks = [e](Clicks points) {
            for (const auto& [u, v] : points) {
                e->clickAt(u, v);
            }
        };
        if (demo == QLatin1String("sketch-edit")) {
            // Trim, extend, mirror and the Sketch Doctor on a few loose lines.
            auto curveCount = [e] { return e->sketch().curves().size(); };
            auto line = [&](std::pair<double, double> a, std::pair<double, double> b, Tool tool = Tool::Line) {
                e->setTool(tool);
                clicks({a, b});
            };
            line({0, 0}, {40, 0});
            line({20, -10}, {20, 10});
            line({30, -10}, {30, 10});
            e->setTool(Tool::Trim);
            clicks({{25, 0}}); // the piece between the two verticals
            spdlog::info("demo sketch-edit: trim -> {} curves", curveCount());
            e->setTool(Tool::Trim);
            clicks({{20, 5}}); // the upper half of the first vertical
            e->setTool(Tool::Extend);
            clicks({{18, 0}}); // past the stub of the first vertical to the second one
            spdlog::info("demo sketch-edit: extend -> {} curves", curveCount());
            line({50, -10}, {50, 10}, Tool::Centerline);
            e->setTool(Tool::Circle);
            clicks({{42, 5}, {44, 5}});
            e->setTool(Tool::Mirror);
            clicks({{44, 5}, {50, -5}});
            const bool mirrored = e->applyMirror();
            spdlog::info("demo sketch-edit: mirror {} -> {} curves", mirrored, curveCount());
            line({0, 20}, {10, 20});
            line({0, 20}, {10, 20}); // a duplicate for the Sketch Doctor
            e->setTool(Tool::Doctor);
            const size_t found = e->issues().size();
            e->repairAll();
            spdlog::info("demo sketch-edit: doctor {} issue(s), {} after Repair All; {} curves, {}", found,
                         e->issues().size(), curveCount(), sketch::statusName(e->solveResult().status));
            return;
        }
        // Lines to the fillet, a tangent arc, then lines back to the origin (the line leaving
        // the arc picks up tangency, the closing one its vertical relation).
        e->setTool(Tool::Line);
        clicks({{0, 0}, {60, 0}, {60, 10}, {15, 10}});
        e->setTool(Tool::TangentArc);
        clicks({{15, 10}, {10, 15}});
        e->setTool(Tool::Line);
        clicks({{10, 15}, {10, 40}, {0, 40}, {0, 0}});
        e->setTool(Tool::Circle);
        clicks({{40, 5}, {42.5, 5}});
        // Smart Dimension: pick, place, accept the measured value.
        e->setTool(Tool::Dimension);
        const Clicks dimensions[] = {
            {{30, 0}, {30, -9}},          // overall width 60
            {{60, 5}, {69, 5}},           // end height 10
            {{0, 20}, {-9, 20}},          // overall height 40
            {{5, 40}, {5, 49}},           // leg width 10
            {{11.4645, 11.4645}, {6, 6}}, // fillet R5
            {{42.5, 5}, {52, 16}},        // hole diameter 5
            {{40, 5}, {0, 0}, {20, -19}}, // hole from the origin, horizontal 40
            {{40, 5}, {50, 0}, {47, 2.5}}, // hole above the bottom edge 5
        };
        if (demo != QLatin1String("sketch-partial")) {
            for (const Clicks& dim : dimensions) {
                clicks(dim);
                e->escapePressed(); // close the value box, keeping the value
            }
        }
        if (demo == QLatin1String("sketch-tools")) {
            // Round the far corner, offset the outline inwards, then a slot in the leg and a
            // hexagon in the flange, and end in the Sketch Doctor.
            e->setFilletRadius(3.0);
            e->setTool(Tool::Fillet);
            clicks({{60, 0}});
            e->setOffsetDistance(1.5, true);
            e->setTool(Tool::Offset);
            clicks({{30, 0}});
            e->applyOffset();
            e->setTool(Tool::Slot);
            clicks({{5, 22}, {5, 32}, {7, 27}});
            e->setPolygonSides(6);
            e->setTool(Tool::Polygon);
            clicks({{25, 5}, {28, 5}});
            e->showRelations(false); // a cleaner picture
            e->setTool(Tool::Doctor);
            const sketch::Sketch& s = e->sketch();
            spdlog::info("demo sketch-tools: {} curves, {} constraints, {}, DOF {}, {} regions, {} issues",
                         s.curves().size(), s.constraints().size(),
                         sketch::statusName(e->solveResult().status), e->solveResult().dof,
                         e->contours().regions.size(), e->issues().size());
            return;
        }
        e->setTool(Tool::Select);
        if (demo == QLatin1String("sketch-exit")) {
            finishCommand(true);
            if (m_activeView) {
                m_activeView->viewer()->setStandardView(render::StandardView::Isometric);
            }
        } else {
            const sketch::Sketch& s = e->sketch();
            for (const sketch::Curve& c : s.curves()) {
                const sketch::Point* a = s.point(c.p1);
                const sketch::Point* b = s.point(c.p2);
                if (c.type == sketch::CurveType::Line && a && b && std::abs(a->y) < 1e-6 && std::abs(b->y) < 1e-6) {
                    e->select({c.id});
                    break;
                }
            }
            const sketch::SolveResult& r = e->solveResult();
            spdlog::info("demo sketch: {} curves, {} constraints, {}, DOF {}", s.curves().size(), s.constraints().size(),
                         sketch::statusName(r.status), r.dof);
        }
    }
}

void MainWindow::runSmokeTest(const QString& pngPath, const QString& demo) {
    auto finished = std::make_shared<bool>(false);
    auto finish = [this, pngPath, demo, finished] {
        if (*finished) {
            return;
        }
        *finished = true;
        QTimer::singleShot(400, this, [this, pngPath, demo] {
            if (demo.startsWith(QLatin1String("sketch")) && !m_activeView) {
                newPart(); // sketch demos start from an empty part
            }
            const int settle = demo.startsWith(QLatin1String("sketch")) ? 700 : 0;
            QTimer::singleShot(settle, this, [this, demo] { runDemo(demo); });
            QTimer::singleShot(settle + 900, this, [this, pngPath] {
                QWidget* target = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : this;
                const QImage image = grab().toImage();
                bool ok = !image.isNull() && image.save(pngPath);
                if (target != this) {
                    // A dialog demo: save the dialog next to the window shot.
                    const QString dialogPng = QString(pngPath).replace(QLatin1String(".png"), QLatin1String("-dialog.png"));
                    ok = target->grab().save(dialogPng) && ok;
                }
                spdlog::info("smoke test screenshot {} -> {}", pngPath.toStdString(), ok ? "saved" : "FAILED");
                QApplication::exit(ok ? 0 : 2);
            });
        });
    };
    auto check = [this, finish] {
        if (m_importsBusy == 0 && (m_documents->count() == 0 || m_firstFrame)) {
            finish();
        }
    };
    connect(this, &MainWindow::ready, this, check);
    connect(this, &MainWindow::importFinished, this, check);
    QTimer::singleShot(2000, this, check); // Home screen: nothing to wait for
    QTimer::singleShot(60000, this, [] {
        spdlog::error("smoke test timed out");
        QApplication::exit(3);
    });
}

} // namespace bl::ui
