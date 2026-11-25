#include "ui/ViewHud.h"

#include "ui/CommandRegistry.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

namespace bl::ui {
namespace {
QToolButton* hudButton(QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setAutoRaise(true);
    b->setIconSize(QSize(20, 20));
    b->setObjectName(QStringLiteral("HudButton"));
    return b;
}
} // namespace

ViewHud::ViewHud(CommandRegistry& registry, QWidget* viewport)
    : QWidget(viewport), m_registry(registry), m_viewport(viewport) {
    setObjectName(QStringLiteral("ViewHud"));
    setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(1);
    viewport->installEventFilter(this);
    raise();
}

void ViewHud::addCommand(const QString& id) {
    QAction* a = m_registry.action(id);
    if (!a) {
        return;
    }
    QToolButton* b = hudButton(this);
    b->setDefaultAction(a);
    layout()->addWidget(b);
    adjustSize();
    reposition();
}

void ViewHud::addFlyout(const QString& iconName, const QString& tooltip, const QStringList& ids) {
    QToolButton* b = hudButton(this);
    b->setIcon(icon(iconName, tooltip));
    b->setToolTip(tooltip);
    b->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(b);
    for (const QString& id : ids) {
        if (QAction* a = m_registry.action(id)) {
            menu->addAction(a);
        }
    }
    b->setMenu(menu);
    layout()->addWidget(b);
    adjustSize();
    reposition();
}

void ViewHud::addSeparator() {
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setObjectName(QStringLiteral("HudSeparator"));
    line->setFixedHeight(18);
    layout()->addWidget(line);
}

bool ViewHud::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_viewport && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}

void ViewHud::reposition() {
    adjustSize();
    move((m_viewport->width() - width()) / 2, 6);
    raise();
}

void ViewHud::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = currentTheme() == ThemeKind::Dark;
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    p.fillPath(path, dark ? QColor(30, 35, 42, 170) : QColor(255, 255, 255, 150));
    p.setPen(QPen(dark ? QColor(255, 255, 255, 40) : QColor(31, 58, 90, 45), 1));
    p.drawPath(path);
}

} // namespace bl::ui
