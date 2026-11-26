#pragma once

#include "doc/PartModel.h"
#include "ui/InteractiveCommand.h"

#include <TopoDS_Edge.hxx>

#include <QUuid>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;

namespace bl::ui {

class LengthField;
class SelectionBox;

// Fillet (constant radius) and Chamfer (distance or two distances) on model edges (PLAN.md 6.7).
// Clicking edges in the graphics area adds or removes them; edges tangent to a picked one are
// included. The new faces are previewed in yellow; OK adds the feature (or changes the edited one)
// as one undo step. While editing, the model is shown as it was before the feature so its edges can
// be picked again.
class EdgeFeatureCommand : public InteractiveCommand {
    Q_OBJECT
public:
    // `type`: Fillet or Chamfer. `editing`: an existing feature of that type.
    EdgeFeatureCommand(CommandHost& host, doc::FeatureType type, const QUuid& editing = {});

    QString id() const override { return isFillet() ? QStringLiteral("features.fillet") : QStringLiteral("features.chamfer"); }
    QString title() const override { return isFillet() ? tr("Fillet") : tr("Chamfer"); }
    QString iconName() const override { return isFillet() ? QStringLiteral("fillet") : QStringLiteral("chamfer"); }

    void start() override;
    void selectionChanged() override;
    void unitsChanged() override;
    bool accept() override;
    void cancel() override;

    // For the smoke test / scripting: pick an edge of a body as if clicked, and set values.
    bool pickEdge(const QUuid& body, const TopoDS_Edge& edge);
    void setSize(double mm); // fillet radius or chamfer distance
    void setTwoDistances(double first, double second);
    int edgeCount() const { return static_cast<int>(m_edges.size()); }

private:
    bool isFillet() const { return m_type == doc::FeatureType::Fillet; }
    void buildPanel();
    void preview();
    void finish();
    // The model with this feature applied (new or edited) and rebuilt.
    doc::PartModel applied(QUuid* featureId) const;

    doc::FeatureType m_type;
    QUuid m_editing;
    doc::FilletParams m_fillet;
    doc::ChamferParams m_chamfer;
    std::vector<kernel::EdgeRef> m_edges;
    std::shared_ptr<const doc::BuildState> m_before; // the bodies the feature works on
    int m_index = 0;                                 // where the feature sits in the history
    SelectionBox* m_box = nullptr;
    LengthField* m_size = nullptr;
    LengthField* m_size2 = nullptr;
    QComboBox* m_chamferType = nullptr;
    QCheckBox* m_flip = nullptr;
    bool m_building = false;
    bool m_selecting = false;
};

} // namespace bl::ui
