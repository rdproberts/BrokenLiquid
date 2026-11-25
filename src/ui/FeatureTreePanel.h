#pragma once

#include "doc/Document.h"

#include <QList>
#include <QPointer>
#include <QUuid>
#include <QWidget>

class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace bl::ui {

// Feature Tree (PLAN.md 7.4) for the active document: part skeleton (history, bodies, material,
// default planes, origin, rollback bar). Bodies can be renamed (F2), hidden, recoloured and
// deleted; the panel only asks, the main window turns requests into undoable transactions.
class FeatureTreePanel : public QWidget {
    Q_OBJECT
public:
    explicit FeatureTreePanel(QWidget* parent = nullptr);

    void setDocument(doc::Document* document); // null: empty panel (Home screen)
    doc::Document* document() const { return m_document; }

    QList<QUuid> selectedBodies() const;
    void setSelectedBodies(const QList<QUuid>& ids); // from the graphics area; emits nothing
    QUuid selectedSketch() const; // the first selected sketch, if any

signals:
    void itemActivated(const QString& id);
    void bodySelectionChanged(const QList<QUuid>& ids);
    void renameRequested(const QUuid& id, const QString& name);
    void visibilityRequested(const QList<QUuid>& ids, bool visible);
    void colorRequested(const QList<QUuid>& ids);
    void deleteRequested(const QList<QUuid>& ids);
    void sketchEditRequested(const QUuid& id);
    void sketchRenameRequested(const QUuid& id, const QString& name);
    void sketchVisibilityRequested(const QUuid& id, bool visible);
    void sketchDeleteRequested(const QUuid& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void applyFilter(const QString& text);
    void showContextMenu(const QPoint& pos);
    QTreeWidgetItem* addNode(QTreeWidgetItem* parent, const QString& id, const QString& text,
                             const QString& iconName);
    QUuid bodyId(const QTreeWidgetItem* item) const;
    QUuid sketchId(const QTreeWidgetItem* item) const;

    QPointer<doc::Document> m_document;
    QLineEdit* m_filter;
    QTreeWidget* m_tree;
    QLabel* m_empty;
    QTreeWidgetItem* m_root = nullptr;
    QTreeWidgetItem* m_bodies = nullptr;
    QTreeWidgetItem* m_rollback = nullptr;
    QList<QTreeWidgetItem*> m_sketchItems;
    bool m_updating = false;
};

} // namespace bl::ui
