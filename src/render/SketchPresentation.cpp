#include "render/SketchPresentation.h"

#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_AspectText3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_Text.hxx>
#include <Prs3d_TextAspect.hxx>

#include <map>
#include <tuple>

namespace bl::render {

gp_Pnt SketchPresentation::to3d(const gp_Pnt2d& p) const {
    const gp_Pnt o = m_plane.Location();
    const gp_Dir& x = m_plane.XDirection();
    const gp_Dir& y = m_plane.YDirection();
    return gp_Pnt(o.X() + p.X() * x.X() + p.Y() * y.X(), o.Y() + p.X() * x.Y() + p.Y() * y.Y(),
                  o.Z() + p.X() * x.Z() + p.Y() * y.Z());
}

void SketchPresentation::Compute(const Handle(PrsMgr_PresentationManager)&, const Handle(Prs3d_Presentation)& prs,
                                 const int mode) {
    if (mode != 0) {
        return;
    }
    // Batch strokes and markers by look: one group (one draw call) per style.
    using Key = std::tuple<double, double, double, float, int>;
    auto keyOf = [](const Quantity_Color& c, float size, int type) {
        double r = 0, g = 0, b = 0;
        c.Values(r, g, b, Quantity_TOC_RGB);
        return Key{r, g, b, size, type};
    };

    std::map<Key, std::vector<const Drawing2d::Stroke*>> strokes;
    for (const Drawing2d::Stroke& s : m_drawing.strokes) {
        if (s.points.size() >= 2) {
            strokes[keyOf(s.color, s.width, s.type)].push_back(&s);
        }
    }
    for (const auto& [key, list] : strokes) {
        int vertices = 0;
        for (const Drawing2d::Stroke* s : list) {
            vertices += static_cast<int>(s->points.size());
        }
        Handle(Graphic3d_ArrayOfPolylines) array = new Graphic3d_ArrayOfPolylines(vertices, static_cast<int>(list.size()));
        for (const Drawing2d::Stroke* s : list) {
            array->AddBound(static_cast<int>(s->points.size()));
            for (const gp_Pnt2d& p : s->points) {
                array->AddVertex(to3d(p));
            }
        }
        const Drawing2d::Stroke* first = list.front();
        Handle(Graphic3d_Group) group = prs->NewGroup();
        group->SetGroupPrimitivesAspect(new Graphic3d_AspectLine3d(first->color, first->type, first->width));
        group->AddPrimitiveArray(array);
    }

    std::map<Key, std::vector<const Drawing2d::Marker*>> markers;
    for (const Drawing2d::Marker& m : m_drawing.markers) {
        markers[keyOf(m.color, m.scale, m.type)].push_back(&m);
    }
    for (const auto& [key, list] : markers) {
        Handle(Graphic3d_ArrayOfPoints) array = new Graphic3d_ArrayOfPoints(static_cast<int>(list.size()));
        for (const Drawing2d::Marker* m : list) {
            array->AddVertex(to3d(m->at));
        }
        const Drawing2d::Marker* first = list.front();
        Handle(Graphic3d_Group) group = prs->NewGroup();
        group->SetGroupPrimitivesAspect(new Graphic3d_AspectMarker3d(first->type, first->color, first->scale));
        group->AddPrimitiveArray(array);
    }

    for (const Drawing2d::Label& l : m_drawing.labels) {
        Handle(Prs3d_TextAspect) aspect = new Prs3d_TextAspect();
        aspect->SetColor(l.color);
        aspect->SetHeight(l.height);
        aspect->SetHorizontalJustification(Graphic3d_HTA_CENTER);
        aspect->SetVerticalJustification(Graphic3d_VTA_CENTER);
        aspect->Aspect()->SetFont("Arial");
        if (l.box) {
            aspect->Aspect()->SetDisplayType(Aspect_TODT_SUBTITLE);
            aspect->Aspect()->SetColorSubTitle(*l.box);
        }
        Handle(Graphic3d_Group) group = prs->NewGroup();
        Prs3d_Text::Draw(group, aspect, l.text, to3d(l.at));
    }
}

} // namespace bl::render
