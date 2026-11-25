#pragma once

#include "core/Units.h"

#include <QDialog>

#include <optional>

class QCheckBox;
class QComboBox;
class QListWidget;
class QSpinBox;
class QStackedWidget;
class QTabWidget;

namespace bl::ui {

// Tools > Options (PLAN.md 7.7): "System Options" are per user (Settings), "Document Properties"
// belong to the active document and change through an undoable transaction.
class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    // `document` is the active document's units, or null when no document is open.
    OptionsDialog(const Units* document, const QString& documentTitle, QWidget* parent = nullptr);

    void showDocumentProperties();

    // After exec(): the document units if the user changed them.
    std::optional<Units> changedDocumentUnits() const { return m_changedUnits; }

    void accept() override;

private:
    QWidget* makeSystemOptions();
    QWidget* makeDocumentProperties(const Units& units);
    void load();
    void resetAll();

    QTabWidget* m_tabs;
    // System options
    QComboBox* m_theme = nullptr;
    QCheckBox* m_splash = nullptr;
    QCheckBox* m_reverseZoom = nullptr;
    QComboBox* m_defaultUnits = nullptr;
    QSpinBox* m_defaultDecimals = nullptr;
    QSpinBox* m_recentMax = nullptr;
    // Document properties
    QComboBox* m_docUnits = nullptr;
    QSpinBox* m_docDecimals = nullptr;
    QSpinBox* m_docAngleDecimals = nullptr;
    std::optional<Units> m_initialUnits;
    std::optional<Units> m_changedUnits;
};

} // namespace bl::ui
