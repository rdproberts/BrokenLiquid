#include "ui/ConfirmCorner.h"

#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

namespace bl::ui {

ConfirmCorner::ConfirmCorner(QWidget* viewport)
    : QWidget(viewport), m_viewport(viewport), m_ok(new QToolButton(this)), m_cancel(new QToolButton(this)) {
    setObjectName(QStringLiteral("ConfirmCorner"));
    setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    m_ok->setObjectName(QStringLiteral("CornerOk"));
    m_cancel->setObjectName(QStringLiteral("CornerCancel"));
    m_ok->setText(QStringLiteral("✔"));
    m_cancel->setText(QStringLiteral("✖"));
    m_ok->setToolTip(tr("OK (Enter)"));
    m_cancel->setToolTip(tr("Cancel (Esc)"));
    for (QToolButton* b : {m_ok, m_cancel}) {
        b->setFixedSize(34, 30);
        b->setAutoRaise(true);
        layout->addWidget(b);
    }
    connect(m_ok, &QToolButton::clicked, this, &ConfirmCorner::accepted);
    connect(m_cancel, &QToolButton::clicked, this, &ConfirmCorner::cancelled);
    viewport->installEventFilter(this);
    hide();
}

void ConfirmCorner::showFor(bool confirm) {
    m_ok->setVisible(confirm);
    m_cancel->setToolTip(confirm ? tr("Cancel (Esc)") : tr("Close (Esc)"));
    m_pinnedToCursor = false;
    adjustSize();
    placeInCorner();
    show();
    raise();
}

void ConfirmCorner::moveToCursor() {
    if (!isVisible()) {
        return;
    }
    const QPoint p = m_viewport->mapFromGlobal(QCursor::pos()) - QPoint(width() / 2, height() / 2);
    move(p.x(), std::max(0, p.y()));
    m_pinnedToCursor = true;
}

void ConfirmCorner::placeInCorner() {
    move(m_viewport->width() - width() - 10, 10);
}

bool ConfirmCorner::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_viewport && event->type() == QEvent::Resize && !m_pinnedToCursor) {
        placeInCorner();
    }
    return QWidget::eventFilter(watched, event);
}

void ConfirmCorner::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    QColor bg = palette().color(QPalette::Window);
    bg.setAlpha(225);
    p.fillPath(path, bg);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.drawPath(path);
}

} // namespace bl::ui
