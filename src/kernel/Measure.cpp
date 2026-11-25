#include "kernel/Measure.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>

#include <cmath>
#include <numbers>

namespace bl::kernel {
namespace {

double edgeLength(const TopoDS_Shape& e) {
    GProp_GProps props;
    BRepGProp::LinearProperties(e, props);
    return props.Mass();
}

bool isFullCircle(const BRepAdaptor_Curve& c) {
    return std::abs(c.LastParameter() - c.FirstParameter() - 2 * std::numbers::pi) < 1e-7;
}

// Angle between two directions folded into 0..pi/2 (lines and planes have no sense of direction).
double foldedAngle(const gp_Dir& a, const gp_Dir& b) {
    double ang = a.Angle(b);
    if (ang > std::numbers::pi / 2) {
        ang = std::numbers::pi - ang;
    }
    return ang;
}

} // namespace

EntityInfo describe(const TopoDS_Shape& shape) {
    EntityInfo info;
    if (shape.IsNull()) {
        return info;
    }
    try {
        switch (shape.ShapeType()) {
        case TopAbs_VERTEX:
            info.kind = EntityKind::Vertex;
            info.geometry = "Point";
            info.point = BRep_Tool::Pnt(TopoDS::Vertex(shape));
            break;
        case TopAbs_EDGE: {
            info.kind = EntityKind::Edge;
            info.length = edgeLength(shape);
            BRepAdaptor_Curve c(TopoDS::Edge(shape));
            switch (c.GetType()) {
            case GeomAbs_Line:
                info.geometry = "Line";
                info.axis = gp_Ax1(c.Line().Location(), c.Line().Direction());
                break;
            case GeomAbs_Circle:
                info.geometry = isFullCircle(c) ? "Circle" : "Arc";
                info.radius = c.Circle().Radius();
                info.center = c.Circle().Location();
                info.axis = c.Circle().Axis();
                break;
            case GeomAbs_Ellipse: info.geometry = "Ellipse"; break;
            case GeomAbs_BSplineCurve:
            case GeomAbs_BezierCurve: info.geometry = "Spline"; break;
            default: info.geometry = "Curve"; break;
            }
            break;
        }
        case TopAbs_FACE: {
            info.kind = EntityKind::Face;
            GProp_GProps props;
            BRepGProp::SurfaceProperties(shape, props);
            info.area = props.Mass();
            for (TopExp_Explorer ex(shape, TopAbs_EDGE); ex.More(); ex.Next()) {
                if (!BRep_Tool::Degenerated(TopoDS::Edge(ex.Current()))) {
                    info.perimeter += edgeLength(ex.Current());
                }
            }
            const TopoDS_Face& face = TopoDS::Face(shape);
            BRepAdaptor_Surface s(face);
            switch (s.GetType()) {
            case GeomAbs_Plane: {
                info.geometry = "Plane";
                gp_Ax1 n = s.Plane().Axis();
                if (face.Orientation() == TopAbs_REVERSED) {
                    n.Reverse();
                }
                info.axis = n;
                break;
            }
            case GeomAbs_Cylinder:
                info.geometry = "Cylinder";
                info.radius = s.Cylinder().Radius();
                info.axis = s.Cylinder().Axis();
                info.center = s.Cylinder().Location();
                break;
            case GeomAbs_Cone:
                info.geometry = "Cone";
                info.radius = s.Cone().RefRadius();
                info.axis = s.Cone().Axis();
                break;
            case GeomAbs_Sphere:
                info.geometry = "Sphere";
                info.radius = s.Sphere().Radius();
                info.center = s.Sphere().Location();
                break;
            case GeomAbs_Torus: info.geometry = "Torus"; break;
            case GeomAbs_BSplineSurface:
            case GeomAbs_BezierSurface: info.geometry = "Freeform"; break;
            default: info.geometry = "Surface"; break;
            }
            break;
        }
        case TopAbs_SOLID:
        case TopAbs_COMPSOLID:
        case TopAbs_COMPOUND: {
            info.kind = EntityKind::Solid;
            info.geometry = "Body";
            GProp_GProps vol, surf;
            BRepGProp::VolumeProperties(shape, vol);
            BRepGProp::SurfaceProperties(shape, surf);
            info.volume = vol.Mass();
            info.area = surf.Mass();
            break;
        }
        default: break;
        }
    } catch (const Standard_Failure&) {
        info.geometry = "Unknown";
    }
    return info;
}

PairMeasure measurePair(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    PairMeasure m;
    if (a.IsNull() || b.IsNull()) {
        return m;
    }
    try {
        BRepExtrema_DistShapeShape dist(a, b);
        if (dist.IsDone() && dist.NbSolution() > 0) {
            m.distance = dist.Value();
            m.p1 = dist.PointOnShape1(1);
            m.p2 = dist.PointOnShape2(1);
            m.delta = gp_Vec(m.p1, m.p2);
            m.valid = true;
        }
    } catch (const Standard_Failure&) {
        return m;
    }

    const EntityInfo ia = describe(a), ib = describe(b);

    // Centre distance: circle/arc/sphere centres, or parallel cylinder axes.
    const bool cylA = ia.geometry == "Cylinder", cylB = ib.geometry == "Cylinder";
    if (ia.center && ib.center && !cylA && !cylB) {
        m.c1 = *ia.center;
        m.c2 = *ib.center;
        m.centerDistance = m.c1.Distance(m.c2);
    } else if ((cylA || ia.center) && (cylB || ib.center) && ia.axis && ib.axis &&
               ia.axis->Direction().IsParallel(ib.axis->Direction(), 1e-6)) {
        // Axis-to-axis (or circle centre to axis) distance.
        const gp_Lin la(*ia.axis), lb(*ib.axis);
        const gp_Pnt pa = cylA ? la.Location() : *ia.center;
        m.centerDistance = lb.Distance(pa);
        m.c1 = pa;
        const gp_Vec v(lb.Location(), pa);
        m.c2 = lb.Location().Translated(gp_Vec(lb.Direction()) * v.Dot(gp_Vec(lb.Direction())));
    }

    // Angle between straight things: line/line, plane/plane, line/plane, axis/axis.
    auto direction = [](const EntityInfo& i) -> std::optional<gp_Dir> {
        if (!i.axis) {
            return std::nullopt;
        }
        if (i.geometry == "Line" || i.geometry == "Plane" || i.geometry == "Cylinder" || i.geometry == "Circle" ||
            i.geometry == "Arc") {
            return i.axis->Direction();
        }
        return std::nullopt;
    };
    const auto da = direction(ia), db = direction(ib);
    if (da && db) {
        const bool planeA = ia.geometry == "Plane", planeB = ib.geometry == "Plane";
        const bool lineA = ia.geometry == "Line" || cylA, lineB = ib.geometry == "Line" || cylB;
        double ang = foldedAngle(*da, *db);
        if ((planeA && lineB) || (lineA && planeB)) {
            ang = std::numbers::pi / 2 - ang; // angle between a line and a plane
        }
        if ((planeA || lineA) && (planeB || lineB)) {
            m.angle = ang;
            m.parallel = std::abs(ang) < 1e-7;
        }
    }
    return m;
}

Totals totals(const std::vector<TopoDS_Shape>& shapes) {
    Totals t;
    for (const TopoDS_Shape& s : shapes) {
        const EntityInfo i = describe(s);
        switch (i.kind) {
        case EntityKind::Edge:
            ++t.edges;
            t.length += i.length;
            break;
        case EntityKind::Face:
            ++t.faces;
            t.area += i.area;
            break;
        case EntityKind::Vertex: ++t.vertices; break;
        case EntityKind::Solid:
            ++t.solids;
            t.volume += i.volume;
            t.area += i.area;
            break;
        default: break;
        }
    }
    return t;
}

} // namespace bl::kernel
