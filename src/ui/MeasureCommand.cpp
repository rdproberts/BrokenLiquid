#include "ui/MeasureCommand.h"

#include "kernel/Measure.h"
#include "ui/DocumentView.h"
#include "ui/PropertyPanel.h"

#include <AIS_Shape.hxx>
#include <AIS_TextLabel.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_TextAspect.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TopoDS_Edge.hxx>

#include <QLabel>

#include <cmath>

namespace bl::ui {
namespace {

QString q(const std::string& s) { return QString::fromStdString(s); }

QString kindName(kernel::EntityKind kind) {
    switch (kind) {
    case kernel::EntityKind::Vertex: return QObject::tr("Vertex");
    case kernel::EntityKind::Edge: return QObject::tr("Edge");
    case kernel::EntityKind::Face: return QObject::tr("Face");
    case kernel::EntityKind::Solid: return QObject::tr("Body");
    case kernel::EntityKind::Other: break;
    }
    return QObject::tr("Entity");
}

// Two-column result table; the panel is narrow, so labels stay short.
class Table {
public:
    void row(const QString& label, const QString& value) {
        m_html += QStringLiteral("<tr><td style='padding-right:10px'>%1</td><td><b>%2</b></td></tr>")
                      .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
    }
    void heading(const QString& text) {
        m_html += QStringLiteral("<tr><td colspan='2' style='padding-top:6px'><i>%1</i></td></tr>").arg(text.toHtmlEscaped());
    }
    QString html() const { return QStringLiteral("<table cellspacing='0' cellpadding='1'>%1</table>").arg(m_html); }

private:
    QString m_html;
};

QString point(const gp_Pnt& p, const Units& u) {
    return QStringLiteral("%1, %2, %3")
        .arg(q(units::formatLength(p.X(), u, false)), q(units::formatLength(p.Y(), u, false)),
             q(units::formatLength(p.Z(), u)));
}

} // namespace

void MeasureCommand::start() {
    PropertyPanel& panel = m_host.panel();
    panel.setMessage(tr("Select vertices, edges, faces or bodies. Two picks give the distance and angle "
                        "between them."));
    PanelGroup* picks = panel.addGroup(tr("Selections"));
    m_box = picks->addSelectionBox(tr("Pick entities in the graphics area"), 4);
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

    PanelGroup* results = panel.addGroup(tr("Results"));
    m_result = results->addText();
    m_result->setWordWrap(true);
    m_result->setTextFormat(Qt::RichText);
    m_result->setTextInteractionFlags(Qt::TextSelectableByMouse);

    if (DocumentView* view = m_host.activeView()) {
        view->viewer()->setSelectionFilter(true, true, true);
    }
    selectionChanged();
}

void MeasureCommand::selectionChanged() {
    DocumentView* view = m_host.activeView();
    if (!view || !m_result) {
        return;
    }
    render::OcctViewer* viewer = view->viewer();
    viewer->clearTemporary();
    const Units u = m_host.units();
    const auto picks = viewer->selection();

    std::vector<TopoDS_Shape> shapes;
    QStringList entries;
    for (const auto& pick : picks) {
        if (pick.shape.IsNull()) {
            continue;
        }
        shapes.push_back(pick.shape);
        const kernel::EntityInfo info = kernel::describe(pick.shape);
        QString name = kindName(info.kind);
        if (!info.geometry.empty() && info.kind != kernel::EntityKind::Vertex && info.kind != kernel::EntityKind::Solid) {
            name += QStringLiteral(" (%1)").arg(q(info.geometry));
        }
        entries << QStringLiteral("%1<%2>").arg(name).arg(entries.size() + 1);
    }
    m_box->setEntries(entries);

    Table t;
    if (shapes.empty()) {
        m_result->setText(tr("Nothing selected."));
        return;
    }

    if (shapes.size() == 1) {
        const kernel::EntityInfo info = kernel::describe(shapes.front());
        t.heading(kindName(info.kind) + (info.geometry.empty() ? QString() : QStringLiteral(" — ") + q(info.geometry)));
        switch (info.kind) {
        case kernel::EntityKind::Vertex:
            if (info.point) {
                t.row(tr("X, Y, Z"), point(*info.point, u));
            }
            break;
        case kernel::EntityKind::Edge:
            t.row(tr("Length"), q(units::formatLength(info.length, u)));
            break;
        case kernel::EntityKind::Face:
            t.row(tr("Area"), q(units::formatArea(info.area, u)));
            t.row(tr("Perimeter"), q(units::formatLength(info.perimeter, u)));
            break;
        case kernel::EntityKind::Solid:
            t.row(tr("Volume"), q(units::formatVolume(info.volume, u)));
            t.row(tr("Surface area"), q(units::formatArea(info.area, u)));
            break;
        case kernel::EntityKind::Other: break;
        }
        if (info.radius) {
            t.row(tr("Radius"), q(units::formatLength(*info.radius, u)));
            t.row(tr("Diameter"), q(units::formatLength(*info.radius * 2.0, u)));
        }
        if (info.center) {
            t.row(tr("Center"), point(*info.center, u));
        }
        m_result->setText(t.html());
        return;
    }

    if (shapes.size() == 2) {
        const kernel::PairMeasure m = kernel::measurePair(shapes[0], shapes[1]);
        if (!m.valid) {
            m_result->setText(tr("Could not measure between these entities."));
            return;
        }
        t.row(tr("Distance"), q(units::formatLength(m.distance, u)));
        t.row(tr("dX"), q(units::formatLength(std::abs(m.delta.X()), u)));
        t.row(tr("dY"), q(units::formatLength(std::abs(m.delta.Y()), u)));
        t.row(tr("dZ"), q(units::formatLength(std::abs(m.delta.Z()), u)));
        if (m.centerDistance) {
            t.row(tr("Center distance"), q(units::formatLength(*m.centerDistance, u)));
        }
        if (m.angle) {
            t.row(tr("Angle"), q(units::formatAngle(*m.angle, u)));
        }
        if (m.parallel) {
            t.row(tr("Relation"), tr("Parallel"));
        }
        m_result->setText(t.html());

        // Dimension line between the closest points, drawn on top.
        if (m.distance > 1e-9) {
            const TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(m.p1, m.p2).Edge();
            Handle(AIS_Shape) line = new AIS_Shape(edge);
            line->SetColor(Quantity_Color(0.133, 0.765, 0.902, Quantity_TOC_sRGB)); // #22C3E6
            line->SetWidth(2.5);
            viewer->showTemporary(line);

            Handle(AIS_TextLabel) label = new AIS_TextLabel();
            label->SetText(TCollection_ExtendedString(units::formatLength(m.distance, u).c_str(), true));
            label->SetPosition(gp_Pnt((m.p1.XYZ() + m.p2.XYZ()) * 0.5));
            label->SetColor(Quantity_Color(0.055, 0.102, 0.169, Quantity_TOC_sRGB)); // ink navy
            label->SetHeight(15);
            const Handle(Graphic3d_AspectText3d)& text = label->Attributes()->TextAspect()->Aspect();
            text->SetDisplayType(Aspect_TODT_SUBTITLE); // filled box behind the text
            text->SetColorSubTitle(Quantity_Color(0.89, 0.965, 0.984, Quantity_TOC_sRGB)); // #E3F6FB
            viewer->showTemporary(label);
        }
        return;
    }

    const kernel::Totals sum = kernel::totals(shapes);
    t.heading(tr("%n entities", nullptr, static_cast<int>(shapes.size())));
    if (sum.edges) {
        t.row(tr("Total length (%n edges)", nullptr, sum.edges), q(units::formatLength(sum.length, u)));
    }
    if (sum.faces) {
        t.row(tr("Total area (%n faces)", nullptr, sum.faces), q(units::formatArea(sum.area, u)));
    }
    if (sum.solids) {
        t.row(tr("Total volume (%n bodies)", nullptr, sum.solids), q(units::formatVolume(sum.volume, u)));
    }
    m_result->setText(t.html());
}

void MeasureCommand::cancel() {
    if (DocumentView* view = m_host.activeView()) {
        view->viewer()->clearTemporary();
    }
}

} // namespace bl::ui
