#pragma once

#include "core/Units.h"

#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QToolButton;
class QVBoxLayout;

namespace bl::ui {

// Numeric length input (PLAN.md 7.3): shows the value in document units, accepts units and simple
// arithmetic ("20mm", "1in + 5", "=40/2"), mouse wheel steps (Ctrl = fine). Value is kept in mm.
class LengthField : public QLineEdit {
    Q_OBJECT
public:
    explicit LengthField(QWidget* parent = nullptr);

    double value() const { return m_mm; }
    void setValue(double mm);
    void setUnits(const Units& units);
    void setRange(double minMm, double maxMm);
    void setStep(double mm) { m_step = mm; }

signals:
    void valueChanged(double mm);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void commit();
    void refresh();

    double m_mm = 0.0;
    double m_min = -1e9, m_max = 1e9;
    double m_step = 1.0; // mm per wheel notch; scaled to document units
    Units m_units;
};

// A pick list (PLAN.md 7.3 "selection boxes"): one active at a time, shown with an accent border.
// The owning command fills it from the graphics area; right-click offers Delete / Clear.
class SelectionBox : public QListWidget {
    Q_OBJECT
public:
    explicit SelectionBox(const QString& placeholder, QWidget* parent = nullptr);

    void setEntries(const QStringList& entries);
    void setActive(bool active);
    bool isActive() const { return m_active; }

signals:
    void activated();
    void removeRequested(int row);
    void clearRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_placeholder;
    bool m_active = false;
};

// Collapsible group box with an optional enable checkbox in its header.
class PanelGroup : public QFrame {
    Q_OBJECT
public:
    PanelGroup(const QString& title, bool checkable, QWidget* parent = nullptr);

    QFormLayout* form() const { return m_form; }
    bool isChecked() const;
    void setChecked(bool on);
    void setExpanded(bool expanded);

    // Builders: each adds one row and returns the created widget.
    SelectionBox* addSelectionBox(const QString& placeholder, int rows = 3);
    LengthField* addLength(const QString& label, double mm, const Units& units);
    QCheckBox* addCheck(const QString& text, bool checked);
    QComboBox* addChoice(const QString& label, const QStringList& items, int current = 0);
    QLabel* addText(const QString& text = {});
    void addRow(const QString& label, QWidget* widget);

signals:
    void toggled(bool on);

private:
    QToolButton* m_header;
    QCheckBox* m_check = nullptr;
    QWidget* m_body;
    QFormLayout* m_form;
};

// The Property Panel (PLAN.md 7.3): header with OK/Cancel, a message box and groups.
// Commands build their UI into it; the main window shows it in place of the Feature Tree.
class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget* parent = nullptr);

    // Starts a fresh panel for a command. `confirm`: show OK + Cancel; otherwise only Close.
    void begin(const QString& title, const QString& iconName, bool confirm);
    void clear();
    // Removes the groups and message but keeps the header (for commands that rebuild their panel).
    void clearContent();
    bool isOpen() const { return m_open; }

    void setMessage(const QString& text);
    PanelGroup* addGroup(const QString& title, bool checkable = false);

signals:
    void okClicked();
    void cancelClicked();

private:
    QLabel* m_icon;
    QLabel* m_title;
    QToolButton* m_ok;
    QToolButton* m_cancel;
    QLabel* m_message;
    QWidget* m_content;
    QVBoxLayout* m_contentLayout;
    QLabel* m_idle;
    bool m_open = false;
};

} // namespace bl::ui
