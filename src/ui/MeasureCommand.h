#pragma once

#include "ui/InteractiveCommand.h"

class QLabel;

namespace bl::ui {

class SelectionBox;

// Evaluate > Measure (PLAN.md 6.9): pick one or more entities and read sizes, distances and angles.
// A tool window: it has a Close button only and changes nothing in the document.
class MeasureCommand : public InteractiveCommand {
    Q_OBJECT
public:
    explicit MeasureCommand(CommandHost& host) : InteractiveCommand(host) {}

    QString id() const override { return QStringLiteral("evaluate.measure"); }
    QString title() const override { return tr("Measure"); }
    QString iconName() const override { return QStringLiteral("measure"); }
    bool confirms() const override { return false; }

    void start() override;
    void selectionChanged() override;
    void unitsChanged() override { selectionChanged(); }
    void cancel() override;

private:
    SelectionBox* m_box = nullptr;
    QLabel* m_result = nullptr;
};

} // namespace bl::ui
