#include "ui/PropertyPanel.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace bl::ui {

// --- LengthField ---------------------------------------------------------------------------------

LengthField::LengthField(QWidget* parent) : QLineEdit(parent) {
    setObjectName(QStringLiteral("LengthField"));
    setToolTip(tr("Type a value with or without units (20, 20mm, 1in, =40/2).\n"
                  "Mouse wheel steps the value; hold Ctrl for fine steps."));
    connect(this, &QLineEdit::returnPressed, this, &LengthField::commit);
    refresh();
}

void LengthField::setValue(double mm) {
    mm = std::clamp(mm, m_min, m_max);
    const bool changed = std::abs(mm - m_mm) > 1e-12;
    m_mm = mm;
    refresh();
    if (changed) {
        emit valueChanged(m_mm);
    }
}

void LengthField::setUnits(const Units& units) {
    m_units = units;
    refresh();
}

void LengthField::setRange(double minMm, double maxMm) {
    m_min = minMm;
    m_max = maxMm;
    setValue(m_mm);
}

void LengthField::refresh() {
    setText(QString::fromStdString(units::formatLength(m_mm, m_units)));
    setProperty("invalid", false);
    style()->unpolish(this);
    style()->polish(this);
}

void LengthField::commit() {
    const auto v = units::parseLength(text().toStdString(), m_units);
    if (!v) {
        setProperty("invalid", true); // red border via QSS; keep the text so the user can fix it
        style()->unpolish(this);
        style()->polish(this);
        return;
    }
    setValue(*v);
    refresh();
}

void LengthField::focusOutEvent(QFocusEvent* event) {
    commit();
    QLineEdit::focusOutEvent(event);
}

void LengthField::wheelEvent(QWheelEvent* event) {
    if (!hasFocus()) {
        QLineEdit::wheelEvent(event);
        return;
    }
    const double notches = event->angleDelta().y() / 120.0;
    double step = m_step * units::lengthFactor(m_units.system);
    if (event->modifiers() & Qt::ControlModifier) {
        step *= 0.1;
    }
    setValue(m_mm + notches * step);
    event->accept();
}

// --- SelectionBox ----------------------------------------------------------------------------------

SelectionBox::SelectionBox(const QString& placeholder, QWidget* parent) : QListWidget(parent), m_placeholder(placeholder) {
    setObjectName(QStringLiteral("SelectionBox"));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::ClickFocus);
    setActive(false);
}

void SelectionBox::setEntries(const QStringList& entries) {
    clear();
    addItems(entries);
    viewport()->update();
}

void SelectionBox::setActive(bool active) {
    m_active = active;
    setProperty("active", active);
    style()->unpolish(this);
    style()->polish(this);
    viewport()->update();
}

void SelectionBox::mousePressEvent(QMouseEvent* event) {
    if (!m_active) {
        emit activated();
    }
    QListWidget::mousePressEvent(event);
}

void SelectionBox::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QListWidgetItem* item = itemAt(event->pos());
    QAction* del = menu.addAction(tr("Delete"));
    del->setEnabled(item != nullptr);
    QAction* clearAll = menu.addAction(tr("Clear Selections"));
    clearAll->setEnabled(count() > 0);
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == del && item) {
        emit removeRequested(row(item));
    } else if (chosen == clearAll) {
        emit clearRequested();
    }
}

void SelectionBox::paintEvent(QPaintEvent* event) {
    QListWidget::paintEvent(event);
    if (count() == 0) {
        QPainter p(viewport());
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(viewport()->rect().adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   m_placeholder);
    }
}

// --- PanelGroup ------------------------------------------------------------------------------------

PanelGroup::PanelGroup(const QString& title, bool checkable, QWidget* parent)
    : QFrame(parent), m_header(new QToolButton(this)), m_body(new QWidget(this)), m_form(new QFormLayout(m_body)) {
    setObjectName(QStringLiteral("PanelGroup"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(4, 2, 4, 2);
    m_header->setObjectName(QStringLiteral("PanelGroupHeader"));
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setArrowType(Qt::DownArrow);
    m_header->setCheckable(true);
    m_header->setChecked(true);
    m_header->setAutoRaise(true);
    if (checkable) {
        m_check = new QCheckBox(title, this);
        m_check->setChecked(true);
        m_header->setText(QString());
        headerRow->addWidget(m_header);
        headerRow->addWidget(m_check, 1);
        connect(m_check, &QCheckBox::toggled, this, [this](bool on) {
            m_body->setEnabled(on);
            emit toggled(on);
        });
    } else {
        m_header->setText(title);
        headerRow->addWidget(m_header, 1);
    }
    layout->addLayout(headerRow);

    m_form->setContentsMargins(10, 4, 8, 8);
    m_form->setLabelAlignment(Qt::AlignLeft);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(m_body);

    connect(m_header, &QToolButton::toggled, this, &PanelGroup::setExpanded);
}

bool PanelGroup::isChecked() const { return !m_check || m_check->isChecked(); }

void PanelGroup::setChecked(bool on) {
    if (m_check) {
        m_check->setChecked(on);
    }
}

void PanelGroup::setExpanded(bool expanded) {
    m_header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    m_body->setVisible(expanded);
    if (m_header->isChecked() != expanded) {
        m_header->setChecked(expanded);
    }
}

SelectionBox* PanelGroup::addSelectionBox(const QString& placeholder, int rows) {
    auto* box = new SelectionBox(placeholder, m_body);
    box->setFixedHeight(std::max(2, rows) * 20 + 8);
    m_form->addRow(box);
    return box;
}

LengthField* PanelGroup::addLength(const QString& label, double mm, const Units& units) {
    auto* field = new LengthField(m_body);
    field->setUnits(units);
    field->setValue(mm);
    m_form->addRow(label, field);
    return field;
}

QCheckBox* PanelGroup::addCheck(const QString& text, bool checked) {
    auto* box = new QCheckBox(text, m_body);
    box->setChecked(checked);
    m_form->addRow(box);
    return box;
}

QComboBox* PanelGroup::addChoice(const QString& label, const QStringList& items, int current) {
    auto* combo = new QComboBox(m_body);
    combo->addItems(items);
    combo->setCurrentIndex(current);
    if (label.isEmpty()) {
        m_form->addRow(combo);
    } else {
        m_form->addRow(label, combo);
    }
    return combo;
}

QLabel* PanelGroup::addText(const QString& text) {
    auto* label = new QLabel(text, m_body);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_form->addRow(label);
    return label;
}

void PanelGroup::addRow(const QString& label, QWidget* widget) {
    widget->setParent(m_body);
    if (label.isEmpty()) {
        m_form->addRow(widget);
    } else {
        m_form->addRow(label, widget);
    }
}

// --- PropertyPanel ---------------------------------------------------------------------------------

PropertyPanel::PropertyPanel(QWidget* parent)
    : QWidget(parent), m_icon(new QLabel(this)), m_title(new QLabel(this)), m_ok(new QToolButton(this)),
      m_cancel(new QToolButton(this)), m_message(new QLabel(this)), m_content(new QWidget()),
      m_contentLayout(new QVBoxLayout(m_content)), m_idle(new QLabel(this)) {
    setObjectName(QStringLiteral("PropertyPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(6);

    auto* header = new QHBoxLayout();
    header->setSpacing(6);
    m_icon->setFixedSize(22, 22);
    m_title->setObjectName(QStringLiteral("PanelTitle"));
    m_ok->setObjectName(QStringLiteral("PanelOk"));
    m_cancel->setObjectName(QStringLiteral("PanelCancel"));
    m_ok->setText(QStringLiteral("✔"));
    m_cancel->setText(QStringLiteral("✖"));
    m_ok->setToolTip(tr("OK (Enter)"));
    m_cancel->setToolTip(tr("Cancel (Esc)"));
    for (QToolButton* b : {m_ok, m_cancel}) {
        b->setAutoRaise(true);
        b->setFixedSize(28, 26);
    }
    header->addWidget(m_icon);
    header->addWidget(m_title, 1);
    header->addWidget(m_ok);
    header->addWidget(m_cancel);
    layout->addLayout(header);

    m_message->setObjectName(QStringLiteral("PanelMessage"));
    m_message->setWordWrap(true);
    layout->addWidget(m_message);

    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(6);
    m_contentLayout->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("PanelScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_content);
    layout->addWidget(scroll, 1);

    m_idle->setObjectName(QStringLiteral("PaneHint"));
    m_idle->setWordWrap(true);
    m_idle->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_idle->setText(tr("Commands that need input (Measure, Section View, and later every feature) "
                       "show their options here."));
    layout->addWidget(m_idle);

    connect(m_ok, &QToolButton::clicked, this, &PropertyPanel::okClicked);
    connect(m_cancel, &QToolButton::clicked, this, &PropertyPanel::cancelClicked);
    clear();
}

void PropertyPanel::begin(const QString& title, const QString& iconName, bool confirm) {
    clear();
    m_open = true;
    m_icon->setPixmap(icon(iconName, title).pixmap(22, 22));
    m_title->setText(title);
    m_ok->setVisible(confirm);
    m_cancel->setToolTip(confirm ? tr("Cancel (Esc)") : tr("Close (Esc)"));
    for (QWidget* w : std::initializer_list<QWidget*>{m_icon, m_title, m_cancel}) {
        w->show();
    }
    m_content->parentWidget()->parentWidget()->show(); // scroll area
    m_idle->hide();
}

void PropertyPanel::clear() {
    m_open = false;
    while (m_contentLayout->count() > 1) {
        QLayoutItem* item = m_contentLayout->takeAt(0);
        if (QWidget* w = item->widget()) {
            // Later: the widget may be the sender of the signal that ended the command.
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    m_message->clear();
    m_message->hide();
    for (QWidget* w : std::initializer_list<QWidget*>{m_icon, m_title, m_ok, m_cancel}) {
        w->hide();
    }
    m_content->parentWidget()->parentWidget()->hide();
    m_idle->show();
}

void PropertyPanel::setMessage(const QString& text) {
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
}

PanelGroup* PropertyPanel::addGroup(const QString& title, bool checkable) {
    auto* group = new PanelGroup(title, checkable, m_content);
    m_contentLayout->insertWidget(m_contentLayout->count() - 1, group);
    return group;
}

} // namespace bl::ui
