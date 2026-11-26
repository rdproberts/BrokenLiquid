#pragma once

#include "doc/PartModel.h"
#include "ui/InteractiveCommand.h"

#include <AIS_Shape.hxx>

#include <QUuid>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

namespace bl::ui {

// Revolved Boss/Base and Revolved Cut (PLAN.md 6.7): turns the closed regions of a sketch about a
// line of that sketch (its centerline by default). One direction or mid plane, angle up to 360
// degrees, reverse, and for bosses Merge result. Previewed like the extrude; OK refuses while the
// feature would fail.
class RevolveCommand : public InteractiveCommand {
    Q_OBJECT
public:
    RevolveCommand(CommandHost& host, const QUuid& sketch, bool cut, const QUuid& editing = {});

    QString id() const override { return m_params.cut ? QStringLiteral("features.revolveCut") : QStringLiteral("features.revolve"); }
    QString title() const override { return m_params.cut ? tr("Cut-Revolve") : tr("Revolve"); }
    QString iconName() const override { return m_params.cut ? QStringLiteral("revolve-cut") : QStringLiteral("revolve"); }

    void start() override;
    void unitsChanged() override;
    bool accept() override;
    void cancel() override;

    // For the smoke test / scripting.
    void setAngle(double degrees);
    void setMidPlane(bool on);
    void setReverse(bool on);
    const doc::RevolveParams& params() const { return m_params; }

private:
    void buildPanel();
    void preview();
    void clearPreview();
    doc::PartModel applied(QUuid* featureId) const;

    doc::RevolveParams m_params;
    QUuid m_editing;
    std::vector<sketch::Id> m_axes; // the lines offered as axis, in the order of the choice box
    Handle(AIS_Shape) m_preview;
    QComboBox* m_axis = nullptr;
    QComboBox* m_type = nullptr;
    QCheckBox* m_reverse = nullptr;
    QDoubleSpinBox* m_angle = nullptr;
    bool m_building = false;
};

} // namespace bl::ui
