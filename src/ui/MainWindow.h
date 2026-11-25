#pragma once

#include "kernel/StepIO.h"
#include "render/OcctViewer.h"
#include "ui/Theme.h"

#include <QMainWindow>

#include <memory>

class QLabel;
class QLineEdit;
class QTabWidget;

namespace bl::ui {

class CommandRegistry;
class CommandBar;
class FeatureTreePanel;
class ResourcePane;
class ViewHud;

// Main application window with the familiar CAD layout (PLAN.md 7.1).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    CommandRegistry& commands() { return *m_registry; }
    render::OcctViewer* viewer() const { return m_viewer; }

    // Imports a STEP file on a worker thread and shows it.
    void openFile(const QString& path);

    // Test hook: once the first frame and any pending import are done, save a screenshot of the
    // window to `pngPath` and quit with exit code 0 (2 on failure).
    void runSmokeTest(const QString& pngPath);

signals:
    void ready(); // first frame rendered
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
    void showImport(const kernel::ImportResult& result, const QString& path);
    void showMassProperties();
    void showGlInfo();
    void showAbout();
    void restoreLayout();
    void saveLayout();

    std::unique_ptr<CommandRegistry> m_registry;
    render::OcctViewer* m_viewer = nullptr;
    CommandBar* m_commandBar = nullptr;
    FeatureTreePanel* m_tree = nullptr;
    ResourcePane* m_resources = nullptr;
    ViewHud* m_hud = nullptr;
    QTabWidget* m_managerTabs = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_statusState = nullptr;
    QLabel* m_statusUnits = nullptr;
    QLabel* m_statusSelection = nullptr;

    kernel::ImportResult m_model; // keeps the XCAF document alive while shown
    QString m_modelPath;
    bool m_importBusy = false;
    bool m_firstFrame = false;
};

} // namespace bl::ui
