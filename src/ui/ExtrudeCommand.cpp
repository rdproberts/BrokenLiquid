#include "ui/ExtrudeCommand.h"

#include "doc/Document.h"
#include "doc/Rebuild.h"
#include "ui/DocumentView.h"
#include "ui/PropertyPanel.h"

#include <AIS_InteractiveContext.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>

#include <algorithm>

namespace bl::ui {
namespace {

Quantity_Color rgb(int r, int g, int b) { return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB); }

// The model state a new feature at the end of the history (or above the rollback bar) builds on.
const doc::BuildState* stateFor(const doc::PartModel& m, int index) {
    static const doc::BuildState empty;
    if (index <= 0 || m.features.empty()) {
        return &empty;
    }
    const doc::Feature& f = m.features[static_cast<size_t>(std::min<int>(index, static_cast<int>(m.features.size())) - 1)];
    return f.after ? f.after.get() : &empty;
}

} // namespace

ExtrudeCommand::ExtrudeCommand(CommandHost& host, const QUuid& sketch, bool cut, const QUuid& editing)
    : InteractiveCommand(host), m_editing(editing) {
    m_params.sketch = sketch;
    m_params.cut = cut;
    if (const doc::Document* d = host.activeDocument(); d && !editing.isNull()) {
        if (const doc::Feature* f = d->model().feature(editing)) {
            m_params = f->extrude;
        }
    }
}

void ExtrudeCommand::start() {
    buildPanel();
    preview();
}

void ExtrudeCommand::unitsChanged() {
    if (!m_building) {
        buildPanel();
    }
}

void ExtrudeCommand::buildPanel() {
    m_building = true;
    PropertyPanel& panel = m_host.panel();
    panel.clearContent();
    const Units u = m_host.units();
    const doc::Document* d = m_host.activeDocument();
    const doc::SketchFeature* sketch = d ? d->model().sketch(m_params.sketch) : nullptr;
    const QString sketchName = sketch ? sketch->name : tr("(no sketch)");
    panel.setMessage(m_params.cut ? tr("Cuts %1 out of the bodies it meets. The preview shows the material that "
                                       "is removed.").arg(sketchName)
                                  : tr("Extrudes the closed contours of %1. Contours inside contours become holes; "
                                       "the preview shows the material that is added.").arg(sketchName));

    PanelGroup* d1 = panel.addGroup(tr("Direction 1"));
    m_end = d1->addChoice(tr("End condition"), {tr("Blind"), tr("Through All"), tr("Mid Plane")},
                          static_cast<int>(m_params.end));
    m_reverse = d1->addCheck(tr("Reverse direction"), m_params.reverse);
    m_depth = d1->addLength(tr("Depth"), m_params.depth, u);
    m_depth->setRange(1e-4, 1e6);
    m_depth->setEnabled(m_params.end != doc::EndCondition::ThroughAll);

    m_direction2 = panel.addGroup(tr("Direction 2"), true);
    m_direction2->setChecked(m_params.direction2);
    m_depth2 = m_direction2->addLength(tr("Depth"), m_params.depth2, u);
    m_depth2->setRange(1e-4, 1e6);
    m_direction2->setEnabled(m_params.end != doc::EndCondition::MidPlane);
    m_direction2->setExpanded(m_params.direction2); // unchecked: just the header, as in the familiar panels

    if (!m_params.cut) {
        PanelGroup* options = panel.addGroup(tr("Options"));
        QCheckBox* merge = options->addCheck(tr("Merge result"), m_params.merge);
        merge->setToolTip(tr("Join the new solid to the bodies it touches; off: always a new body."));
        connect(merge, &QCheckBox::toggled, this, [this](bool on) {
            m_params.merge = on;
            preview();
        });
    }

    connect(m_end, &QComboBox::currentIndexChanged, this, [this](int index) {
        setEnd(static_cast<doc::EndCondition>(index));
    });
    connect(m_reverse, &QCheckBox::toggled, this, [this](bool on) { setReverse(on); });
    connect(m_depth, &LengthField::valueChanged, this, [this](double mm) { setDepth(mm); });
    connect(m_direction2, &PanelGroup::toggled, this, [this](bool on) {
        m_params.direction2 = on;
        m_direction2->setExpanded(on);
        preview();
    });
    connect(m_depth2, &LengthField::valueChanged, this, [this](double mm) {
        m_params.depth2 = mm;
        preview();
    });
    m_building = false;
}

void ExtrudeCommand::setDepth(double mm) {
    if (mm <= 0.0) {
        return;
    }
    m_params.depth = mm;
    if (m_depth && m_depth->value() != mm) {
        const QSignalBlocker block(m_depth);
        m_depth->setValue(mm);
    }
    preview();
}

void ExtrudeCommand::setEnd(doc::EndCondition end) {
    m_params.end = end;
    if (m_end && m_end->currentIndex() != static_cast<int>(end)) {
        const QSignalBlocker block(m_end);
        m_end->setCurrentIndex(static_cast<int>(end));
    }
    if (m_depth) {
        m_depth->setEnabled(end != doc::EndCondition::ThroughAll);
    }
    if (m_direction2) {
        m_direction2->setEnabled(end != doc::EndCondition::MidPlane);
    }
    preview();
}

void ExtrudeCommand::setReverse(bool on) {
    m_params.reverse = on;
    if (m_reverse && m_reverse->isChecked() != on) {
        const QSignalBlocker block(m_reverse);
        m_reverse->setChecked(on);
    }
    preview();
}

void ExtrudeCommand::clearPreview() {
    if (!m_preview.IsNull()) {
        if (DocumentView* view = m_host.activeView()) {
            view->viewer()->remove(m_preview);
        }
        m_preview.Nullify();
    }
}

void ExtrudeCommand::preview() {
    clearPreview();
    DocumentView* view = m_host.activeView();
    const doc::Document* d = m_host.activeDocument();
    if (!view || !d) {
        return;
    }
    const doc::PartModel& m = d->model();
    const doc::SketchFeature* sketch = m.sketch(m_params.sketch);
    if (!sketch) {
        m_host.showStatus(tr("The sketch to extrude is gone."));
        return;
    }
    const int index = m_editing.isNull() ? (m.rollback >= 0 ? m.rollback : static_cast<int>(m.features.size()))
                                         : m.featureIndex(m_editing);
    QString error;
    const kernel::NamedShape tool = doc::extrudeTool(*sketch, m_params, *stateFor(m, index), "preview", &error);
    if (tool.isNull()) {
        m_host.showStatus(error, 6000);
        return;
    }
    m_preview = new AIS_Shape(tool.shape);
    m_preview->SetColor(m_params.cut ? rgb(235, 110, 70) : rgb(245, 200, 60));
    m_preview->SetTransparency(0.45);
    m_preview->SetDisplayMode(AIS_Shaded);
    view->viewer()->display(m_preview, false);
    view->viewer()->context()->Deactivate(m_preview); // not pickable
    view->viewer()->redraw();
}

doc::PartModel ExtrudeCommand::applied(QUuid* featureId) const {
    const doc::Document* d = m_host.activeDocument();
    doc::PartModel next = d->model();
    *featureId = {};
    if (!m_editing.isNull()) {
        if (doc::Feature* f = next.feature(m_editing)) {
            f->extrude = m_params;
            *featureId = f->id;
        }
    } else {
        doc::Feature f;
        f.type = doc::FeatureType::Extrude;
        f.name = m_params.cut ? tr("ExtrudeCut%1").arg(++next.cutCount) : tr("Extrude%1").arg(++next.extrudeCount);
        f.body = QUuid::createUuid();
        f.extrude = m_params;
        *featureId = f.id;
        if (next.rollback >= 0 && next.rollback <= static_cast<int>(next.features.size())) {
            next.features.insert(next.features.begin() + next.rollback, f);
            ++next.rollback;
        } else {
            next.features.push_back(f);
        }
        if (doc::SketchFeature* s = next.sketch(m_params.sketch)) {
            s->visible = false; // the feature shows the result; the sketch goes out of the way
        }
    }
    doc::rebuild(next, &d->model());
    return next;
}

bool ExtrudeCommand::accept() {
    doc::Document* d = m_host.activeDocument();
    if (!d) {
        return true;
    }
    QUuid id;
    doc::PartModel next = applied(&id);
    const doc::Feature* f = next.feature(id);
    if (!f) {
        return true; // the edited feature is gone
    }
    if (f->state == doc::FeatureState::Error) {
        m_host.showStatus(f->message, 8000);
        return false; // stay open so the values can be fixed
    }
    clearPreview();
    const QString name = f->name;
    d->transact(m_editing.isNull() ? tr("New %1").arg(name) : tr("Edit %1").arg(name), [&next](doc::PartModel& m) {
        m = next;
        return true;
    });
    m_host.showStatus(m_editing.isNull() ? tr("%1 added.").arg(name) : tr("%1 changed.").arg(name));
    return true;
}

void ExtrudeCommand::cancel() { clearPreview(); }

} // namespace bl::ui
