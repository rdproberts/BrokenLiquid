#pragma once

#include <QWidget>

class QFrame;
class QVBoxLayout;

namespace bl::ui {

class CommandRegistry;

// Home screen (PLAN.md 7.2): shown in the graphics area when no document is open.
// New / Open, recent files, a short "getting around" card and, on first run, a setup card.
class HomePage : public QWidget {
    Q_OBJECT
public:
    HomePage(CommandRegistry& registry, QWidget* parent = nullptr);

    void refresh(); // re-reads recent files and first-run state from Settings

signals:
    void openRecentRequested(const QString& path);
    void settingsChanged(); // first-run card applied theme/units

private:
    QWidget* makeActions();
    QWidget* makeFirstRun();
    QWidget* makeTips();

    CommandRegistry& m_registry;
    QVBoxLayout* m_recentList = nullptr;
    QWidget* m_firstRun = nullptr;
};

} // namespace bl::ui
