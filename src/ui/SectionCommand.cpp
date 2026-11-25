#include "ui/SectionCommand.h"

#include "ui/PropertyPanel.h"

#include <BRepAdaptor_Surface.hxx>
#include <Bnd_Box.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>

namespace bl::ui {
namespace {

// Planar face -> plane with its outward normal (respecting the face orientation).
std::optional<gp_Pln> planeOfFace(const TopoDS_Shape& shape) {
    if (shape.IsNull() || shape.ShapeType() != TopAbs_FACE) {
        return std::nullopt;
    }
    const TopoDS_Face face = TopoDS::Face(shape);
    BRepAdaptor_Surface surface(face);
    if (surface.GetType() != GeomAbs_Plane) {
        return std::nullopt;
    }
    gp_Pln plane = surface.Plane();
    if (face.Orientation() == TopAbs_REVERSED) {
        gp_Ax3 pos = plane.Position();
        pos.ZReverse();
        plane.SetPosition(pos);
    }
    // Anchor the plane at the face's middle so offsets are measured from the face itself.
    const double u = (surface.FirstUParameter() + surface.LastUParameter()) / 2.0;
    const double v = (surface.FirstVParameter() + surface.LastVParameter()) / 2.0;
    plane.SetLocation(surface.Value(u, v));
    return plane;
}

} // namespace

void SectionCommand::start() {
    DocumentView* view = m_host.activeView();
    const Units u = m_host.units();
    if (view && view->section()) {
        m_spec = *view->section(); // edit the current section
    }

    PropertyPanel& panel = m_host.panel();
    panel.setMessage(tr("Choose a plane or pick a planar face, then drag the offset. The cut face is "
                        "filled (capped) so you can see inside solids."));

    PanelGroup* group = panel.addGroup(tr("Section 1"));
    m_reference = group->addChoice(tr("Plane"), {tr("Front Plane"), tr("Top Plane"), tr("Right Plane"), tr("Selected face")},
                                   static_cast<int>(m_spec.reference));
    m_faceBox = group->addSelectionBox(tr("Pick a planar face"), 1);
    m_offset = group->addLength(tr("Offset"), m_spec.offset, u);
    m_flip = group->addCheck(tr("Flip side"), m_spec.flip);
    m_cap = group->addCheck(tr("Fill cut faces"), m_spec.cap);
    m_hasFace = m_spec.reference == SectionSpec::Face;
    m_faceBox->setEntries(m_hasFace ? QStringList{tr("Face<1>")} : QStringList{});
    m_faceBox->setActive(true);

    Bnd_Box box;
    if (view && view->bounds(box)) {
        const double size = std::sqrt(box.SquareExtent());
        m_offset->setStep(std::max(0.1, size / 100.0));
    }

    connect(m_reference, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == SectionSpec::Face && !m_hasFace) {
            m_host.showStatus(tr("Pick a planar face in the graphics area."));
            return; // keep the old plane until a face is picked
        }
        m_spec.reference = static_cast<SectionSpec::Reference>(index);
        resetOffset();
        preview();
    });
    connect(m_offset, &LengthField::valueChanged, this, [this](double mm) {
        m_spec.offset = mm;
        preview();
    });
    connect(m_flip, &QCheckBox::toggled, this, [this](bool on) {
        m_spec.flip = on;
        preview();
    });
    connect(m_cap, &QCheckBox::toggled, this, [this](bool on) {
        m_spec.cap = on;
        preview();
    });
    connect(m_faceBox, &SelectionBox::clearRequested, this, [this] {
        if (DocumentView* v = m_host.activeView()) {
            v->viewer()->clearSelection();
        }
    });

    if (view) {
        view->viewer()->setSelectionFilter(true, false, false);
        view->viewer()->clearSelection();
    }
    if (!view || !view->section()) {
        resetOffset();
    }
    preview();
}

void SectionCommand::selectionChanged() {
    DocumentView* view = m_host.activeView();
    if (!view) {
        return;
    }
    const auto picks = view->viewer()->selection();
    if (picks.empty()) {
        return;
    }
    const std::optional<gp_Pln> plane = planeOfFace(picks.back().shape);
    if (!plane) {
        m_host.showStatus(tr("Section View needs a planar face."));
        return;
    }
    m_spec.reference = SectionSpec::Face;
    m_spec.face = *plane;
    m_spec.offset = 0.0;
    m_hasFace = true;
    m_faceBox->setEntries({tr("Face<1>")});
    {
        const QSignalBlocker block(m_reference);
        m_reference->setCurrentIndex(SectionSpec::Face);
    }
    {
        const QSignalBlocker block(m_offset);
        m_offset->setValue(0.0);
    }
    preview();
}

void SectionCommand::unitsChanged() {
    if (m_offset) {
        m_offset->setUnits(m_host.units());
    }
}

void SectionCommand::resetOffset() {
    DocumentView* view = m_host.activeView();
    double offset = 0.0;
    Bnd_Box box;
    if (view && view->bounds(box) && m_spec.reference != SectionSpec::Face) {
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        const gp_Pnt centre((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0);
        const gp_Pln base = m_spec.basePlane();
        offset = gp_Vec(base.Location(), centre).Dot(gp_Vec(base.Axis().Direction()));
    }
    m_spec.offset = offset;
    const QSignalBlocker block(m_offset);
    m_offset->setValue(offset);
}

void SectionCommand::preview() {
    if (DocumentView* view = m_host.activeView()) {
        view->previewSection(m_spec);
    }
}

bool SectionCommand::accept() {
    if (DocumentView* view = m_host.activeView()) {
        view->setSection(m_spec);
        view->viewer()->clearSelection();
    }
    return true;
}

void SectionCommand::cancel() {
    if (DocumentView* view = m_host.activeView()) {
        view->restoreSection();
        view->viewer()->clearSelection();
    }
}

} // namespace bl::ui
