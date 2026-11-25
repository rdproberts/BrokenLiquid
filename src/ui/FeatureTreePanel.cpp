#include "ui/FeatureTreePanel.h"

#include "doc/Document.h"
#include "ui/Icons.h"

#include <QAction>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace bl::ui {
namespace {
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kBodyRole = Qt::UserRole + 2;
} // namespace

FeatureTreePanel::FeatureTreePanel(QWidget* parent)
    : QWidget(parent), m_filter(new QLineEdit(this)), m_tree(new QTreeWidget(this)), m_empty(new QLabel(this)) {
    setObjectName(QStringLiteral("FeatureTreePanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_filter->setPlaceholderText(tr("Filter tree…"));
    m_filter->setClearButtonEnabled(true);
    layout->addWidget(m_filter);

    m_tree->setHeaderHidden(true);
    m_tree->setIconSize(QSize(18, 18));
    m_tree->setIndentation(16);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setObjectName(QStringLiteral("FeatureTree"));
    m_tree->installEventFilter(this);
    layout->addWidget(m_tree, 1);

    m_empty->setObjectName(QStringLiteral("PaneHint"));
    m_empty->setWordWrap(true);
    m_empty->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_empty->setMargin(8);
    m_empty->setText(tr("No document is open.\n\nCreate a new part or open a STEP file to see its Feature Tree here."));
    layout->addWidget(m_empty, 1);

    m_root = addNode(nullptr, QStringLiteral("doc"), tr("Part1"), QStringLiteral("tree-part"));
    addNode(m_root, QStringLiteral("history"), tr("History"), QStringLiteral("tree-folder"));
    m_bodies = addNode(m_root, QStringLiteral("bodies"), tr("Solid Bodies"), QStringLiteral("tree-folder"));
    addNode(m_root, QStringLiteral("material"), tr("Material <not specified>"), QStringLiteral("tree-material"));
    addNode(m_root, QStringLiteral("plane.front"), tr("Front Plane"), QStringLiteral("tree-plane"));
    addNode(m_root, QStringLiteral("plane.top"), tr("Top Plane"), QStringLiteral("tree-plane"));
    addNode(m_root, QStringLiteral("plane.right"), tr("Right Plane"), QStringLiteral("tree-plane"));
    addNode(m_root, QStringLiteral("origin"), tr("Origin"), QStringLiteral("tree-origin"));

    // Rollback bar: drawn now, draggable once there is a feature history (Phase 3).
    m_rollback = new QTreeWidgetItem(m_root);
    m_rollback->setText(0, QStringLiteral("━━━━━━━━━━━━━━━━"));
    m_rollback->setForeground(0, QColor(0x22, 0xC3, 0xE6));
    m_rollback->setFlags(Qt::ItemIsEnabled);
    m_rollback->setToolTip(0, tr("Rollback bar"));
    m_rollback->setData(0, kIdRole, QStringLiteral("rollback"));

    m_root->setExpanded(true);
    m_bodies->setExpanded(true);

    connect(m_filter, &QLineEdit::textChanged, this, &FeatureTreePanel::applyFilter);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        emit itemActivated(item->data(0, kIdRole).toString());
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (!m_updating) {
            emit bodySelectionChanged(selectedBodies());
        }
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        if (m_updating || !m_document) {
            return;
        }
        const QUuid id = bodyId(item);
        const doc::Body* body = id.isNull() ? nullptr : m_document->model().body(id);
        const QString name = item->text(0).trimmed();
        if (body && !name.isEmpty() && name != body->name) {
            emit renameRequested(id, name);
        } else if (body) {
            m_updating = true;
            item->setText(0, body->name); // empty name: put the old one back
            m_updating = false;
        }
    });
    connect(m_tree, &QWidget::customContextMenuRequested, this, &FeatureTreePanel::showContextMenu);
    rebuild();
}

QTreeWidgetItem* FeatureTreePanel::addNode(QTreeWidgetItem* parent, const QString& id, const QString& text,
                                           const QString& iconName) {
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, text);
    item->setIcon(0, icon(iconName, text));
    item->setData(0, kIdRole, id);
    return item;
}

QUuid FeatureTreePanel::bodyId(const QTreeWidgetItem* item) const {
    return item ? item->data(0, kBodyRole).toUuid() : QUuid();
}

void FeatureTreePanel::setDocument(doc::Document* document) {
    if (m_document == document) {
        return;
    }
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    if (m_document) {
        connect(m_document, &doc::Document::modelChanged, this, &FeatureTreePanel::rebuild);
        connect(m_document, &doc::Document::titleChanged, this, &FeatureTreePanel::rebuild);
    }
    rebuild();
}

void FeatureTreePanel::rebuild() {
    m_updating = true;
    const QList<QUuid> selected = selectedBodies();
    qDeleteAll(m_bodies->takeChildren());
    m_tree->setVisible(m_document != nullptr);
    m_filter->setVisible(m_document != nullptr);
    m_empty->setVisible(m_document == nullptr);
    m_root->setText(0, m_document ? m_document->title() : tr("No document"));

    int count = 0;
    if (m_document) {
        const QColor hidden = palette().color(QPalette::Disabled, QPalette::Text);
        for (const doc::Body& body : m_document->model().bodies) {
            auto* item = new QTreeWidgetItem(m_bodies);
            item->setText(0, body.name);
            item->setIcon(0, icon(QStringLiteral("tree-body"), body.name));
            item->setData(0, kIdRole, QStringLiteral("body"));
            item->setData(0, kBodyRole, body.id);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            QString tip = body.sourceFile.isEmpty() ? body.name : tr("%1\nImported from %2").arg(body.name, body.sourceFile);
            if (!body.visible) {
                item->setForeground(0, hidden);
                tip += tr("\n(hidden)");
            }
            item->setToolTip(0, tip);
            item->setSelected(selected.contains(body.id));
            ++count;
        }
    }
    m_bodies->setText(0, tr("Solid Bodies(%1)").arg(count));
    m_bodies->setHidden(count == 0);
    applyFilter(m_filter->text());
    m_updating = false;
}

QList<QUuid> FeatureTreePanel::selectedBodies() const {
    QList<QUuid> ids;
    for (const QTreeWidgetItem* item : m_tree->selectedItems()) {
        const QUuid id = bodyId(item);
        if (!id.isNull()) {
            ids << id;
        }
    }
    return ids;
}

void FeatureTreePanel::setSelectedBodies(const QList<QUuid>& ids) {
    m_updating = true;
    m_tree->clearSelection();
    for (int i = 0; i < m_bodies->childCount(); ++i) {
        QTreeWidgetItem* item = m_bodies->child(i);
        item->setSelected(ids.contains(bodyId(item)));
    }
    m_updating = false;
}

void FeatureTreePanel::showContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!m_document || !item || bodyId(item).isNull()) {
        return;
    }
    if (!item->isSelected()) {
        m_tree->setCurrentItem(item);
    }
    const QList<QUuid> ids = selectedBodies();
    const doc::Body* first = m_document->model().body(bodyId(item));
    const bool visible = first && first->visible;

    QMenu menu(this);
    QAction* rename = menu.addAction(tr("Rename"));
    rename->setShortcut(Qt::Key_F2);
    rename->setEnabled(ids.size() == 1);
    QAction* toggle = menu.addAction(visible ? tr("Hide") : tr("Show"));
    QAction* color = menu.addAction(tr("Appearance…"));
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Delete"));
    remove->setShortcut(QKeySequence::Delete);

    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (chosen == rename) {
        m_tree->editItem(item);
    } else if (chosen == toggle) {
        emit visibilityRequested(ids, !visible);
    } else if (chosen == color) {
        emit colorRequested(ids);
    } else if (chosen == remove) {
        emit deleteRequested(ids);
    }
}

bool FeatureTreePanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_tree && event->type() == QEvent::KeyPress) { // the inline editor gets its own keys
        const auto* key = static_cast<QKeyEvent*>(event);
        const QList<QUuid> ids = selectedBodies();
        if (!ids.isEmpty() && key->key() == Qt::Key_Delete) {
            emit deleteRequested(ids);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void FeatureTreePanel::applyFilter(const QString& text) {
    // An item stays visible if it or any child matches; parents of matches stay visible too.
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        bool any = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);
        for (int i = 0; i < item->childCount(); ++i) {
            any = visit(item->child(i)) || any;
        }
        if (item != m_root) {
            item->setHidden(!any || (item == m_bodies && item->childCount() == 0));
        }
        return any;
    };
    visit(m_root);
}

} // namespace bl::ui
