#pragma once

#include "kernel/StepIO.h"
#include "render/OcctViewer.h"
#include "ui/DocumentView.h"
#include "ui/InteractiveCommand.h"
#include "ui/SketchEditor.h"
#include "ui/Theme.h"

#include <QMainWindow>
#include <QPointer>

#include <memory>
#include <vector>

class QLabel;
class QLineEdit;
class QShortcut;
class QStackedWidget;
class QTabWidget;
class QToolButton;

namespace bl::doc {
class Document;
}

namespace bl::ui {

class CommandRegistry;
class CommandBar;
class DocumentView;
class FeatureTreePanel;
class HomePage;
class PropertyPanel;
class ResourcePane;

// Main application window with the familiar CAD layout (PLAN.md 7.1): one tab per open document,
// the Home screen when none is open, Feature Tree / Property Panel on the left.
class MainWindow : public QMainWindow, public CommandHost {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    CommandRegistry& commands() { return *m_registry; }

    // Opens a STEP file as a new part document (imported on a worker thread).
    void openFile(const QString& path);
    // Adds the bodies of a STEP file to the active part (undoable).
    void insertFile(const QString& path);
    doc::Document* newPart();

    // Test hook: once the first frame and any pending import are done, optionally run a demo
    // ("measure", "section"), save a screenshot of the window to `pngPath` and quit with exit
    // code 0 (2 on failure).
    void runSmokeTest(const QString& pngPath, const QString& demo = {});

    // CommandHost
    PropertyPanel& panel() override { return *m_panel; }
    DocumentView* activeView() override { return m_activeView; }
    doc::Document* activeDocument() override;
    Units units() const override;
    void showStatus(const QString& text, int ms = 4000) override;
    void setStateText(const QString& text) override;

signals:
    void ready(); // first frame rendered (or Home screen shown)
    void importFinished(bool ok);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void registerCommands();
    void buildMenus();
    void buildQuickAccess();
    void buildCommandBar();
    void buildDocks();
    void buildCentral();
    void buildStatusBar();
    void setTheme(ThemeKind kind);

    // Documents
    DocumentView* addDocument(std::unique_ptr<doc::Document> document);
    bool closeDocument(int index);
    void setActiveView(DocumentView* view);
    void updateDocumentState();
    void updateTabText(DocumentView* view);
    void importAsync(const QString& path, bool insert);
    void finishImport(const kernel::ImportResult& result, const QString& path, bool insert);
    void addRecentFile(const QString& path);
    std::vector<DocumentView*> views() const;

    // Interactive commands (Property Panel + Confirmation Corner)
    void startCommand(std::unique_ptr<InteractiveCommand> command);
    // `force`: no "discard changes?" question (the document is closing anyway).
    void finishCommand(bool accept, bool force = false);
    void toggleSection();

    // Sketches
    SketchEditor* sketchEditor() const;
    void startSketch();
    void startSketchOn(const gp_Ax3& plane, const QString& planeName,
                       const std::optional<kernel::FaceRef>& face = std::nullopt);
    void editSketch(const QUuid& id);
    void renameSketch(const QUuid& id, const QString& name);
    void setSketchVisible(const QUuid& id, bool visible);
    void deleteSketch(const QUuid& id);
    // Part features (Phase 3)
    void startExtrude(bool cut);
    void editFeature(const QUuid& id);
    void renameFeature(const QUuid& id, const QString& name);
    void suppressFeature(const QUuid& id, bool suppressed);
    void deleteFeature(const QUuid& id);
    void setRollback(int index);
    // Native files
    bool saveDocument(DocumentView* view, bool saveAs);
    bool confirmClose(DocumentView* view); // asks to save changes; false: cancelled
    void openPart(const QString& path);
    void runPartDemo(bool stopAtCut = false); // stopAtCut: leave the Cut-Extrude panel and preview open
    void updateSketchActions();

    // Edits through the active document (undoable)
    void renameBody(const QUuid& id, const QString& name);
    void setBodiesVisible(const QList<QUuid>& ids, bool visible);
    void recolorBodies(const QList<QUuid>& ids);
    void deleteBodies(const QList<QUuid>& ids);
    void setDocumentUnits(const Units& units);

    void onViewerSelection();
    void updateSelectionStatus();
    void updateUnitsStatus();
    void showOptions(bool documentPage = false);
    void applyViewerSettings(render::OcctViewer* viewer);
    void showMassProperties();
    void showGlInfo();
    void showAbout();
    void runDemo(const QString& demo);
    void restoreLayout();
    void saveLayout();

    std::unique_ptr<CommandRegistry> m_registry;
    CommandBar* m_commandBar = nullptr;
    FeatureTreePanel* m_tree = nullptr;
    PropertyPanel* m_panel = nullptr;
    ResourcePane* m_resources = nullptr;
    HomePage* m_home = nullptr;
    QStackedWidget* m_central = nullptr;
    QTabWidget* m_documents = nullptr;
    QTabWidget* m_managerTabs = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_statusSelection = nullptr;
    QLabel* m_statusState = nullptr;
    QToolButton* m_statusUnits = nullptr;
    std::vector<QShortcut*> m_commandKeys; // Enter / Esc / D while a command runs

    QPointer<DocumentView> m_activeView;
    std::vector<QMetaObject::Connection> m_activeConnections;
    std::unique_ptr<InteractiveCommand> m_command;
    QString m_stateText;
    QString m_smokeOut; // smoke test screenshot path (demos save next to it)
    int m_sketchTab = -1;
    int m_partCounter = 0;
    int m_importsBusy = 0;
    bool m_firstFrame = false;
};

} // namespace bl::ui
