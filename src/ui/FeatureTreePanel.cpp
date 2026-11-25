#include "ui/FeatureTreePanel.h"

#include "ui/Icons.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace bl::ui {
namespace {
constexpr int kIdRole = Qt::UserRole + 1;
}

FeatureTreePanel::FeatureTreePanel(QWidget* parent)
    : QWidget(parent), m_filter(new QLineEdit(this)), m_tree(new QTreeWidget(this)) {
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
    m_tree->setObjectName(QStringLiteral("FeatureTree"));
    layout->addWidget(m_tree, 1);

    m_root = addNode(nullptr, QStringLiteral("doc"), tr("Part1"), QStringLiteral("tree-part"));
    addNode(m_root, QStringLiteral("history"), tr("History"), QStringLiteral("tree-folder"));
    addNode(m_root, QStringLiteral("material"), tr("Material <not specified>"), QStringLiteral("tree-material"));
    addNode(m_root, QStringLiteral("plane.front"), tr("Front Plane"), QStringLiteral("tree-plane"));
    addNode(m_root, QStringLiteral("plane.top"), tr("Top Plane"), QStringLiteral("tree-plane"));
    addNode(m_root, QStringLiteral("plane.right"), tr("Right Plane"), QStringLiteral("tree-plane"));
    addNode(m_root, QStringLiteral("origin"), tr("Origin"), QStringLiteral("tree-origin"));

    // Visual placeholder for the rollback bar (interactive in Phase 3).
    m_rollback = new QTreeWidgetItem(m_root);
    m_rollback->setText(0, QStringLiteral("━━━━━━━━━━━━━━━━"));
    m_rollback->setForeground(0, QColor(0x22, 0xC3, 0xE6));
    m_rollback->setFlags(Qt::ItemIsEnabled);
    m_rollback->setToolTip(0, tr("Rollback bar"));
    m_rollback->setData(0, kIdRole, QStringLiteral("rollback"));

    m_root->setExpanded(true);

    connect(m_filter, &QLineEdit::textChanged, this, &FeatureTreePanel::applyFilter);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        emit itemActivated(item->data(0, kIdRole).toString());
    });
}

QTreeWidgetItem* FeatureTreePanel::addNode(QTreeWidgetItem* parent, const QString& id, const QString& text,
                                           const QString& iconName) {
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, text);
    item->setIcon(0, icon(iconName, text));
    item->setData(0, kIdRole, id);
    return item;
}

void FeatureTreePanel::setDocumentName(const QString& name) { m_root->setText(0, name); }

void FeatureTreePanel::addImportedBody(const QString& name, const QString& details) {
    ++m_bodyCount;
    const QString text = name.isEmpty() ? tr("Imported%1").arg(m_bodyCount) : name;
    auto* item = new QTreeWidgetItem();
    item->setText(0, text);
    item->setIcon(0, icon(QStringLiteral("tree-body"), text));
    item->setToolTip(0, details);
    item->setData(0, kIdRole, QStringLiteral("body.%1").arg(m_bodyCount));
    // Insert above the rollback bar.
    m_root->insertChild(m_root->indexOfChild(m_rollback), item);
}

void FeatureTreePanel::clearBodies() {
    for (int i = m_root->childCount() - 1; i >= 0; --i) {
        QTreeWidgetItem* child = m_root->child(i);
        if (child->data(0, kIdRole).toString().startsWith(QLatin1String("body."))) {
            delete m_root->takeChild(i);
        }
    }
    m_bodyCount = 0;
}

void FeatureTreePanel::applyFilter(const QString& text) {
    for (int i = 0; i < m_root->childCount(); ++i) {
        QTreeWidgetItem* child = m_root->child(i);
        const bool match = text.isEmpty() || child->text(0).contains(text, Qt::CaseInsensitive);
        child->setHidden(!match);
    }
}

} // namespace bl::ui
