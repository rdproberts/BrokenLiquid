#include "ui/HomePage.h"

#include "bl/Version.h"
#include "core/Settings.h"
#include "core/Units.h"
#include "ui/CommandRegistry.h"
#include "ui/Icons.h"

#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace bl::ui {
namespace {

QFrame* card(const QString& title, QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("HomeCard"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 14, 18, 16);
    layout->setSpacing(8);
    auto* heading = new QLabel(title, frame);
    heading->setObjectName(QStringLiteral("HomeCardTitle"));
    layout->addWidget(heading);
    return frame;
}

} // namespace

HomePage::HomePage(CommandRegistry& registry, QWidget* parent) : QWidget(parent), m_registry(registry) {
    setObjectName(QStringLiteral("HomePage"));

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    auto* page = new QWidget(scroll);
    page->setObjectName(QStringLiteral("HomeContent"));
    scroll->setWidget(page);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(48, 36, 48, 36);
    column->setSpacing(18);

    auto* title = new QLabel(QStringLiteral("BrokenLiquid"), page);
    title->setObjectName(QStringLiteral("HomeTitle"));
    auto* subtitle = new QLabel(tr("Free, local, parametric 3D CAD · version %1").arg(QString::fromLatin1(kVersion)), page);
    subtitle->setObjectName(QStringLiteral("HomeSubtitle"));
    column->addWidget(title);
    column->addWidget(subtitle);

    m_firstRun = makeFirstRun();
    column->addWidget(m_firstRun);
    column->addWidget(makeActions());

    auto* row = new QHBoxLayout();
    row->setSpacing(18);
    QFrame* recent = card(tr("Recent"), page);
    auto* recentBody = new QWidget(recent);
    m_recentList = new QVBoxLayout(recentBody);
    m_recentList->setContentsMargins(0, 0, 0, 0);
    m_recentList->setSpacing(2);
    recent->layout()->addWidget(recentBody);
    static_cast<QVBoxLayout*>(recent->layout())->addStretch(1);
    row->addWidget(recent, 3);
    row->addWidget(makeTips(), 2);
    column->addLayout(row);
    column->addStretch(1);

    refresh();
}

QWidget* HomePage::makeActions() {
    auto* box = new QWidget(this);
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    for (const char* id : {"file.new.part", "file.new.assembly", "file.new.drawing", "file.open"}) {
        QAction* action = m_registry.action(QString::fromLatin1(id));
        if (!action) {
            continue;
        }
        auto* button = new QToolButton(box);
        button->setObjectName(QStringLiteral("HomeAction"));
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(40, 40));
        button->setMinimumSize(132, 96);
        layout->addWidget(button);
    }
    layout->addStretch(1);
    return box;
}

QWidget* HomePage::makeTips() {
    QFrame* tips = card(tr("Getting around"), this);
    auto* text = new QLabel(tips);
    text->setObjectName(QStringLiteral("HomeTips"));
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);
    text->setText(tr("<table cellspacing='3'>"
                     "<tr><td><b>Middle drag</b></td><td>rotate</td></tr>"
                     "<tr><td><b>Ctrl + middle drag</b></td><td>pan</td></tr>"
                     "<tr><td><b>Wheel</b></td><td>zoom at the pointer</td></tr>"
                     "<tr><td><b>F</b></td><td>zoom to fit</td></tr>"
                     "<tr><td><b>Ctrl+1…7</b></td><td>standard views</td></tr>"
                     "<tr><td><b>Ctrl+K</b></td><td>search commands</td></tr>"
                     "<tr><td><b>Enter / Esc</b></td><td>OK / Cancel a command</td></tr>"
                     "</table>"));
    tips->layout()->addWidget(text);
    static_cast<QVBoxLayout*>(tips->layout())->addStretch(1);
    return tips;
}

QWidget* HomePage::makeFirstRun() {
    QFrame* frame = card(tr("Welcome! Set up a few things first"), this);
    frame->setObjectName(QStringLiteral("HomeCardAccent"));
    auto* form = new QFormLayout();
    auto* units = new QComboBox(frame);
    for (UnitSystem s : {UnitSystem::MMGS, UnitSystem::CGS, UnitSystem::MKS, UnitSystem::IPS}) {
        units->addItem(QStringLiteral("%1 (%2)").arg(QString::fromLatin1(units::systemName(s)),
                                                     QString::fromLatin1(units::systemDescription(s))),
                       QString::fromLatin1(units::systemName(s)));
    }
    auto* standard = new QComboBox(frame);
    standard->addItems({QStringLiteral("ISO"), QStringLiteral("ANSI")});
    standard->setToolTip(tr("Drafting standard for drawings (used from Phase 5)"));
    auto* theme = new QComboBox(frame);
    theme->addItem(tr("Light"), QStringLiteral("light"));
    theme->addItem(tr("Dark"), QStringLiteral("dark"));
    form->addRow(tr("Units for new parts"), units);
    form->addRow(tr("Drafting standard"), standard);
    form->addRow(tr("Theme"), theme);
    static_cast<QVBoxLayout*>(frame->layout())->addLayout(form);

    auto* done = new QPushButton(tr("Get started"), frame);
    done->setObjectName(QStringLiteral("HomePrimary"));
    done->setDefault(true);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(done);
    static_cast<QVBoxLayout*>(frame->layout())->addLayout(buttons);

    connect(done, &QPushButton::clicked, this, [this, units, standard, theme] {
        Settings& s = Settings::global();
        s.set("units.default", units->currentData().toString().toStdString());
        s.set("drawing.standard", standard->currentText().toStdString());
        s.set("ui.theme", theme->currentData().toString().toStdString());
        s.set("app.firstRunDone", true);
        s.save();
        m_firstRun->hide();
        emit settingsChanged();
    });
    return frame;
}

void HomePage::refresh() {
    m_firstRun->setVisible(!Settings::global().get<bool>("app.firstRunDone", false));

    while (QLayoutItem* item = m_recentList->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const auto recent = Settings::global().get<std::vector<std::string>>("files.recent", {});
    int shown = 0;
    for (const std::string& entry : recent) {
        const QString path = QString::fromStdString(entry);
        const QFileInfo info(path);
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("HomeRecent"));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIcon(icon(QStringLiteral("tree-part"), info.fileName()));
        button->setIconSize(QSize(20, 20));
        button->setText(QStringLiteral("%1   —   %2").arg(info.fileName(), info.absolutePath()));
        button->setToolTip(path);
        button->setAutoRaise(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setEnabled(info.exists());
        connect(button, &QToolButton::clicked, this, [this, path] { emit openRecentRequested(path); });
        m_recentList->addWidget(button);
        if (++shown == 10) {
            break;
        }
    }
    if (shown == 0) {
        auto* none = new QLabel(tr("Files you open appear here. Drop a STEP file anywhere on the window to open it."), this);
        none->setWordWrap(true);
        none->setObjectName(QStringLiteral("PaneHint"));
        m_recentList->addWidget(none);
    }
}

} // namespace bl::ui
