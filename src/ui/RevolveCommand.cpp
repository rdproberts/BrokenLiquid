#include "ui/RevolveCommand.h"

#include "doc/Document.h"
#include "doc/Rebuild.h"
#include "ui/DocumentView.h"
#include "ui/PropertyPanel.h"

#include <AIS_InteractiveContext.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSignalBlocker>

#include <algorithm>

namespace bl::ui {
namespace {

Quantity_Color rgb(int r, int g, int b) { return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB); }

} // namespace

RevolveCommand::RevolveCommand(CommandHost& host, const QUuid& sketch, bool cut, const QUuid& editing)
    : InteractiveCommand(host), m_editing(editing) {
    m_params.sketch = sketch;
    m_params.cut = cut;
    const doc::Document* d = host.activeDocument();
    if (!d) {
        return;
    }
    if (!editing.isNull()) {
        if (const doc::Feature* f = d->model().feature(editing)) {
            m_params = f->revolve;
        }
    } else if (const doc::SketchFeature* s = d->model().sketch(sketch)) {
        m_params.axis = doc::revolveAxis(s->sketch, 0); // the centerline, stored so later lines do not change it
    }
}

void RevolveCommand::start() {
    buildPanel();
    preview();
}

void RevolveCommand::unitsChanged() {
    if (!m_building) {
        buildPanel();
    }
}

void RevolveCommand::buildPanel() {
    m_building = true;
    PropertyPanel& panel = m_host.panel();
    panel.clearContent();
    const doc::Document* d = m_host.activeDocument();
    const doc::SketchFeature* sketch = d ? d->model().sketch(m_params.sketch) : nullptr;
    const QString sketchName = sketch ? sketch->name : tr("(no sketch)");
    panel.setMessage(m_params.cut ? tr("Turns the closed contours of %1 about the axis and removes the material they "
                                       "sweep through.").arg(sketchName)
                                  : tr("Turns the closed contours of %1 about the axis. Keep the profile on one side "
                                       "of the axis.").arg(sketchName));

    // Centerlines first, then the other lines of the sketch.
    m_axes.clear();
    QStringList names;
    if (sketch) {
        for (int pass = 0; pass < 2; ++pass) {
            for (const sketch::Curve& c : sketch->sketch.curves()) {
                if (c.type == sketch::CurveType::Line && c.construction == (pass == 0)) {
                    m_axes.push_back(c.id);
                    names << QString::fromStdString(sketch->sketch.name(c.id)) +
                                 (c.construction ? tr(" (centerline)") : QString());
                }
            }
        }
    }
    PanelGroup* axis = panel.addGroup(tr("Axis of Revolution"));
    const auto current = std::find(m_axes.begin(), m_axes.end(), m_params.axis);
    m_axis = axis->addChoice(tr("Axis"), names.isEmpty() ? QStringList{tr("(draw a centerline)")} : names,
                             current == m_axes.end() ? 0 : static_cast<int>(current - m_axes.begin()));
    m_axis->setEnabled(!names.isEmpty());

    PanelGroup* d1 = panel.addGroup(tr("Direction 1"));
    m_type = d1->addChoice(tr("Revolve type"), {tr("Blind"), tr("Mid Plane")}, m_params.midPlane ? 1 : 0);
    m_reverse = d1->addCheck(tr("Reverse direction"), m_params.reverse);
    m_angle = new QDoubleSpinBox;
    m_angle->setRange(0.01, 360.0);
    m_angle->setDecimals(std::clamp(m_host.units().angleDecimals, 0, 4));
    m_angle->setSuffix(QStringLiteral("°"));
    m_angle->setValue(m_params.angle);
    d1->addRow(tr("Angle"), m_angle);

    if (!m_params.cut) {
        PanelGroup* options = panel.addGroup(tr("Options"));
        QCheckBox* merge = options->addCheck(tr("Merge result"), m_params.merge);
        merge->setToolTip(tr("Join the new solid to the bodies it touches; off: always a new body."));
        connect(merge, &QCheckBox::toggled, this, [this](bool on) {
            m_params.merge = on;
            preview();
        });
    }

    connect(m_axis, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0 && index < static_cast<int>(m_axes.size())) {
            m_params.axis = m_axes[static_cast<size_t>(index)];
            preview();
        }
    });
    connect(m_type, &QComboBox::currentIndexChanged, this, [this](int index) { setMidPlane(index == 1); });
    connect(m_reverse, &QCheckBox::toggled, this, [this](bool on) { setReverse(on); });
    connect(m_angle, &QDoubleSpinBox::valueChanged, this, [this](double degrees) { setAngle(degrees); });
    m_building = false;
}

void RevolveCommand::setAngle(double degrees) {
    if (degrees <= 0.0 || degrees > 360.0) {
        return;
    }
    m_params.angle = degrees;
    if (m_angle && m_angle->value() != degrees) {
        const QSignalBlocker block(m_angle);
        m_angle->setValue(degrees);
    }
    preview();
}

void RevolveCommand::setMidPlane(bool on) {
    m_params.midPlane = on;
    if (m_type && m_type->currentIndex() != (on ? 1 : 0)) {
        const QSignalBlocker block(m_type);
        m_type->setCurrentIndex(on ? 1 : 0);
    }
    preview();
}

void RevolveCommand::setReverse(bool on) {
    m_params.reverse = on;
    if (m_reverse && m_reverse->isChecked() != on) {
        const QSignalBlocker block(m_reverse);
        m_reverse->setChecked(on);
    }
    preview();
}

void RevolveCommand::clearPreview() {
    if (!m_preview.IsNull()) {
        if (DocumentView* view = m_host.activeView()) {
            view->viewer()->remove(m_preview);
        }
        m_preview.Nullify();
    }
}

void RevolveCommand::preview() {
    clearPreview();
    DocumentView* view = m_host.activeView();
    const doc::Document* d = m_host.activeDocument();
    if (!view || !d) {
        return;
    }
    const doc::SketchFeature* sketch = d->model().sketch(m_params.sketch);
    if (!sketch) {
        m_host.showStatus(tr("The sketch to revolve is gone."));
        return;
    }
    QString error;
    const kernel::NamedShape tool = doc::revolveTool(*sketch, m_params, "preview", &error);
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

doc::PartModel RevolveCommand::applied(QUuid* featureId) const {
    const doc::Document* d = m_host.activeDocument();
    doc::PartModel next = d->model();
    *featureId = {};
    if (!m_editing.isNull()) {
        if (doc::Feature* f = next.feature(m_editing)) {
            f->revolve = m_params;
            *featureId = f->id;
        }
    } else {
        doc::Feature f;
        f.type = doc::FeatureType::Revolve;
        f.name = m_params.cut ? tr("Cut-Revolve%1").arg(++next.revolveCutCount) : tr("Revolve%1").arg(++next.revolveCount);
        f.body = QUuid::createUuid();
        f.revolve = m_params;
        *featureId = f.id;
        if (next.rollback >= 0 && next.rollback <= static_cast<int>(next.features.size())) {
            next.features.insert(next.features.begin() + next.rollback, f);
            ++next.rollback;
        } else {
            next.features.push_back(f);
        }
        if (doc::SketchFeature* s = next.sketch(m_params.sketch)) {
            s->visible = false;
        }
    }
    doc::rebuild(next, &d->model());
    return next;
}

bool RevolveCommand::accept() {
    doc::Document* d = m_host.activeDocument();
    if (!d) {
        return true;
    }
    QUuid id;
    doc::PartModel next = applied(&id);
    const doc::Feature* f = next.feature(id);
    if (!f) {
        return true;
    }
    if (f->state == doc::FeatureState::Error) {
        m_host.showStatus(f->message, 8000);
        return false;
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

void RevolveCommand::cancel() { clearPreview(); }

} // namespace bl::ui
