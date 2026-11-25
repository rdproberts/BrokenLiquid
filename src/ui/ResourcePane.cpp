#include "ui/ResourcePane.h"

#include "ui/CommandRegistry.h"
#include "ui/Icons.h"

#include <QFileSystemModel>
#include <QHeaderView>
#include <QLabel>
#include <QStandardPaths>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace bl::ui {

ResourcePane::ResourcePane(CommandRegistry& registry, QWidget* parent) : QTabWidget(parent) {
    setObjectName(QStringLiteral("ResourcePane"));
    setTabPosition(QTabWidget::East);
    setDocumentMode(true);
    setIconSize(QSize(20, 20));

    auto addPage = [this](QWidget* page, const QString& iconName, const QString& title) {
        const int i = addTab(page, icon(iconName, title), QString());
        setTabToolTip(i, title);
    };
    addPage(makeHomePage(registry), QStringLiteral("pane-home"), tr("Home"));
    addPage(makeFileExplorer(), QStringLiteral("pane-files"), tr("File Explorer"));
    addPage(makePlaceholder(tr("View Palette"),
                            tr("Drag standard views onto a drawing sheet.\n\nComing in Phase 5 (Drawings).")),
            QStringLiteral("pane-views"), tr("View Palette"));
    addPage(makePlaceholder(tr("Appearances"), tr("Colours and materials for faces, bodies and parts.\n\n"
                                                  "Coming in Phase 3 (Part modelling).")),
            QStringLiteral("pane-appearance"), tr("Appearances"));
    addPage(makePlaceholder(tr("Properties"),
                            tr("Part number, description and other custom properties.\n\nComing in Phase 3.")),
            QStringLiteral("pane-properties"), tr("Properties"));
}

QWidget* ResourcePane::makeHomePage(CommandRegistry& registry) {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    auto* title = new QLabel(tr("<b>BrokenLiquid</b>"), page);
    title->setObjectName(QStringLiteral("PaneTitle"));
    layout->addWidget(title);

    for (const char* id : {"file.open", "file.new.part", "file.new.assembly", "file.new.drawing"}) {
        if (QAction* a = registry.action(QString::fromLatin1(id))) {
            auto* b = new QToolButton(page);
            b->setDefaultAction(a);
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setIconSize(QSize(22, 22));
            b->setAutoRaise(true);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            layout->addWidget(b);
        }
    }
    auto* hint = new QLabel(tr("Tip: drop a part (.blpart) or STEP file anywhere on the window to open it.\n\n"
                               "Middle-drag rotates, Ctrl+middle-drag pans, the wheel zooms, F fits."),
                            page);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("PaneHint"));
    layout->addSpacing(8);
    layout->addWidget(hint);
    layout->addStretch(1);
    return page;
}

QWidget* ResourcePane::makeFileExplorer() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 6, 4, 4);

    auto* title = new QLabel(tr("<b>File Explorer</b>"), page);
    title->setObjectName(QStringLiteral("PaneTitle"));
    layout->addWidget(title);

    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    auto* model = new QFileSystemModel(page);
    model->setRootPath(documents);
    model->setNameFilters({QStringLiteral("*.step"), QStringLiteral("*.stp"), QStringLiteral("*.blpart"),
                           QStringLiteral("*.blasm"), QStringLiteral("*.bldraw")});
    model->setNameFilterDisables(false);

    auto* view = new QTreeView(page);
    view->setModel(model);
    view->setRootIndex(model->index(documents));
    for (int c = 1; c < model->columnCount(); ++c) {
        view->hideColumn(c);
    }
    view->header()->hide();
    view->setAnimated(true);
    layout->addWidget(view, 1);

    connect(view, &QTreeView::activated, this, [this, model](const QModelIndex& index) {
        if (!model->isDir(index)) {
            emit openFileRequested(model->filePath(index));
        }
    });
    return page;
}

QWidget* ResourcePane::makePlaceholder(const QString& title, const QString& text) {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);
    auto* t = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), page);
    t->setObjectName(QStringLiteral("PaneTitle"));
    auto* body = new QLabel(text, page);
    body->setWordWrap(true);
    body->setObjectName(QStringLiteral("PaneHint"));
    layout->addWidget(t);
    layout->addWidget(body);
    layout->addStretch(1);
    return page;
}

} // namespace bl::ui
