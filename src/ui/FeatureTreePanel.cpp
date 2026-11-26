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
constexpr int kSketchRole = Qt::UserRole + 3;
constexpr int kFeatureRole = Qt::UserRole + 4;

QString iconFor(const doc::Feature& f) {
    switch (f.type) {
    case doc::FeatureType::Import: return QStringLiteral("tree-body");
    case doc::FeatureType::Sketch: return QStringLiteral("sketch");
    case doc::FeatureType::Extrude: return f.extrude.cut ? QStringLiteral("extrude-cut") : QStringLiteral("extrude");
    case doc::FeatureType::Revolve: return f.revolve.cut ? QStringLiteral("revolve-cut") : QStringLiteral("revolve");
    case doc::FeatureType::Fillet: return QStringLiteral("fillet");
    case doc::FeatureType::Chamfer: return QStringLiteral("chamfer");
    }
    return QStringLiteral("tree-folder");
}
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
        if (const QUuid id = sketchId(item); !id.isNull()) {
            emit sketchEditRequested(id);
            return;
        }
        if (const QUuid id = featureId(item); !id.isNull()) {
            emit featureEditRequested(id);
            return;
        }
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
        if (const QUuid sid = sketchId(item); !sid.isNull()) {
            const doc::SketchFeature* sketch = m_document->model().sketch(sid);
            QString name = item->text(0).trimmed();
            for (const QString& prefix : {QStringLiteral("(-) "), QStringLiteral("(+) ")}) {
                if (name.startsWith(prefix)) {
                    name = name.mid(prefix.size());
                }
            }
            if (sketch && !name.isEmpty() && name != sketch->name) {
                emit sketchRenameRequested(sid, name);
            } else {
                rebuild();
            }
            return;
        }
        if (const QUuid fid = featureId(item); !fid.isNull()) {
            const doc::Feature* f = m_document->model().feature(fid);
            const QString name = item->text(0).trimmed();
            if (f && !name.isEmpty() && name != f->name) {
                emit featureRenameRequested(fid, name);
            } else {
                rebuild();
            }
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

QUuid FeatureTreePanel::sketchId(const QTreeWidgetItem* item) const {
    return item ? item->data(0, kSketchRole).toUuid() : QUuid();
}

QUuid FeatureTreePanel::featureId(const QTreeWidgetItem* item) const {
    return item ? item->data(0, kFeatureRole).toUuid() : QUuid();
}

QUuid FeatureTreePanel::selectedFeature() const {
    for (const QTreeWidgetItem* item : m_tree->selectedItems()) {
        if (const QUuid id = featureId(item); !id.isNull()) {
            return id;
        }
        if (const QUuid id = sketchId(item); !id.isNull()) {
            return id;
        }
    }
    return {};
}

QUuid FeatureTreePanel::selectedSketch() const {
    for (const QTreeWidgetItem* item : m_tree->selectedItems()) {
        if (const QUuid id = sketchId(item); !id.isNull()) {
            return id;
        }
    }
    return {};
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
    const QUuid selectedSketchId = selectedSketch();
    const QUuid selectedFeatureId = selectedFeature();
    qDeleteAll(m_bodies->takeChildren());
    qDeleteAll(m_historyItems);
    m_historyItems.clear();
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
    if (m_document) {
        // The history after the origin: features in order, each feature's sketch under it, and
        // the rollback bar between what is built and what is rolled back.
        const doc::PartModel& model = m_document->model();
        m_root->removeChild(m_rollback);
        m_root->addChild(m_rollback);
        const int rollback = model.rollback;
        for (size_t i = 0; i < model.features.size(); ++i) {
            const doc::Feature& f = model.features[i];
            if (f.type == doc::FeatureType::Sketch && model.consumerOf(f.id)) {
                continue; // shown under its feature
            }
            QTreeWidgetItem* item = historyItem(f, nullptr);
            if (rollback >= 0 && static_cast<int>(i) >= rollback) {
                m_root->addChild(item); // below the bar
            } else {
                m_root->insertChild(m_root->indexOfChild(m_rollback), item);
            }
            m_historyItems << item;
            if (!f.sketchUsed().isNull()) {
                if (const doc::Feature* sketch = model.feature(f.sketchUsed())) {
                    historyItem(*sketch, item);
                }
            }
        }
        // Put the selection back.
        std::function<void(QTreeWidgetItem*)> reselect = [&](QTreeWidgetItem* item) {
            const QUuid id = !sketchId(item).isNull() ? sketchId(item) : featureId(item);
            if (!id.isNull() && (id == selectedSketchId || id == selectedFeatureId)) {
                item->setSelected(true);
            }
            for (int c = 0; c < item->childCount(); ++c) {
                reselect(item->child(c));
            }
        };
        for (QTreeWidgetItem* item : m_historyItems) {
            reselect(item);
        }
    }
    m_bodies->setText(0, tr("Solid Bodies(%1)").arg(count));
    m_bodies->setHidden(count == 0);
    applyFilter(m_filter->text());
    m_updating = false;
}

QTreeWidgetItem* FeatureTreePanel::historyItem(const doc::Feature& f, QTreeWidgetItem* parent) {
    const doc::PartModel& model = m_document->model();
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem();
    const QColor disabled = palette().color(QPalette::Disabled, QPalette::Text);
    const QString name = model.featureName(f);
    QString tip;
    if (f.type == doc::FeatureType::Sketch) {
        const doc::SketchFeature* sketch = model.sketch(f.id);
        QString prefix;
        QString state = tr("Fully defined");
        if (sketch->status == ::bl::sketch::SketchStatus::UnderDefined) {
            prefix = QStringLiteral("(-) ");
            state = tr("Under defined");
        } else if (sketch->status == ::bl::sketch::SketchStatus::OverDefined) {
            prefix = QStringLiteral("(+) ");
            state = tr("Over defined");
        }
        item->setText(0, prefix + name);
        item->setData(0, kIdRole, QStringLiteral("sketch"));
        item->setData(0, kSketchRole, sketch->id);
        tip = tr("%1 on %2\n%3. Double-click to edit.").arg(name, sketch->planeName, state);
        if (!sketch->visible) {
            item->setForeground(0, disabled);
        }
    } else {
        item->setText(0, name);
        item->setData(0, kIdRole, QStringLiteral("feature"));
        item->setData(0, kFeatureRole, f.id);
        tip = f.type == doc::FeatureType::Import ? tr("%1\nImported from %2").arg(name, f.import.sourceFile)
                                                 : tr("%1\nDouble-click to edit.").arg(name);
    }
    item->setIcon(0, icon(iconFor(f), name));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    switch (f.state) {
    case doc::FeatureState::Ok: break;
    case doc::FeatureState::Warning:
        item->setForeground(0, QColor(0xD0, 0x8A, 0x10));
        tip += QStringLiteral("\n") + f.message;
        break;
    case doc::FeatureState::Error:
        item->setForeground(0, QColor(0xD0, 0x30, 0x30));
        tip += QStringLiteral("\n") + f.message;
        break;
    case doc::FeatureState::Suppressed:
        item->setForeground(0, disabled);
        tip += tr("\n(suppressed)");
        break;
    case doc::FeatureState::RolledBack:
        item->setForeground(0, disabled);
        tip += tr("\n(rolled back)");
        break;
    }
    item->setToolTip(0, tip);
    return item;
}

void FeatureTreePanel::showFeatureMenu(QTreeWidgetItem* item, const QPoint& globalPos) {
    const QUuid id = featureId(item);
    const doc::Feature* f = m_document ? m_document->model().feature(id) : nullptr;
    if (!f) {
        return;
    }
    const doc::PartModel& model = m_document->model();
    const int index = model.featureIndex(id);
    QMenu menu(this);
    QAction* edit = nullptr;
    if (f->type != doc::FeatureType::Import && f->type != doc::FeatureType::Sketch) {
        edit = menu.addAction(icon(iconFor(*f)), tr("Edit Feature"));
    }
    QAction* rename = menu.addAction(tr("Rename"));
    rename->setShortcut(Qt::Key_F2);
    QAction* suppress = menu.addAction(f->suppressed ? tr("Unsuppress") : tr("Suppress"));
    menu.addSeparator();
    QAction* rollHere = menu.addAction(tr("Roll Back"));
    rollHere->setToolTip(tr("Move the rollback bar above this feature"));
    QAction* rollEnd = menu.addAction(tr("Roll to End"));
    rollEnd->setEnabled(model.rollback >= 0);
    menu.addSeparator();
    QAction* remove = menu.addAction(tr("Delete"));
    remove->setShortcut(QKeySequence::Delete);
    const bool suppressed = f->suppressed;
    QAction* chosen = menu.exec(globalPos);
    if (!chosen) {
        return;
    }
    if (chosen == edit) {
        emit featureEditRequested(id);
    } else if (chosen == rename) {
        m_updating = true;
        item->setText(0, model.feature(id)->name);
        m_updating = false;
        m_tree->editItem(item);
    } else if (chosen == suppress) {
        emit featureSuppressRequested(id, !suppressed);
    } else if (chosen == rollHere) {
        emit rollbackRequested(index);
    } else if (chosen == rollEnd) {
        emit rollbackRequested(-1);
    } else if (chosen == remove) {
        emit featureDeleteRequested(id);
    }
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
    if (m_document && !featureId(item).isNull()) {
        showFeatureMenu(item, m_tree->viewport()->mapToGlobal(pos));
        return;
    }
    if (const QUuid sid = sketchId(item); m_document && !sid.isNull()) {
        const doc::SketchFeature* sketch = m_document->model().sketch(sid);
        if (!sketch) {
            return;
        }
        QMenu menu(this);
        QAction* edit = menu.addAction(icon(QStringLiteral("sketch")), tr("Edit Sketch"));
        QAction* rename = menu.addAction(tr("Rename"));
        rename->setShortcut(Qt::Key_F2);
        QAction* toggle = menu.addAction(sketch->visible ? tr("Hide") : tr("Show"));
        menu.addSeparator();
        QAction* remove = menu.addAction(tr("Delete"));
        remove->setShortcut(QKeySequence::Delete);
        const bool visible = sketch->visible;
        QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (chosen == edit) {
            emit sketchEditRequested(sid);
        } else if (chosen == rename) {
            m_updating = true;
            item->setText(0, m_document->model().sketch(sid)->name);
            m_updating = false;
            m_tree->editItem(item);
        } else if (chosen == toggle) {
            emit sketchVisibilityRequested(sid, !visible);
        } else if (chosen == remove) {
            emit sketchDeleteRequested(sid);
        }
        return;
    }
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
        if (const QUuid sid = selectedSketch(); !sid.isNull() && key->key() == Qt::Key_Delete) {
            emit sketchDeleteRequested(sid);
            return true;
        }
        if (const QUuid fid = selectedFeature(); !fid.isNull() && key->key() == Qt::Key_Delete) {
            emit featureDeleteRequested(fid);
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
