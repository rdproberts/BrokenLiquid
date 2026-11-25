#pragma once

#include <QStringList>
#include <QWidget>

class QTabBar;
class QStackedWidget;
class QHBoxLayout;

namespace bl::ui {

class CommandRegistry;

// The Command Bar (PLAN.md 7.2): tabs of large icon+label buttons, grouped and separated,
// with optional flyout buttons. Content depends on the document type.
class CommandBar : public QWidget {
    Q_OBJECT
public:
    explicit CommandBar(CommandRegistry& registry, QWidget* parent = nullptr);

    int addTab(const QString& title);
    // Adds a group of command buttons to a tab. Ids that are not registered are skipped.
    void addGroup(int tab, const QStringList& commandIds);
    // Adds one button whose arrow opens the other commands; clicking it runs the first command.
    void addFlyout(int tab, const QString& title, const QStringList& commandIds);

    void setCurrentTab(int index);
    void clearTabs();

private:
    QHBoxLayout* rowOf(int tab);
    void addSeparatorIfNeeded(QHBoxLayout* row);

    CommandRegistry& m_registry;
    QTabBar* m_tabs;
    QStackedWidget* m_pages;
};

} // namespace bl::ui
