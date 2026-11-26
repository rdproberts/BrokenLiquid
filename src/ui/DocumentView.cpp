#include "ui/DocumentView.h"

#include "doc/Document.h"
#include "ui/ConfirmationCorner.h"
#include "ui/SketchEditor.h"
#include "ui/Theme.h"
#include "ui/ViewHud.h"

#include <AIS_Shape.hxx>
#include <BRepBndLib.hxx>
#include <XCAFPrs_AISObject.hxx>

#include <QVBoxLayout>

#include <algorithm>

namespace bl::ui {
namespace {

Quantity_Color toOcct(const QColor& c) { return Quantity_Color(c.redF(), c.greenF(), c.blueF(), Quantity_TOC_sRGB); }

} // namespace

gp_Pln SectionSpec::basePlane() const {
    switch (reference) {
    case Front: return gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
    case Top: return gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
    case Right: return gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
    case Face: return face;
    }
    return face;
}

gp_Pln SectionSpec::plane() const {
    const gp_Pln base = basePlane();
    const gp_Dir n = base.Axis().Direction();
    const gp_Pnt origin = base.Location().Translated(gp_Vec(n) * offset);
    return gp_Pln(origin, flip ? n.Reversed() : n);
}

DocumentView::DocumentView(doc::Document* document, CommandRegistry& registry, QWidget* parent)
    : QWidget(parent), m_document(document), m_viewer(new render::OcctViewer(this)) {
    setObjectName(QStringLiteral("GraphicsArea"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_viewer);

    m_hud = new ViewHud(registry, this);
    m_hud->addCommand(QStringLiteral("view.fit"));
    m_hud->addCommand(QStringLiteral("view.zoomArea"));
    m_hud->addCommand(QStringLiteral("view.previous"));
    m_hud->addSeparator();
    m_hud->addCommand(QStringLiteral("view.section"));
    m_hud->addFlyout(QStringLiteral("view-orientation"), tr("View Orientation"),
                     {QStringLiteral("view.front"), QStringLiteral("view.back"), QStringLiteral("view.left"),
                      QStringLiteral("view.right"), QStringLiteral("view.top"), QStringLiteral("view.bottom"),
                      QStringLiteral("view.isometric"), QStringLiteral("view.dimetric"),
                      QStringLiteral("view.trimetric"), QStringLiteral("view.normalTo")});
    m_hud->addFlyout(QStringLiteral("view-style"), tr("Display Style"),
                     {QStringLiteral("view.style.shadedEdges"), QStringLiteral("view.style.shaded"),
                      QStringLiteral("view.style.hlr"), QStringLiteral("view.style.hlv"),
                      QStringLiteral("view.style.wireframe")});

    m_corner = new ConfirmationCorner(this);

    connect(m_viewer, &render::OcctViewer::glReady, this, [this] {
        m_viewer->setStandardView(render::StandardView::Isometric);
        m_fitted = !m_document->model().bodies.empty();
    });
    connect(m_document, &doc::Document::modelChanged, this, &DocumentView::sync);
    sync();
}

void DocumentView::setBodiesOverride(std::optional<std::vector<doc::Body>> bodies) {
    m_override = bodies ? std::make_shared<const std::vector<doc::Body>>(std::move(*bodies)) : nullptr;
    sync();
}

void DocumentView::sync() {
    const doc::PartModel& model = m_document->model();
    const Handle(AIS_InteractiveContext)& ctx = m_viewer->context();
    const std::vector<doc::Body>& bodies = m_override ? *m_override : model.bodies;
    auto shown = [&bodies](const QUuid& id) {
        return std::any_of(bodies.begin(), bodies.end(), [&id](const doc::Body& b) { return b.id == id; });
    };

    // Drop objects whose body is gone.
    for (auto it = m_objects.begin(); it != m_objects.end();) {
        if (!shown(it->first)) {
            m_viewer->remove(it->second.object);
            it = m_objects.erase(it);
        } else {
            ++it;
        }
    }

    for (const doc::Body& body : bodies) {
        auto it = m_objects.find(body.id);
        const bool rebuild = it == m_objects.end() || !it->second.shape.IsSame(body.shape) || it->second.color != body.color;
        if (rebuild) {
            if (it != m_objects.end()) {
                m_viewer->remove(it->second.object);
                m_objects.erase(it);
            }
            Entry entry;
            entry.shape = body.shape;
            entry.color = body.color;
            entry.visible = body.visible;
            if (!body.color && !body.xcaf.IsNull() && !body.label.IsNull()) {
                entry.object = new XCAFPrs_AISObject(body.label); // keeps imported colours
            } else {
                Handle(AIS_Shape) shape = new AIS_Shape(body.shape);
                if (body.color) {
                    shape->SetColor(toOcct(*body.color));
                }
                entry.object = shape;
            }
            if (body.visible) {
                m_viewer->display(entry.object, false);
            }
            m_objects.emplace(body.id, entry);
            continue;
        }
        Entry& entry = it->second;
        if (entry.visible != body.visible) {
            entry.visible = body.visible;
            if (body.visible) {
                m_viewer->display(entry.object, false);
            } else {
                ctx->Erase(entry.object, false);
                m_viewer->redraw();
            }
        }
    }

    // Finished sketches: rebuilt each time (cheap), the one being edited is left to the editor.
    for (auto& [id, prs] : m_sketches) {
        m_viewer->remove(prs);
    }
    m_sketches.clear();
    const bool dark = currentTheme() == ThemeKind::Dark;
    for (const doc::SketchFeature& f : model.sketches) {
        if (!f.visible || f.id == m_editingSketch) {
            continue;
        }
        Handle(render::SketchPresentation) prs = new render::SketchPresentation(f.plane);
        prs->setDrawing(inactiveSketchDrawing(f.sketch, dark));
        m_viewer->showOverlay(prs, false);
        m_sketches.emplace(f.id, prs);
    }

    if (!m_fitted && !model.bodies.empty() && m_viewer->isGlReady()) {
        m_fitted = true;
        m_viewer->setStandardView(render::StandardView::Isometric);
    }
    m_viewer->redraw();
}

void DocumentView::setEditingSketch(const QUuid& id) {
    if (id != m_editingSketch) {
        m_editingSketch = id;
        sync();
    }
}

QUuid DocumentView::bodyOf(const Handle(AIS_InteractiveObject)& object) const {
    for (const auto& [id, entry] : m_objects) {
        if (entry.object == object) {
            return id;
        }
    }
    return {};
}

Handle(AIS_InteractiveObject) DocumentView::objectOf(const QUuid& body) const {
    const auto it = m_objects.find(body);
    return it == m_objects.end() ? Handle(AIS_InteractiveObject)() : it->second.object;
}

bool DocumentView::bounds(Bnd_Box& box) const {
    box.SetVoid();
    for (const doc::Body& body : m_document->model().bodies) {
        if (body.visible && !body.shape.IsNull()) {
            BRepBndLib::Add(body.shape, box);
        }
    }
    return !box.IsVoid();
}

QList<QUuid> DocumentView::selectedBodies() const {
    QList<QUuid> ids;
    for (const render::OcctViewer::Pick& pick : m_viewer->selection()) {
        const QUuid id = bodyOf(pick.object);
        if (!id.isNull() && !ids.contains(id)) {
            ids << id;
        }
    }
    return ids;
}

void DocumentView::setSection(const std::optional<SectionSpec>& spec) {
    m_section = spec;
    previewSection(spec);
}

void DocumentView::previewSection(const std::optional<SectionSpec>& spec) {
    if (spec) {
        m_viewer->setSection(spec->plane(), spec->cap);
    } else {
        m_viewer->clearSection();
    }
}

void DocumentView::selectBodies(const QList<QUuid>& ids) {
    const Handle(AIS_InteractiveContext)& ctx = m_viewer->context();
    ctx->ClearSelected(false);
    for (const QUuid& id : ids) {
        const auto it = m_objects.find(id);
        if (it != m_objects.end() && it->second.visible) {
            ctx->AddOrRemoveSelected(it->second.object, false);
        }
    }
    m_viewer->redraw();
}

} // namespace bl::ui
