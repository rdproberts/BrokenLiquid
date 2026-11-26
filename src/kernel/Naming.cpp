#include "kernel/Naming.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <tuple>
#include <cmath>
#include <limits>

namespace bl::kernel {
namespace {

using FaceMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
using ShapeList = NCollection_List<TopoDS_Shape>;
using AncestorMap = NCollection_IndexedDataMap<TopoDS_Shape, ShapeList, TopTools_ShapeMapHasher>;

// Largest mismatch still accepted as "the same face" when its name is gone.
constexpr double kRepairLimit = 0.02;
// Edges: tighter (half a percent of the model size). A chamfer or fillet next to an edge moves its
// neighbours by about its size, and those must not pass for the edge that went away.
constexpr double kEdgeRepairLimit = 0.005;

FaceMap facesOf(const TopoDS_Shape& shape) {
    FaceMap map;
    if (!shape.IsNull()) {
        TopExp::MapShapes(shape, TopAbs_FACE, map);
    }
    return map;
}

FaceMap edgesOf(const TopoDS_Shape& shape) {
    FaceMap map;
    if (!shape.IsNull()) {
        TopExp::MapShapes(shape, TopAbs_EDGE, map);
    }
    return map;
}

std::vector<TopoDS_Face> distinctFaces(const ShapeList& list) {
    std::vector<TopoDS_Face> out;
    for (auto it = list.cbegin(); it != list.cend(); ++it) {
        if (it->ShapeType() == TopAbs_FACE &&
            std::none_of(out.begin(), out.end(), [&](const TopoDS_Face& f) { return f.IsSame(*it); })) {
            out.push_back(TopoDS::Face(*it));
        }
    }
    return out;
}

// The two names an edge between `a` and `b` is known by (first name of each face, sorted).
std::pair<std::string, std::string> pairName(const NamedShape& shape, const TopoDS_Face& a, const TopoDS_Face& b) {
    const std::vector<std::string> na = shape.namesOf(a);
    const std::vector<std::string> nb = shape.namesOf(b);
    if (na.empty() || nb.empty()) {
        return {};
    }
    std::string x = na.front(), y = nb.front();
    if (y < x) {
        std::swap(x, y);
    }
    return {x, y};
}

bool hasName(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

double sizeOf(const std::vector<const NamedShape*>& shapes) {
    Bnd_Box box;
    for (const NamedShape* s : shapes) {
        if (s && !s->shape.IsNull()) {
            BRepBndLib::Add(s->shape, box);
        }
    }
    if (box.IsVoid()) {
        return 1.0;
    }
    return std::max(1e-3, std::sqrt(box.SquareExtent()));
}

} // namespace

std::vector<TopoDS_Face> NamedShape::find(const std::string& name) const {
    std::vector<TopoDS_Face> out;
    for (const auto& [face, n] : faces) {
        if (n == name && std::none_of(out.begin(), out.end(), [&](const TopoDS_Face& f) { return f.IsSame(face); })) {
            out.push_back(face);
        }
    }
    return out;
}

std::vector<std::string> NamedShape::namesOf(const TopoDS_Shape& face) const {
    std::vector<std::string> out;
    for (const auto& [f, n] : faces) {
        if (f.IsSame(face) && std::find(out.begin(), out.end(), n) == out.end()) {
            out.push_back(n);
        }
    }
    return out;
}

NamedShape namedImport(const TopoDS_Shape& shape, const std::string& prefix) {
    NamedShape named;
    named.shape = shape;
    const FaceMap map = facesOf(shape);
    for (int i = 1; i <= map.Extent(); ++i) {
        named.add(TopoDS::Face(map(i)), prefix + "/Face" + std::to_string(i));
    }
    return named;
}

void carryNames(BRepBuilderAPI_MakeShape& op, const NamedShape& input, NamedShape& result) {
    const FaceMap inResult = facesOf(result.shape);
    for (const auto& [face, name] : input.faces) {
        if (op.IsDeleted(face)) {
            continue;
        }
        const auto& modified = op.Modified(face);
        if (modified.IsEmpty()) {
            if (const int i = inResult.FindIndex(face); i > 0) {
                result.add(TopoDS::Face(inResult(i)), name);
            }
            continue;
        }
        for (auto it = modified.cbegin(); it != modified.cend(); ++it) {
            if (it->ShapeType() == TopAbs_FACE) {
                if (const int i = inResult.FindIndex(*it); i > 0) {
                    result.add(TopoDS::Face(inResult(i)), name);
                }
            }
        }
    }
}

void nameGenerated(BRepBuilderAPI_MakeShape& op, const NamedShape& input, NamedShape& result,
                   const std::string& feature, const std::string& role) {
    const FaceMap inResult = facesOf(result.shape);
    auto nameAll = [&](const ShapeList& generated, const std::string& name) {
        for (auto it = generated.cbegin(); it != generated.cend(); ++it) {
            if (it->ShapeType() == TopAbs_FACE) {
                if (const int i = inResult.FindIndex(*it); i > 0) {
                    result.add(TopoDS::Face(inResult(i)), name);
                }
            }
        }
    };
    const FaceMap edges = edgesOf(input.shape);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge edge = TopoDS::Edge(edges(i));
        const ShapeList& generated = op.Generated(edge);
        if (!generated.IsEmpty()) {
            nameAll(generated, feature + "/" + role + "(" + edgeName(input, edge) + ")");
        }
    }
    AncestorMap vertexFaces;
    TopExp::MapShapesAndAncestors(input.shape, TopAbs_VERTEX, TopAbs_FACE, vertexFaces);
    for (int i = 1; i <= vertexFaces.Extent(); ++i) {
        const ShapeList& generated = op.Generated(vertexFaces.FindKey(i));
        if (generated.IsEmpty()) {
            continue;
        }
        std::vector<std::string> names;
        for (const TopoDS_Face& f : distinctFaces(vertexFaces(i))) {
            const std::vector<std::string> n = input.namesOf(f);
            names.push_back(n.empty() ? std::string("?") : n.front());
        }
        std::sort(names.begin(), names.end());
        std::string joined;
        for (const std::string& n : names) {
            joined += (joined.empty() ? "" : "^") + n;
        }
        nameAll(generated, feature + "/Corner(" + joined + ")");
    }
}

std::vector<TopoDS_Face> adjacentFaces(const TopoDS_Shape& shape, const TopoDS_Edge& edge) {
    AncestorMap map;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, map);
    const int i = map.FindIndex(edge);
    return i > 0 ? distinctFaces(map(i)) : std::vector<TopoDS_Face>{};
}

std::string edgeName(const NamedShape& shape, const TopoDS_Edge& edge) {
    const std::vector<TopoDS_Face> faces = adjacentFaces(shape.shape, edge);
    if (faces.size() != 2) {
        return {};
    }
    const auto [a, b] = pairName(shape, faces[0], faces[1]);
    return a.empty() ? std::string() : a + "^" + b;
}

// --- Signatures and references -----------------------------------------------------------------------

bool FaceSignature::operator==(const FaceSignature& o) const {
    return surface == o.surface && planar == o.planar && centroid.IsEqual(o.centroid, 0.0) &&
           normal.IsEqual(o.normal, 0.0) && area == o.area;
}

FaceSignature signatureOf(const TopoDS_Face& face) {
    FaceSignature s;
    if (face.IsNull()) {
        return s;
    }
    BRepAdaptor_Surface surface(face);
    s.surface = static_cast<int>(surface.GetType());
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    s.area = props.Mass();
    s.centroid = props.CentreOfMass();
    if (const auto plane = planeOfFace(face)) {
        s.planar = true;
        s.normal = plane->Axis().Direction();
    }
    return s;
}

double mismatch(const FaceSignature& a, const FaceSignature& b, double size) {
    if (a.surface != b.surface || a.planar != b.planar) {
        return std::numeric_limits<double>::infinity();
    }
    double m = a.centroid.Distance(b.centroid) / std::max(size, 1e-9);
    m += std::abs(a.area - b.area) / std::max({a.area, b.area, 1e-12});
    if (a.planar) {
        m += 1.0 - a.normal.Dot(b.normal); // 0 when parallel, 2 when opposite
    }
    return m;
}

const char* refStatusName(RefStatus status) {
    switch (status) {
    case RefStatus::Exact: return "found";
    case RefStatus::Ambiguous: return "ambiguous";
    case RefStatus::Repaired: return "repaired";
    case RefStatus::Missing: return "missing";
    }
    return "";
}

FaceRef referenceTo(const NamedShape& shape, const TopoDS_Face& face) {
    FaceRef ref;
    const std::vector<std::string> names = shape.namesOf(face);
    if (!names.empty()) {
        ref.name = names.front();
    }
    // The signature of the face as it sits in the shape (orientation decides the plane normal).
    const FaceMap map = facesOf(shape.shape);
    const int i = map.FindIndex(face);
    ref.signature = signatureOf(i > 0 ? TopoDS::Face(map(i)) : face);
    return ref;
}

Resolved resolve(const std::vector<const NamedShape*>& shapes, const FaceRef& ref) {
    Resolved r;
    const double size = sizeOf(shapes);
    auto closest = [&](const std::vector<TopoDS_Face>& faces) {
        TopoDS_Face best;
        double bestScore = std::numeric_limits<double>::infinity();
        for (const TopoDS_Face& f : faces) {
            const double score = mismatch(ref.signature, signatureOf(f), size);
            if (score < bestScore) {
                bestScore = score;
                best = f;
            }
        }
        return std::make_pair(best, bestScore);
    };
    if (!ref.name.empty()) {
        std::vector<TopoDS_Face> named;
        for (const NamedShape* s : shapes) {
            for (const TopoDS_Face& f : s->find(ref.name)) {
                named.push_back(f);
            }
        }
        if (named.size() == 1) {
            r.face = named.front();
            r.status = RefStatus::Exact;
            return r;
        }
        if (named.size() > 1) {
            r.face = closest(named).first;
            r.status = RefStatus::Ambiguous;
            return r;
        }
    }
    // The name is gone: accept only a face that looks the same.
    std::vector<TopoDS_Face> all;
    for (const NamedShape* s : shapes) {
        const FaceMap map = facesOf(s->shape);
        for (int i = 1; i <= map.Extent(); ++i) {
            all.push_back(TopoDS::Face(map(i)));
        }
    }
    const auto [face, score] = closest(all);
    if (!face.IsNull() && score < kRepairLimit) {
        r.face = face;
        r.status = RefStatus::Repaired;
    }
    return r;
}

bool EdgeSignature::operator==(const EdgeSignature& o) const {
    return curve == o.curve && straight == o.straight && middle.IsEqual(o.middle, 0.0) &&
           direction.IsEqual(o.direction, 0.0) && length == o.length;
}

EdgeSignature edgeSignatureOf(const TopoDS_Edge& edge) {
    EdgeSignature s;
    if (edge.IsNull()) {
        return s;
    }
    BRepAdaptor_Curve curve(edge);
    s.curve = static_cast<int>(curve.GetType());
    const double t0 = curve.FirstParameter(), t1 = curve.LastParameter();
    s.middle = curve.Value(0.5 * (t0 + t1));
    s.length = GCPnts_AbscissaPoint::Length(curve);
    if (curve.GetType() == GeomAbs_Line) {
        s.straight = true;
        s.direction = curve.Line().Direction();
    }
    return s;
}

double mismatch(const EdgeSignature& a, const EdgeSignature& b, double size) {
    if (a.curve != b.curve || a.straight != b.straight) {
        return std::numeric_limits<double>::infinity();
    }
    double m = a.middle.Distance(b.middle) / std::max(size, 1e-9);
    m += std::abs(a.length - b.length) / std::max({a.length, b.length, 1e-12});
    if (a.straight) {
        m += 1.0 - std::abs(a.direction.Dot(b.direction));
    }
    return m;
}

EdgeRef edgeReferenceTo(const NamedShape& shape, const TopoDS_Edge& edge) {
    EdgeRef ref;
    const FaceMap map = edgesOf(shape.shape);
    const int i = map.FindIndex(edge);
    const TopoDS_Edge own = i > 0 ? TopoDS::Edge(map(i)) : edge;
    const std::vector<TopoDS_Face> faces = adjacentFaces(shape.shape, own);
    if (faces.size() == 2) {
        std::tie(ref.face1, ref.face2) = pairName(shape, faces[0], faces[1]);
    }
    ref.signature = edgeSignatureOf(own);
    return ref;
}

ResolvedEdge resolveEdge(const std::vector<const NamedShape*>& shapes, const EdgeRef& ref) {
    ResolvedEdge r;
    const double size = sizeOf(shapes);
    struct Candidate {
        TopoDS_Edge edge;
        int shape;
    };
    auto closest = [&](const std::vector<Candidate>& candidates) {
        Candidate best{{}, -1};
        double bestScore = std::numeric_limits<double>::infinity();
        for (const Candidate& c : candidates) {
            const double score = mismatch(ref.signature, edgeSignatureOf(c.edge), size);
            if (score < bestScore) {
                bestScore = score;
                best = c;
            }
        }
        return std::make_pair(best, bestScore);
    };
    std::vector<Candidate> named, all;
    for (size_t s = 0; s < shapes.size(); ++s) {
        const NamedShape* shape = shapes[s];
        if (!shape || shape->isNull()) {
            continue;
        }
        AncestorMap map;
        TopExp::MapShapesAndAncestors(shape->shape, TopAbs_EDGE, TopAbs_FACE, map);
        for (int i = 1; i <= map.Extent(); ++i) {
            const TopoDS_Edge edge = TopoDS::Edge(map.FindKey(i));
            if (BRep_Tool::Degenerated(edge)) {
                continue;
            }
            all.push_back({edge, static_cast<int>(s)});
            if (ref.face1.empty()) {
                continue;
            }
            const std::vector<TopoDS_Face> faces = distinctFaces(map(i));
            if (faces.size() != 2) {
                continue;
            }
            const std::vector<std::string> a = shape->namesOf(faces[0]);
            const std::vector<std::string> b = shape->namesOf(faces[1]);
            if ((hasName(a, ref.face1) && hasName(b, ref.face2)) || (hasName(a, ref.face2) && hasName(b, ref.face1))) {
                named.push_back({edge, static_cast<int>(s)});
            }
        }
    }
    if (named.size() == 1) {
        r.edge = named.front().edge;
        r.shape = named.front().shape;
        r.status = RefStatus::Exact;
        return r;
    }
    if (named.size() > 1) {
        const Candidate best = closest(named).first;
        r.edge = best.edge;
        r.shape = best.shape;
        r.status = RefStatus::Ambiguous;
        return r;
    }
    const auto [best, score] = closest(all);
    if (best.shape >= 0 && score < kEdgeRepairLimit) {
        r.edge = best.edge;
        r.shape = best.shape;
        r.status = RefStatus::Repaired;
    }
    return r;
}

// --- Planes -------------------------------------------------------------------------------------------

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
    return plane;
}

gp_Ax3 sketchAxesOnFace(const gp_Pln& face) {
    const gp_Dir n = face.Axis().Direction();
    const gp_Vec toOrigin(face.Location(), gp_Pnt(0, 0, 0));
    const gp_Pnt origin = gp_Pnt(0, 0, 0).Translated(-gp_Vec(n) * toOrigin.Dot(gp_Vec(n)));
    gp_Vec up = std::abs(n.Y()) < 0.9 ? gp_Vec(0, 1, 0) : gp_Vec(0, 0, n.Y() > 0 ? -1 : 1);
    up -= gp_Vec(n) * up.Dot(gp_Vec(n));
    const gp_Dir x = gp_Dir(up.Crossed(gp_Vec(n)));
    return gp_Ax3(origin, n, x);
}

gp_Ax3 followFace(const gp_Ax3& old, const gp_Pln& face) {
    const gp_Dir n = face.Axis().Direction();
    if (!n.IsEqual(old.Direction(), 1e-9)) {
        return sketchAxesOnFace(face);
    }
    const gp_Vec shift = gp_Vec(n) * gp_Vec(old.Location(), face.Location()).Dot(gp_Vec(n));
    gp_Ax3 moved = old;
    moved.SetLocation(old.Location().Translated(shift));
    return moved;
}

} // namespace bl::kernel
