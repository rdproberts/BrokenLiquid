#include "kernel/Features.h"

#include "sketch/Geometry.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <ElCLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Geom_Circle.hxx>
#include <ShapeFix_Face.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>

namespace bl::kernel {
namespace {

using sketch::Curve;
using sketch::CurveType;
using sketch::Loop;
using sketch::P2;
using sketch::Piece;
using sketch::Region;

constexpr double kVertexTolerance = 1e-6;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

void fail(std::string* error, const std::string& text) {
    if (error) {
        *error = text;
    }
}

// Even-odd test of a point against one closed polygon.
bool insidePolygon(const std::vector<P2>& poly, P2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const P2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            in = !in;
        }
    }
    return in;
}

// A point inside the region (inside the outer loop, outside its holes).
std::optional<P2> interiorPoint(const Region& r) {
    const std::vector<P2>& poly = r.outer.polygon;
    double extent = 0.0;
    for (const P2& p : poly) {
        extent = std::max({extent, std::abs(p.x - poly.front().x), std::abs(p.y - poly.front().y)});
    }
    for (double eps : {1e-4, 1e-3, 1e-2}) {
        const double step = std::max(extent, 1.0) * eps;
        for (size_t i = 0; i < poly.size(); ++i) {
            const P2 a = poly[i], b = poly[(i + 1) % poly.size()];
            const P2 d = b - a;
            const double len = sketch::length(d);
            if (len < 1e-12) {
                continue;
            }
            const P2 inward{-d.y / len, d.x / len}; // left of a counter-clockwise outline
            const P2 p = (a + b) * 0.5 + inward * step;
            if (sketch::regionContains(r, p)) {
                return p;
            }
        }
    }
    return std::nullopt;
}

class PlaneMap {
public:
    explicit PlaneMap(const gp_Ax3& plane) : m_plane(plane) {}
    gp_Pnt at(P2 p) const {
        return m_plane.Location()
            .Translated(gp_Vec(m_plane.XDirection()) * p.x)
            .Translated(gp_Vec(m_plane.YDirection()) * p.y);
    }
    gp_Dir normal() const { return m_plane.Direction(); }
    gp_Dir xDir() const { return m_plane.XDirection(); }

private:
    gp_Ax3 m_plane;
};

// One closed wire for a loop; consecutive edges share their vertices so the wire keeps them as made.
TopoDS_Wire wireOf(const sketch::Sketch& s, const Loop& loop, const PlaneMap& map,
                   std::vector<std::pair<TopoDS_Edge, sketch::Id>>& edges, std::string* error) {
    const size_t n = loop.pieces.size();
    if (n == 0) {
        fail(error, "empty contour");
        return {};
    }
    BRep_Builder builder;
    std::vector<TopoDS_Vertex> vertices(n);
    for (size_t i = 0; i < n; ++i) {
        builder.MakeVertex(vertices[i], map.at(loop.pieces[i].start), kVertexTolerance);
    }
    BRepBuilderAPI_MakeWire wire;
    for (size_t i = 0; i < n; ++i) {
        const Piece& piece = loop.pieces[i];
        const Curve* c = s.curve(piece.curve);
        if (!c) {
            fail(error, "contour refers to a missing curve");
            return {};
        }
        const TopoDS_Vertex& a = vertices[i];
        const TopoDS_Vertex& b = vertices[(i + 1) % n];
        TopoDS_Edge edge;
        if (c->type == CurveType::Line) {
            BRepBuilderAPI_MakeEdge make(a, b);
            if (!make.IsDone()) {
                fail(error, "could not make an edge for " + s.name(c->id));
                return {};
            }
            edge = make.Edge();
        } else {
            const bool ccw = piece.t1 > piece.t0;
            const gp_Pnt center = map.at(sketch::centerOf(s, *c));
            const double r = sketch::radiusOf(s, *c);
            const gp_Ax2 axis(center, ccw ? map.normal() : map.normal().Reversed(), map.xDir());
            const Handle(Geom_Circle) circle = new Geom_Circle(axis, r);
            const gp_Circ g = circle->Circ();
            const double u1 = ElCLib::Parameter(g, BRep_Tool::Pnt(a));
            double u2 = ElCLib::Parameter(g, BRep_Tool::Pnt(b));
            const double sweep = std::abs(piece.t1 - piece.t0);
            if (n == 1 || sweep > kTwoPi - 1e-9) {
                u2 = u1 + kTwoPi;
            } else {
                while (u2 <= u1 + 1e-12) {
                    u2 += kTwoPi;
                }
            }
            BRepBuilderAPI_MakeEdge make(circle, a, b, u1, u2);
            if (!make.IsDone()) {
                fail(error, "could not make an edge for " + s.name(c->id));
                return {};
            }
            edge = make.Edge();
        }
        edges.emplace_back(edge, c->id);
        wire.Add(edge);
        if (!wire.IsDone()) {
            fail(error, "contour edges do not join near " + s.name(c->id));
            return {};
        }
    }
    return wire.Wire();
}

double edgeMidDistance(const TopoDS_Edge& a, const TopoDS_Edge& b) {
    BRepAdaptor_Curve ca(a), cb(b);
    const gp_Pnt ma = ca.Value(0.5 * (ca.FirstParameter() + ca.LastParameter()));
    const gp_Pnt mb = cb.Value(0.5 * (cb.FirstParameter() + cb.LastParameter()));
    return ma.Distance(mb);
}

// The face's own edges with their sketch curves: edges kept as made match exactly, edges a fix
// replaced are matched by position.
void mapFaceEdges(const TopoDS_Face& face, const std::vector<std::pair<TopoDS_Edge, sketch::Id>>& made,
                  std::vector<std::pair<TopoDS_Edge, sketch::Id>>& out) {
    for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge e = TopoDS::Edge(it.Current());
        sketch::Id curve = 0;
        double best = std::numeric_limits<double>::infinity();
        for (const auto& [m, id] : made) {
            if (m.IsSame(e)) {
                curve = id;
                break;
            }
            const double d = edgeMidDistance(m, e);
            if (d < best && d < 1e-4) {
                best = d;
                curve = id;
            }
        }
        if (curve != 0 &&
            std::none_of(out.begin(), out.end(), [&](const auto& x) { return x.first.IsSame(e); })) {
            out.emplace_back(e, curve);
        }
    }
}

TopoDS_Shape singleSolidOrSelf(const TopoDS_Shape& shape) {
    TopoDS_Shape solid;
    int count = 0;
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next()) {
        solid = it.Current();
        ++count;
    }
    return count == 1 ? solid : shape;
}

} // namespace

std::vector<int> defaultRegions(const sketch::Contours& contours) {
    std::vector<int> out;
    const auto& regions = contours.regions;
    for (size_t i = 0; i < regions.size(); ++i) {
        const std::optional<P2> p = interiorPoint(regions[i]);
        if (!p) {
            continue;
        }
        int depth = 0; // how many islands (holes of other regions) the region sits in
        for (size_t j = 0; j < regions.size(); ++j) {
            if (j == i) {
                continue;
            }
            for (const Loop& hole : regions[j].holes) {
                if (insidePolygon(hole.polygon, *p)) {
                    ++depth;
                }
            }
        }
        if (depth % 2 == 0) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

Profile buildProfile(const sketch::Sketch& s, const gp_Ax3& plane, const sketch::Contours& contours,
                     const std::vector<int>& regions, std::string* error) {
    Profile profile;
    if (contours.regions.empty()) {
        fail(error, "the sketch has no closed contour");
        return profile;
    }
    const PlaneMap map(plane);
    for (int index : regions) {
        if (index < 0 || index >= static_cast<int>(contours.regions.size())) {
            continue;
        }
        const Region& region = contours.regions[static_cast<size_t>(index)];
        std::vector<std::pair<TopoDS_Edge, sketch::Id>> made;
        const TopoDS_Wire outer = wireOf(s, region.outer, map, made, error);
        if (outer.IsNull()) {
            return {};
        }
        BRepBuilderAPI_MakeFace maker(gp_Pln(plane), outer, true);
        for (const Loop& hole : region.holes) {
            const TopoDS_Wire w = wireOf(s, hole, map, made, error);
            if (w.IsNull()) {
                return {};
            }
            maker.Add(w);
        }
        if (!maker.IsDone()) {
            fail(error, "could not make a face from the contour");
            return {};
        }
        TopoDS_Face face = maker.Face();
        if (!BRepCheck_Analyzer(face).IsValid()) {
            ShapeFix_Face fix(face);
            fix.Perform();
            face = fix.Face();
            if (!BRepCheck_Analyzer(face).IsValid()) {
                fail(error, "the contour does not make a valid face (does it cross itself?)");
                return {};
            }
        }
        profile.faces.push_back(face);
        mapFaceEdges(face, made, profile.edges);
    }
    if (profile.faces.empty()) {
        fail(error, "no region selected");
    }
    return profile;
}

NamedShape extrude(const Profile& profile, const gp_Dir& direction, double length, const std::string& feature,
                   const std::string& sketchKey, std::string* error) {
    if (profile.empty()) {
        fail(error, "nothing to extrude");
        return {};
    }
    if (std::abs(length) < 1e-7) {
        fail(error, "the depth is zero");
        return {};
    }
    NamedShape result;
    for (const TopoDS_Face& face : profile.faces) {
        BRepPrimAPI_MakePrism prism(face, gp_Vec(direction) * length, true);
        prism.Build();
        if (!prism.IsDone()) {
            fail(error, "the extrusion failed");
            return {};
        }
        NamedShape part;
        part.shape = prism.Shape();
        for (const auto& [edge, curve] : profile.edges) {
            const auto& generated = prism.Generated(edge);
            for (auto it = generated.cbegin(); it != generated.cend(); ++it) {
                if (it->ShapeType() == TopAbs_FACE) {
                    part.add(TopoDS::Face(*it), feature + "/Side(" + sketchKey + ":" + std::to_string(curve) + ")");
                }
            }
        }
        part.add(TopoDS::Face(prism.FirstShape()), feature + "/StartCap");
        part.add(TopoDS::Face(prism.LastShape()), feature + "/EndCap");
        if (result.isNull()) {
            result = std::move(part);
            continue;
        }
        result = fuse(result, part, error);
        if (result.isNull()) {
            return {};
        }
    }
    return result;
}

NamedShape fuse(const NamedShape& a, const NamedShape& b, std::string* error) {
    BRepAlgoAPI_Fuse op(a.shape, b.shape);
    if (!op.IsDone() || op.HasErrors()) {
        fail(error, "the union failed");
        return {};
    }
    op.SimplifyResult(true, true);
    NamedShape r;
    r.shape = singleSolidOrSelf(op.Shape());
    carryNames(op, a, r);
    carryNames(op, b, r);
    if (!BRepCheck_Analyzer(r.shape).IsValid()) {
        fail(error, "the union made an invalid body");
        return {};
    }
    return r;
}

NamedShape cut(const NamedShape& body, const NamedShape& tool, std::string* error) {
    BRepAlgoAPI_Cut op(body.shape, tool.shape);
    if (!op.IsDone() || op.HasErrors()) {
        fail(error, "the cut failed");
        return {};
    }
    op.SimplifyResult(true, true);
    NamedShape r;
    r.shape = singleSolidOrSelf(op.Shape());
    carryNames(op, body, r);
    carryNames(op, tool, r);
    if (!BRepCheck_Analyzer(r.shape).IsValid()) {
        fail(error, "the cut made an invalid body");
        return {};
    }
    return r;
}

bool touches(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    Bnd_Box ba, bb;
    BRepBndLib::Add(a, ba);
    BRepBndLib::Add(b, bb);
    ba.Enlarge(1e-6);
    if (ba.IsOut(bb)) {
        return false;
    }
    BRepExtrema_DistShapeShape dist(a, b);
    return dist.IsDone() && dist.Value() < 1e-6;
}

bool hasSolid(const TopoDS_Shape& shape) {
    return !shape.IsNull() && TopExp_Explorer(shape, TopAbs_SOLID).More();
}

} // namespace bl::kernel
