#pragma once

#include "ui/DocumentView.h"
#include "ui/InteractiveCommand.h"

#include <optional>

class QCheckBox;
class QComboBox;

namespace bl::ui {

class LengthField;
class SelectionBox;

// View > Section View (PLAN.md 6.10): cut the display with a plane, live preview while editing.
// OK keeps the section on; Cancel goes back to what was shown before. Display only: the model
// does not change and nothing goes on the undo stack.
class SectionCommand : public InteractiveCommand {
    Q_OBJECT
public:
    explicit SectionCommand(CommandHost& host) : InteractiveCommand(host) {}

    QString id() const override { return QStringLiteral("view.section"); }
    QString title() const override { return tr("Section View"); }
    QString iconName() const override { return QStringLiteral("view-section"); }

    void start() override;
    void selectionChanged() override;
    void unitsChanged() override;
    bool accept() override;
    void cancel() override;

private:
    void preview();
    void resetOffset(); // centre of the visible model along the plane normal

    SectionSpec m_spec;
    QComboBox* m_reference = nullptr;
    SelectionBox* m_faceBox = nullptr;
    LengthField* m_offset = nullptr;
    QCheckBox* m_flip = nullptr;
    QCheckBox* m_cap = nullptr;
    bool m_hasFace = false;
};

} // namespace bl::ui
