#pragma once

#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QStringList>

#include <functional>

namespace bl::ui {

// Describes one user command. Every button, menu item, shortcut, search hit and (later) Python
// call goes through a registered command (PLAN.md 6.3).
struct CommandSpec {
    QString id;               // stable id, e.g. "view.fit" - used by scripts and keymaps
    QString label;            // "Zoom to Fit"
    QString tooltip;          // longer description; defaults to label
    QString icon;             // icon name, see Icons.h
    QKeySequence shortcut;    // default shortcut (user-rebindable later)
    QString category;         // "View", "Features", ...
    QString plannedFor;       // non-empty => not implemented yet; shown disabled with this note
    bool checkable = false;
};

class CommandRegistry : public QObject {
    Q_OBJECT
public:
    explicit CommandRegistry(QWidget* owner);

    // Registers a command. Commands with `plannedFor` set are created disabled.
    QAction* add(const CommandSpec& spec, std::function<void()> handler = {});

    QAction* action(const QString& id) const;
    const CommandSpec* spec(const QString& id) const;
    QList<QAction*> actions() const;
    QStringList ids() const { return m_order; }

    // Runs a command by id (as scripts will). Returns false if unknown or disabled.
    bool trigger(const QString& id);

    // Last ten unique commands, most recent first.
    QStringList recent() const { return m_recent; }
    QString lastCommand() const { return m_recent.isEmpty() ? QString() : m_recent.front(); }

signals:
    void commandTriggered(const QString& id);

private:
    void noteRecent(const QString& id);

    QWidget* m_owner;
    QHash<QString, QAction*> m_actions;
    QHash<QString, CommandSpec> m_specs;
    QStringList m_order;
    QStringList m_recent;
};

} // namespace bl::ui
