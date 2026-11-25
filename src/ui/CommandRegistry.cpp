#include "ui/CommandRegistry.h"

#include "ui/Icons.h"

#include <spdlog/spdlog.h>

#include <QWidget>

namespace bl::ui {

CommandRegistry::CommandRegistry(QWidget* owner) : QObject(owner), m_owner(owner) {}

QAction* CommandRegistry::add(const CommandSpec& spec, std::function<void()> handler) {
    if (m_actions.contains(spec.id)) {
        spdlog::warn("command '{}' registered twice", spec.id.toStdString());
        return m_actions.value(spec.id);
    }
    auto* action = new QAction(icon(spec.icon.isEmpty() ? spec.id : spec.icon, spec.label), spec.label, m_owner);
    action->setObjectName(spec.id);
    action->setCheckable(spec.checkable);
    if (!spec.shortcut.isEmpty()) {
        action->setShortcut(spec.shortcut);
        action->setShortcutContext(Qt::WindowShortcut);
    }

    QString tip = spec.tooltip.isEmpty() ? spec.label : spec.tooltip;
    if (!spec.shortcut.isEmpty()) {
        tip += QStringLiteral("  (%1)").arg(spec.shortcut.toString(QKeySequence::NativeText));
    }
    if (!spec.plannedFor.isEmpty()) {
        tip += QStringLiteral("\nComing in %1.").arg(spec.plannedFor);
        action->setEnabled(false);
    }
    action->setToolTip(tip);
    action->setStatusTip(spec.tooltip.isEmpty() ? spec.label : spec.tooltip);

    const QString id = spec.id;
    connect(action, &QAction::triggered, this, [this, id, handler] {
        if (handler) {
            handler();
        }
        noteRecent(id);
        emit commandTriggered(id);
    });

    m_owner->addAction(action); // makes the shortcut work anywhere in the window
    m_actions.insert(spec.id, action);
    m_specs.insert(spec.id, spec);
    m_order << spec.id;
    return action;
}

QAction* CommandRegistry::action(const QString& id) const { return m_actions.value(id, nullptr); }

const CommandSpec* CommandRegistry::spec(const QString& id) const {
    auto it = m_specs.constFind(id);
    return it == m_specs.constEnd() ? nullptr : &it.value();
}

QList<QAction*> CommandRegistry::actions() const {
    QList<QAction*> list;
    list.reserve(m_order.size());
    for (const QString& id : m_order) {
        list << m_actions.value(id);
    }
    return list;
}

bool CommandRegistry::trigger(const QString& id) {
    QAction* a = action(id);
    if (!a || !a->isEnabled()) {
        return false;
    }
    a->trigger();
    return true;
}

void CommandRegistry::noteRecent(const QString& id) {
    // View commands are not interesting as "repeat last command" targets.
    if (id.startsWith(QLatin1String("view.")) || id == QLatin1String("command.repeat")) {
        return;
    }
    m_recent.removeAll(id);
    m_recent.prepend(id);
    while (m_recent.size() > 10) {
        m_recent.removeLast();
    }
}

} // namespace bl::ui
