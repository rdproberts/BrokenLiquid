#pragma once

#include <QTabWidget>

namespace bl::ui {

class CommandRegistry;

// Resource Pane (PLAN.md 3.2): right-hand tabbed pane. Phase 0 ships Home and File Explorer;
// View Palette, Appearances and Properties are placeholders.
class ResourcePane : public QTabWidget {
    Q_OBJECT
public:
    ResourcePane(CommandRegistry& registry, QWidget* parent = nullptr);

signals:
    void openFileRequested(const QString& path);

private:
    QWidget* makeHomePage(CommandRegistry& registry);
    QWidget* makeFileExplorer();
    QWidget* makePlaceholder(const QString& title, const QString& text);
};

} // namespace bl::ui
