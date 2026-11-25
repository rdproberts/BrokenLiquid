#include "ui/CommandBar.h"

#include "ui/CommandRegistry.h"
#include "ui/Icons.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace bl::ui {
namespace {
constexpr int kIconSize = 30;

QToolButton* makeButton(QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setIconSize(QSize(kIconSize, kIconSize));
    b->setAutoRaise(true);
    b->setObjectName(QStringLiteral("CommandButton"));
    b->setMinimumWidth(58);
    return b;
}
} // namespace

CommandBar::CommandBar(CommandRegistry& registry, QWidget* parent)
    : QWidget(parent), m_registry(registry), m_tabs(new QTabBar(this)), m_pages(new QStackedWidget(this)) {
    setObjectName(QStringLiteral("CommandBar"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_pages->setObjectName(QStringLiteral("CommandBarPages"));
    m_tabs->setObjectName(QStringLiteral("CommandBarTabs"));
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);

    layout->addWidget(m_pages);
    layout->addWidget(m_tabs); // tabs below the buttons, like the familiar CAD layout

    connect(m_tabs, &QTabBar::currentChanged, m_pages, &QStackedWidget::setCurrentIndex);
}

int CommandBar::addTab(const QString& title) {
    auto* page = new QWidget(m_pages);
    auto* row = new QHBoxLayout(page);
    row->setContentsMargins(6, 4, 6, 2);
    row->setSpacing(2);
    row->addStretch(1);
    m_pages->addWidget(page);
    return m_tabs->addTab(title);
}

QHBoxLayout* CommandBar::rowOf(int tab) {
    QWidget* page = m_pages->widget(tab);
    return page ? static_cast<QHBoxLayout*>(page->layout()) : nullptr;
}

void CommandBar::addSeparatorIfNeeded(QHBoxLayout* row) {
    if (row->count() <= 1) { // only the trailing stretch
        return;
    }
    auto* line = new QFrame(row->parentWidget());
    line->setFrameShape(QFrame::VLine);
    line->setObjectName(QStringLiteral("CommandBarSeparator"));
    row->insertWidget(row->count() - 1, line);
}

void CommandBar::addGroup(int tab, const QStringList& commandIds) {
    QHBoxLayout* row = rowOf(tab);
    if (!row) {
        return;
    }
    addSeparatorIfNeeded(row);
    for (const QString& id : commandIds) {
        QAction* action = m_registry.action(id);
        if (!action) {
            continue;
        }
        QToolButton* b = makeButton(row->parentWidget());
        b->setDefaultAction(action);
        row->insertWidget(row->count() - 1, b);
    }
}

void CommandBar::addFlyout(int tab, const QString& title, const QStringList& commandIds) {
    QHBoxLayout* row = rowOf(tab);
    if (!row || commandIds.isEmpty()) {
        return;
    }
    QAction* primary = m_registry.action(commandIds.front());
    if (!primary) {
        return;
    }
    QToolButton* b = makeButton(row->parentWidget());
    b->setPopupMode(QToolButton::MenuButtonPopup);
    auto* menu = new QMenu(b);
    for (const QString& id : commandIds) {
        if (QAction* a = m_registry.action(id)) {
            menu->addAction(a);
        }
    }
    b->setMenu(menu);
    b->setDefaultAction(primary);
    b->setText(title);
    // Keep the flyout usable (menu opens) even when the primary command is not implemented yet.
    b->setEnabled(true);
    row->insertWidget(row->count() - 1, b);
}

void CommandBar::setCurrentTab(int index) { m_tabs->setCurrentIndex(index); }

void CommandBar::clearTabs() {
    while (m_tabs->count() > 0) {
        m_tabs->removeTab(0);
    }
    while (m_pages->count() > 0) {
        QWidget* w = m_pages->widget(0);
        m_pages->removeWidget(w);
        w->deleteLater();
    }
}

} // namespace bl::ui
