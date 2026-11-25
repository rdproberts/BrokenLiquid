#include "render/SketchPresentation.h"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Poly_Triangulation.hxx>
#include <ShapeFix_Face.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>
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

namespace {
bool sameLoops(const std::vector<std::vector<gp_Pnt2d>>& a, const std::vector<std::vector<gp_Pnt2d>>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) {
            return false;
        }
        for (size_t j = 0; j < a[i].size(); ++j) {
            if (a[i][j].SquareDistance(b[i][j]) > 1e-18) {
                return false;
            }
        }
    }
    return true;
}
} // namespace

Handle(Graphic3d_ArrayOfTriangles) SketchPresentation::meshFill(const Drawing2d::Fill& fill) {
    for (const FillMesh& m : m_fillCache) {
        if (sameLoops(m.loops, fill.loops)) {
            return m.triangles;
        }
    }
    // A planar face with the loops as polygon wires, meshed by OCCT (handles holes).
    auto wireOf = [this](const std::vector<gp_Pnt2d>& loop) {
        BRepBuilderAPI_MakePolygon polygon;
        for (const gp_Pnt2d& p : loop) {
            polygon.Add(to3d(p));
        }
        polygon.Close();
        return polygon.IsDone() ? polygon.Wire() : TopoDS_Wire();
    };
    Handle(Graphic3d_ArrayOfTriangles) result;
    if (fill.loops.empty() || fill.loops.front().size() < 3) {
        return result;
    }
    const TopoDS_Wire outer = wireOf(fill.loops.front());
    if (outer.IsNull()) {
        return result;
    }
    BRepBuilderAPI_MakeFace maker(gp_Pln(m_plane), outer, true);
    for (size_t i = 1; i < fill.loops.size() && maker.IsDone(); ++i) {
        const TopoDS_Wire hole = wireOf(fill.loops[i]);
        if (!hole.IsNull()) {
            maker.Add(hole);
        }
    }
    if (!maker.IsDone()) {
        return result;
    }
    ShapeFix_Face fix(maker.Face());
    fix.FixOrientation();
    fix.Perform();
    const TopoDS_Face face = fix.Face();
    BRepMesh_IncrementalMesh mesher(face, 1.0, false, 0.5);
    TopLoc_Location location;
    const Handle(Poly_Triangulation) mesh = BRep_Tool::Triangulation(face, location);
    if (mesh.IsNull() || mesh->NbTriangles() == 0) {
        return result;
    }
    result = new Graphic3d_ArrayOfTriangles(mesh->NbNodes(), mesh->NbTriangles() * 3);
    for (int i = 1; i <= mesh->NbNodes(); ++i) {
        result->AddVertex(mesh->Node(i).Transformed(location.Transformation()));
    }
    for (int i = 1; i <= mesh->NbTriangles(); ++i) {
        int a = 0, b = 0, c = 0;
        mesh->Triangle(i).Get(a, b, c);
        result->AddEdges(a, b, c);
    }
    m_fillCache.push_back({fill.loops, result});
    if (m_fillCache.size() > 64) {
        m_fillCache.erase(m_fillCache.begin());
    }
    return result;
}

void SketchPresentation::Compute(const Handle(PrsMgr_PresentationManager)&, const Handle(Prs3d_Presentation)& prs,
                                 const int mode) {
    if (mode != 0) {
        return;
    }
    // Fills first, pushed back a little so the lines drawn over them win the depth test.
    for (const Drawing2d::Fill& f : m_drawing.fills) {
        const Handle(Graphic3d_ArrayOfTriangles) triangles = meshFill(f);
        if (triangles.IsNull()) {
            continue;
        }
        Handle(Graphic3d_AspectFillArea3d) aspect = new Graphic3d_AspectFillArea3d();
        aspect->SetInteriorStyle(Aspect_IS_SOLID);
        aspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
        aspect->SetInteriorColor(Quantity_ColorRGBA(f.color, 1.0f - f.transparency));
        aspect->SetAlphaMode(Graphic3d_AlphaMode_Blend);
        aspect->SetFaceCulling(Graphic3d_TypeOfBackfacingModel_DoubleSided);
        aspect->SetPolygonOffsets(Aspect_POM_Fill, 1.0f, 2.0f);
        aspect->SetDrawEdges(false);
        Handle(Graphic3d_Group) group = prs->NewGroup();
        group->SetGroupPrimitivesAspect(aspect);
        group->AddPrimitiveArray(triangles);
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
