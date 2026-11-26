#pragma once

#include "doc/PartModel.h"
#include "ui/InteractiveCommand.h"

#include <AIS_Shape.hxx>

#include <QUuid>

class QCheckBox;
class QComboBox;

namespace bl::ui {

class LengthField;
class PanelGroup;

// Extrude and Extrude Cut (PLAN.md 6.7): sweeps the closed regions of a sketch along
// its normal. Direction 1 (Blind, Through All, Mid Plane, reverse), an optional blind Direction 2
// and, for bosses, Merge result. The swept solid is previewed while the values change; OK adds the
// feature (or changes the edited one) as one undo step, and refuses while the feature would fail.
class ExtrudeCommand : public InteractiveCommand {
    Q_OBJECT
public:
    // A new feature on `sketch`, or `editing` an existing extrude feature (then `sketch`/`cut` come
    // from it).
    ExtrudeCommand(CommandHost& host, const QUuid& sketch, bool cut, const QUuid& editing = {});

    QString id() const override { return m_params.cut ? QStringLiteral("features.extrudeCut") : QStringLiteral("features.extrude"); }
    QString title() const override { return m_params.cut ? tr("Extrude Cut") : tr("Extrude"); }
    QString iconName() const override { return m_params.cut ? QStringLiteral("extrude-cut") : QStringLiteral("extrude"); }

    void start() override;
    void unitsChanged() override;
    bool accept() override;
    void cancel() override;

    // For the smoke test / scripting: set the values as if typed in the panel.
    void setDepth(double mm);
    void setEnd(doc::EndCondition end);
    void setReverse(bool on);
    const doc::ExtrudeParams& params() const { return m_params; }

private:
    void buildPanel();
    void preview();
    void clearPreview();
    // The model with this feature applied (new or edited) and rebuilt; null feature id on failure.
    doc::PartModel applied(QUuid* featureId) const;

    doc::ExtrudeParams m_params;
    QUuid m_editing;
    Handle(AIS_Shape) m_preview;
    LengthField* m_depth = nullptr;
    LengthField* m_depth2 = nullptr;
    QComboBox* m_end = nullptr;
    QCheckBox* m_reverse = nullptr;
    PanelGroup* m_direction2 = nullptr;
    bool m_building = false;
};

} // namespace bl::ui
