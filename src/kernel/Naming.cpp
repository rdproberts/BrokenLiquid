#include "kernel/Naming.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bl::kernel {
namespace {

using FaceMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

// Largest mismatch still accepted as "the same face" when its name is gone.
constexpr double kRepairLimit = 0.02;

FaceMap facesOf(const TopoDS_Shape& shape) {
    FaceMap map;
    if (!shape.IsNull()) {
        TopExp::MapShapes(shape, TopAbs_FACE, map);
    }
    return map;
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
