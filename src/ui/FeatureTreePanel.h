#pragma once

#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace bl::ui {

// Feature Tree (PLAN.md 7.4). Phase 0: static part skeleton (origin, default planes) plus
// imported bodies. Rollback bar, context menu and editing arrive in Phase 1/3.
class FeatureTreePanel : public QWidget {
    Q_OBJECT
public:
    explicit FeatureTreePanel(QWidget* parent = nullptr);

    void setDocumentName(const QString& name);
    void addImportedBody(const QString& name, const QString& details);
    void clearBodies();

signals:
    void itemActivated(const QString& id);

private:
    void applyFilter(const QString& text);
    QTreeWidgetItem* addNode(QTreeWidgetItem* parent, const QString& id, const QString& text,
                             const QString& iconName);

    QLineEdit* m_filter;
    QTreeWidget* m_tree;
    QTreeWidgetItem* m_root = nullptr;
    QTreeWidgetItem* m_rollback = nullptr;
    int m_bodyCount = 0;
};

} // namespace bl::ui
