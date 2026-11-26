#include "ui/OptionsDialog.h"

#include "core/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

#include <string>
#include <vector>

namespace bl::ui {
namespace {

constexpr UnitSystem kSystems[] = {UnitSystem::Millimetre, UnitSystem::Centimetre, UnitSystem::Metre, UnitSystem::Inch};

QComboBox* unitCombo(QWidget* parent) {
    auto* combo = new QComboBox(parent);
    for (UnitSystem s : kSystems) {
        combo->addItem(QStringLiteral("%1 (%2)").arg(QString::fromLatin1(units::systemName(s)),
                                                     QString::fromLatin1(units::systemDescription(s))),
                       static_cast<int>(s));
    }
    return combo;
}

void selectUnit(QComboBox* combo, UnitSystem s) { combo->setCurrentIndex(combo->findData(static_cast<int>(s))); }

// A list of page names on the left, the pages on the right.
struct PagedWidget {
    QWidget* widget;
    QListWidget* list;
    QStackedWidget* stack;

    explicit PagedWidget(QWidget* parent)
        : widget(new QWidget(parent)), list(new QListWidget(widget)), stack(new QStackedWidget(widget)) {
        auto* layout = new QHBoxLayout(widget);
        list->setFixedWidth(170);
        list->setObjectName(QStringLiteral("OptionsPages"));
        layout->addWidget(list);
        layout->addWidget(stack, 1);
        QObject::connect(list, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
    }

    QFormLayout* addPage(const QString& name, const QString& intro = {}) {
        auto* page = new QWidget(stack);
        auto* column = new QVBoxLayout(page);
        auto* title = new QLabel(QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped()), page);
        column->addWidget(title);
        if (!intro.isEmpty()) {
            auto* text = new QLabel(intro, page);
            text->setWordWrap(true);
            text->setObjectName(QStringLiteral("PaneHint"));
            column->addWidget(text);
        }
        auto* form = new QFormLayout();
        column->addLayout(form);
        column->addStretch(1);
        stack->addWidget(page);
        list->addItem(name);
        return form;
    }
};

} // namespace

OptionsDialog::OptionsDialog(const Units* document, const QString& documentTitle, QWidget* parent)
    : QDialog(parent), m_tabs(new QTabWidget(this)) {
    setWindowTitle(tr("Options"));
    setObjectName(QStringLiteral("OptionsDialog"));
    resize(720, 460);

    m_tabs->addTab(makeAppSettings(), tr("Application"));
    if (document) {
        m_initialUnits = *document;
        m_tabs->addTab(makeDocumentSettings(*document), tr("Document: %1").arg(documentTitle));
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* reset = buttons->addButton(tr("Reset…"), QDialogButtonBox::ResetRole);
    reset->setToolTip(tr("Put every application setting back to its default"));
    connect(buttons, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);
    connect(reset, &QPushButton::clicked, this, &OptionsDialog::resetAll);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(buttons);
    load();
}

QWidget* OptionsDialog::makeAppSettings() {
    PagedWidget paged(m_tabs);

    QFormLayout* general = paged.addPage(tr("General"));
    m_theme = new QComboBox(paged.widget);
    m_theme->addItem(tr("Light"), QStringLiteral("light"));
    m_theme->addItem(tr("Dark"), QStringLiteral("dark"));
    general->addRow(tr("Theme"), m_theme);
    m_splash = new QCheckBox(tr("Show the splash screen at start-up"), paged.widget);
    general->addRow(QString(), m_splash);

    QFormLayout* view = paged.addPage(tr("View"));
    m_reverseZoom = new QCheckBox(tr("Reverse mouse wheel zoom direction"), paged.widget);
    view->addRow(QString(), m_reverseZoom);

    QFormLayout* units = paged.addPage(tr("Default Units"), tr("Used for new documents. Each document keeps its own "
                                                                "units on the Document tab."));
    m_defaultUnits = unitCombo(paged.widget);
    m_defaultDecimals = new QSpinBox(paged.widget);
    m_defaultDecimals->setRange(0, 8);
    units->addRow(tr("Unit system"), m_defaultUnits);
    units->addRow(tr("Decimal places"), m_defaultDecimals);

    QFormLayout* files = paged.addPage(tr("Files"));
    m_recentMax = new QSpinBox(paged.widget);
    m_recentMax->setRange(0, 30);
    files->addRow(tr("Recent files to remember"), m_recentMax);
    auto* clear = new QPushButton(tr("Clear recent files"), paged.widget);
    connect(clear, &QPushButton::clicked, this, [clear] {
        Settings::global().set("files.recent", std::vector<std::string>{});
        clear->setEnabled(false);
    });
    files->addRow(QString(), clear);

    paged.list->setCurrentRow(0);
    return paged.widget;
}

QWidget* OptionsDialog::makeDocumentSettings(const Units& u) {
    PagedWidget paged(m_tabs);
    QFormLayout* page = paged.addPage(tr("Units"), tr("Changing units only changes how values are shown and "
                                                      "typed; the model keeps its size. Undo with Ctrl+Z."));
    m_docUnits = unitCombo(paged.widget);
    selectUnit(m_docUnits, u.system);
    m_docDecimals = new QSpinBox(paged.widget);
    m_docDecimals->setRange(0, 8);
    m_docDecimals->setValue(u.decimals);
    m_docAngleDecimals = new QSpinBox(paged.widget);
    m_docAngleDecimals->setRange(0, 6);
    m_docAngleDecimals->setValue(u.angleDecimals);
    page->addRow(tr("Unit system"), m_docUnits);
    page->addRow(tr("Length decimals"), m_docDecimals);
    page->addRow(tr("Angle decimals"), m_docAngleDecimals);
    paged.list->setCurrentRow(0);
    return paged.widget;
}

void OptionsDialog::showDocumentSettings() {
    if (m_tabs->count() > 1) {
        m_tabs->setCurrentIndex(1);
    }
}

void OptionsDialog::load() {
    const Settings& s = Settings::global();
    m_theme->setCurrentIndex(m_theme->findData(QString::fromStdString(s.get<std::string>("ui.theme", "light"))));
    m_splash->setChecked(s.get<bool>("app.showSplash", true));
    m_reverseZoom->setChecked(s.get<bool>("view.mouse.reverseZoom", false));
    const auto system = units::systemFromName(s.get<std::string>("units.default", "mm"));
    selectUnit(m_defaultUnits, system.value_or(UnitSystem::Millimetre));
    m_defaultDecimals->setValue(s.get<int>("units.decimals", 2));
    m_recentMax->setValue(s.get<int>("files.recentMax", 10));
}

void OptionsDialog::resetAll() {
    if (QMessageBox::question(this, tr("Reset options"),
                              tr("Put all application settings back to their defaults? Recent files and window "
                                 "layout are cleared too.")) != QMessageBox::Yes) {
        return;
    }
    Settings::global().reset();
    Settings::global().set("app.firstRunDone", true);
    load();
}

void OptionsDialog::accept() {
    Settings& s = Settings::global();
    s.set("ui.theme", m_theme->currentData().toString().toStdString());
    s.set("app.showSplash", m_splash->isChecked());
    s.set("view.mouse.reverseZoom", m_reverseZoom->isChecked());
    s.set("units.default",
          std::string(units::systemName(static_cast<UnitSystem>(m_defaultUnits->currentData().toInt()))));
    s.set("units.decimals", m_defaultDecimals->value());
    s.set("files.recentMax", m_recentMax->value());
    s.save();

    if (m_initialUnits && m_docUnits) {
        Units u = *m_initialUnits;
        u.system = static_cast<UnitSystem>(m_docUnits->currentData().toInt());
        u.decimals = m_docDecimals->value();
        u.angleDecimals = m_docAngleDecimals->value();
        if (!(u == *m_initialUnits)) {
            m_changedUnits = u;
        }
    }
    QDialog::accept();
}

} // namespace bl::ui
