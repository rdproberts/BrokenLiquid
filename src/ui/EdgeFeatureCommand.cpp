#include "ui/EdgeFeatureCommand.h"

#include "doc/Document.h"
#include "doc/Rebuild.h"
#include "kernel/Features.h"
#include "ui/DocumentView.h"
#include "ui/PropertyPanel.h"

#include <AIS_Shape.hxx>
#include <BRep_Builder.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>

#include <algorithm>

namespace bl::ui {
namespace {

Quantity_Color rgb(int r, int g, int b) { return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB); }

const doc::BuiltBody* bodyIn(const doc::BuildState& state, const QUuid& id) {
    for (const doc::BuiltBody& b : state.bodies) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

} // namespace

EdgeFeatureCommand::EdgeFeatureCommand(CommandHost& host, doc::FeatureType type, const QUuid& editing)
    : InteractiveCommand(host), m_type(type), m_editing(editing) {
    const doc::Document* d = host.activeDocument();
    if (!d) {
        return;
    }
    const doc::PartModel& m = d->model();
    m_index = m.rollback >= 0 ? m.rollback : static_cast<int>(m.features.size());
    if (const doc::Feature* f = editing.isNull() ? nullptr : m.feature(editing)) {
        m_fillet = f->fillet;
        m_chamfer = f->chamfer;
        m_index = m.featureIndex(editing);
    }
    static const auto empty = std::make_shared<const doc::BuildState>();
    m_before = m_index > 0 && m.features[static_cast<size_t>(m_index - 1)].after
                   ? m.features[static_cast<size_t>(m_index - 1)].after
                   : empty;
}

void EdgeFeatureCommand::start() {
    buildPanel();
    DocumentView* view = m_host.activeView();
    if (!view) {
        return;
    }
    render::OcctViewer* viewer = view->viewer();
    // Edges picked before the command started are used; anything else is dropped.
    std::vector<render::OcctViewer::Pick> keep;
    for (const auto& pick : viewer->selection()) {
        if (!pick.shape.IsNull() && pick.shape.ShapeType() == TopAbs_EDGE) {
            keep.push_back(pick);
        }
    }
    m_selecting = true;
    viewer->clearSelection();
    viewer->setSelectionFilter(false, true, false);
    viewer->setAdditivePicking(true);
    int lost = 0;
    if (!m_editing.isNull()) {
        // Show the model as the feature found it and pick its edges again.
        std::vector<doc::Body> bodies;
        const doc::PartModel& m = m_host.activeDocument()->model();
        for (const doc::BuiltBody& b : m_before->bodies) {
            doc::Body body;
            body.id = b.id;
            body.shape = b.shape.shape;
            body.named = b.shape;
            if (const doc::Body* shown = m.body(b.id)) {
                body.color = shown->color;
                body.visible = shown->visible;
            }
            bodies.push_back(std::move(body));
        }
        view->setBodiesOverride(std::move(bodies));
        std::vector<const kernel::NamedShape*> shapes;
        for (const doc::BuiltBody& b : m_before->bodies) {
            shapes.push_back(&b.shape);
        }
        for (const kernel::EdgeRef& ref : isFillet() ? m_fillet.edges : m_chamfer.edges) {
            const kernel::ResolvedEdge r = kernel::resolveEdge(shapes, ref);
            if (r.status == kernel::RefStatus::Missing ||
                !viewer->select(view->objectOf(m_before->bodies[static_cast<size_t>(r.shape)].id), r.edge)) {
                ++lost;
            }
        }
    } else {
        for (const auto& pick : keep) {
            viewer->select(pick.object, pick.shape);
        }
    }
    m_selecting = false;
    selectionChanged();
    if (lost > 0) {
        m_host.showStatus(tr("%n edge(s) of this feature are gone; pick them again.", nullptr, lost), 8000);
    }
}

void EdgeFeatureCommand::unitsChanged() {
    if (!m_building) {
        buildPanel();
        selectionChanged();
    }
}

void EdgeFeatureCommand::buildPanel() {
    m_building = true;
    PropertyPanel& panel = m_host.panel();
    panel.clearContent();
    const Units u = m_host.units();
    panel.setMessage(isFillet() ? tr("Click the edges to round; click again to remove one. Edges that run on "
                                     "smoothly from a picked edge are rounded too.")
                                : tr("Click the edges to bevel; click again to remove one. Edges that run on "
                                     "smoothly from a picked edge are bevelled too."));
    PanelGroup* items = panel.addGroup(isFillet() ? tr("Items to Fillet") : tr("Items to Chamfer"));
    m_box = items->addSelectionBox(tr("Edges"), 4);
    m_box->setActive(true);
    connect(m_box, &SelectionBox::clearRequested, this, [this] {
        if (DocumentView* view = m_host.activeView()) {
            view->viewer()->clearSelection();
        }
    });
    connect(m_box, &SelectionBox::removeRequested, this, [this](int row) {
        if (DocumentView* view = m_host.activeView()) {
            view->viewer()->deselect(row);
        }
    });

    PanelGroup* params = panel.addGroup(isFillet() ? tr("Fillet Parameters") : tr("Chamfer Parameters"));
    m_chamferType = nullptr;
    m_size2 = nullptr;
    m_flip = nullptr;
    if (isFillet()) {
        m_size = params->addLength(tr("Radius"), m_fillet.radius, u);
    } else {
        m_chamferType = params->addChoice(tr("Chamfer type"), {tr("Equal distance"), tr("Distance distance")},
                                          m_chamfer.type == doc::ChamferType::TwoDistances ? 1 : 0);
        m_size = params->addLength(tr("Distance 1"), m_chamfer.distance, u);
        m_size2 = params->addLength(tr("Distance 2"), m_chamfer.distance2, u);
        m_size2->setRange(1e-4, 1e5);
        m_flip = params->addCheck(tr("Flip direction"), m_chamfer.flip);
        const bool two = m_chamfer.type == doc::ChamferType::TwoDistances;
        m_size2->setEnabled(two);
        m_flip->setEnabled(two);
        connect(m_chamferType, &QComboBox::currentIndexChanged, this, [this](int index) {
            m_chamfer.type = index == 1 ? doc::ChamferType::TwoDistances : doc::ChamferType::Distance;
            m_size2->setEnabled(index == 1);
            m_flip->setEnabled(index == 1);
            preview();
        });
        connect(m_size2, &LengthField::valueChanged, this, [this](double mm) {
            m_chamfer.distance2 = mm;
            preview();
        });
        connect(m_flip, &QCheckBox::toggled, this, [this](bool on) {
            m_chamfer.flip = on;
            preview();
        });
    }
    m_size->setRange(1e-4, 1e5);
    connect(m_size, &LengthField::valueChanged, this, [this](double mm) { setSize(mm); });
    m_building = false;
}

void EdgeFeatureCommand::setSize(double mm) {
    if (mm <= 0.0) {
        return;
    }
    (isFillet() ? m_fillet.radius : m_chamfer.distance) = mm;
    if (m_size && m_size->value() != mm) {
        const QSignalBlocker block(m_size);
        m_size->setValue(mm);
    }
    preview();
}

void EdgeFeatureCommand::setTwoDistances(double first, double second) {
    m_chamfer.type = doc::ChamferType::TwoDistances;
    m_chamfer.distance2 = second;
    if (m_chamferType) {
        const QSignalBlocker block(m_chamferType);
        m_chamferType->setCurrentIndex(1);
        m_size2->setEnabled(true);
        m_flip->setEnabled(true);
    }
    if (m_size2) {
        const QSignalBlocker block(m_size2);
        m_size2->setValue(second);
    }
    setSize(first);
}

bool EdgeFeatureCommand::pickEdge(const QUuid& body, const TopoDS_Edge& edge) {
    DocumentView* view = m_host.activeView();
    return view && view->viewer()->select(view->objectOf(body), edge);
}

void EdgeFeatureCommand::selectionChanged() {
    if (m_selecting) {
        return;
    }
    DocumentView* view = m_host.activeView();
    if (!view || !m_box) {
        return;
    }
    m_edges.clear();
    QStringList entries;
    for (const auto& pick : view->viewer()->selection()) {
        if (pick.shape.IsNull() || pick.shape.ShapeType() != TopAbs_EDGE) {
            continue;
        }
        const doc::BuiltBody* body = bodyIn(*m_before, view->bodyOf(pick.object));
        if (!body) {
            continue;
        }
        m_edges.push_back(kernel::edgeReferenceTo(body->shape, TopoDS::Edge(pick.shape)));
        entries << tr("Edge<%1>").arg(entries.size() + 1);
    }
    m_box->setEntries(entries);
    preview();
}

doc::PartModel EdgeFeatureCommand::applied(QUuid* featureId) const {
    const doc::Document* d = m_host.activeDocument();
    doc::PartModel next = d->model();
    *featureId = {};
    doc::FilletParams fillet = m_fillet;
    doc::ChamferParams chamfer = m_chamfer;
    (isFillet() ? fillet.edges : chamfer.edges) = m_edges;
    if (!m_editing.isNull()) {
        if (doc::Feature* f = next.feature(m_editing)) {
            f->fillet = fillet;
            f->chamfer = chamfer;
            *featureId = f->id;
        }
    } else {
        doc::Feature f;
        f.type = m_type;
        f.name = isFillet() ? tr("Fillet%1").arg(++next.filletCount) : tr("Chamfer%1").arg(++next.chamferCount);
        f.fillet = fillet;
        f.chamfer = chamfer;
        *featureId = f.id;
        if (next.rollback >= 0 && next.rollback <= static_cast<int>(next.features.size())) {
            next.features.insert(next.features.begin() + next.rollback, f);
            ++next.rollback;
        } else {
            next.features.push_back(f);
        }
    }
    doc::rebuild(next, &d->model());
    return next;
}

void EdgeFeatureCommand::preview() {
    DocumentView* view = m_host.activeView();
    const doc::Document* d = m_host.activeDocument();
    if (!view || !d || m_building) {
        return;
    }
    view->viewer()->clearTemporary();
    if (m_edges.empty()) {
        return;
    }
    QUuid id;
    const doc::PartModel next = applied(&id);
    const doc::Feature* f = next.feature(id);
    if (!f) {
        return;
    }
    if (f->state == doc::FeatureState::Error) {
        m_host.showStatus(f->message, 6000);
        return;
    }
    if (!f->message.isEmpty()) {
        m_host.showStatus(f->message, 6000);
    }
    // The faces the feature made, on top of the model in yellow.
    const std::string prefix = doc::namingKey(id) + "/";
    BRep_Builder builder;
    TopoDS_Compound faces;
    builder.MakeCompound(faces);
    for (const doc::BuiltBody& b : f->after->bodies) {
        builder.Add(faces, kernel::facesNamed(b.shape, prefix));
    }
    Handle(AIS_Shape) shape = new AIS_Shape(faces);
    shape->SetColor(rgb(245, 200, 60));
    shape->SetDisplayMode(AIS_Shaded);
    shape->Attributes()->SetFaceBoundaryDraw(true);
    shape->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(rgb(150, 110, 20), Aspect_TOL_SOLID, 1.0));
    view->viewer()->showTemporary(shape);
}

void EdgeFeatureCommand::finish() {
    if (DocumentView* view = m_host.activeView()) {
        view->viewer()->clearTemporary();
        view->viewer()->setAdditivePicking(false);
        m_selecting = true; // the selection belongs to the shown bodies, which may go away
        view->viewer()->clearSelection();
        view->setBodiesOverride(std::nullopt);
    }
}

bool EdgeFeatureCommand::accept() {
    doc::Document* d = m_host.activeDocument();
    if (!d) {
        return true;
    }
    if (m_edges.empty()) {
        m_host.showStatus(isFillet() ? tr("Pick the edges to round.") : tr("Pick the edges to bevel."), 6000);
        return false;
    }
    QUuid id;
    doc::PartModel next = applied(&id);
    const doc::Feature* f = next.feature(id);
    if (!f) {
        finish();
        return true;
    }
    if (f->state == doc::FeatureState::Error) {
        m_host.showStatus(f->message, 8000);
        return false;
    }
    finish();
    const QString name = f->name;
    d->transact(m_editing.isNull() ? tr("New %1").arg(name) : tr("Edit %1").arg(name), [&next](doc::PartModel& m) {
        m = next;
        return true;
    });
    m_host.showStatus(m_editing.isNull() ? tr("%1 added.").arg(name) : tr("%1 changed.").arg(name));
    return true;
}

void EdgeFeatureCommand::cancel() { finish(); }

} // namespace bl::ui
