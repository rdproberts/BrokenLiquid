#pragma once

// Persistent naming (ADR 0003, PLAN.md 6.5). Every face a feature produces gets a name from how it
// was made ("<feature>/Side(<sketch>:<curve>)", "<feature>/EndCap"), and the names travel through
// later operations with OCCT's modelling history (Modified / IsDeleted). References to faces store
// a name plus a small geometric signature; resolving one after a rebuild either finds the face, or
// says that it had to guess (ambiguous / repaired), or that the face is gone. Never a silent guess.

#include <BRepBuilderAPI_MakeShape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bl::kernel {

// A shape with names for its faces. A face can carry several names (two coplanar faces merged by a
// boolean) and a name can sit on several faces (a face split in two).
struct NamedShape {
    TopoDS_Shape shape;
    std::vector<std::pair<TopoDS_Face, std::string>> faces;

    bool isNull() const { return shape.IsNull(); }
    std::vector<TopoDS_Face> find(const std::string& name) const;
    std::vector<std::string> namesOf(const TopoDS_Shape& face) const;
    void add(const TopoDS_Face& face, const std::string& name) { faces.emplace_back(face, name); }
};

// Names faces of a shape that has no history (an imported body): "<prefix>/Face<i>" in the
// shape's own face order, which never changes because the imported shape never changes.
NamedShape namedImport(const TopoDS_Shape& shape, const std::string& prefix);

// Carries the names of `input`'s faces into `result` through an operation's history: deleted faces
// lose their names, modified faces pass them to what they became. Faces of `result` only.
void carryNames(BRepBuilderAPI_MakeShape& op, const NamedShape& input, NamedShape& result);

// Names the faces an operation generated from `input`'s edges and vertices (fillets, chamfers):
// "<feature>/<role>(<edge name>)" for faces from an edge, "<feature>/Corner(<face names>)" for
// faces from a vertex where several of them meet.
void nameGenerated(BRepBuilderAPI_MakeShape& op, const NamedShape& input, NamedShape& result,
                   const std::string& feature, const std::string& role);

// --- Edges ----------------------------------------------------------------------------------------
// An edge is named by the two faces it joins: "<face name>^<face name>", the names sorted, each
// face by its first name. Seam edges (a face meeting itself) and edges of unnamed faces have no name.

// The distinct faces of `shape` that contain `edge` (two for an ordinary edge of a solid).
std::vector<TopoDS_Face> adjacentFaces(const TopoDS_Shape& shape, const TopoDS_Edge& edge);
std::string edgeName(const NamedShape& shape, const TopoDS_Edge& edge);

// --- Geometric signature and references ---------------------------------------------------------

struct FaceSignature {
    int surface = -1;     // GeomAbs_SurfaceType
    gp_Pnt centroid;
    gp_Dir normal;        // planes: outward normal; other surfaces: unused
    bool planar = false;
    double area = 0.0;

    bool operator==(const FaceSignature&) const;
};
FaceSignature signatureOf(const TopoDS_Face& face);
// 0 for identical faces, growing with the difference; `size` scales distances (the model size).
double mismatch(const FaceSignature& a, const FaceSignature& b, double size);

struct FaceRef {
    std::string name;
    FaceSignature signature;

    bool isNull() const { return name.empty() && signature.surface < 0; }
    bool operator==(const FaceRef&) const = default;
};

enum class RefStatus {
    Exact,     // the name found exactly one face
    Ambiguous, // the name sits on several faces (split): the closest by signature was taken
    Repaired,  // the name is gone: a face that looks the same was taken
    Missing,   // nothing found: the user must pick again
};
const char* refStatusName(RefStatus status);

struct Resolved {
    TopoDS_Face face;
    RefStatus status = RefStatus::Missing;
};

// A reference to `face` (a face of `shape`) for storing in a feature.
FaceRef referenceTo(const NamedShape& shape, const TopoDS_Face& face);
// Finds the referenced face among `shapes` (all bodies of the part at that point of the history).
Resolved resolve(const std::vector<const NamedShape*>& shapes, const FaceRef& ref);

struct EdgeSignature {
    int curve = -1;        // GeomAbs_CurveType
    gp_Pnt middle;         // point at the middle parameter
    gp_Dir direction;      // straight edges: the line direction (sign ignored)
    bool straight = false;
    double length = 0.0;

    bool operator==(const EdgeSignature&) const;
};
EdgeSignature edgeSignatureOf(const TopoDS_Edge& edge);
double mismatch(const EdgeSignature& a, const EdgeSignature& b, double size);

struct EdgeRef {
    std::string face1, face2; // the names of the two faces it joins, sorted (empty: no name)
    EdgeSignature signature;

    bool isNull() const { return face1.empty() && face2.empty() && signature.curve < 0; }
    std::string name() const { return face1.empty() && face2.empty() ? std::string() : face1 + "^" + face2; }
    bool operator==(const EdgeRef&) const = default;
};

struct ResolvedEdge {
    TopoDS_Edge edge;
    RefStatus status = RefStatus::Missing;
    int shape = -1; // index into the searched shapes
};

EdgeRef edgeReferenceTo(const NamedShape& shape, const TopoDS_Edge& edge);
ResolvedEdge resolveEdge(const std::vector<const NamedShape*>& shapes, const EdgeRef& ref);

// --- Planes ------------------------------------------------------------------------------------------

// The plane of a planar face with its normal pointing out of the material (null for curved faces).
std::optional<gp_Pln> planeOfFace(const TopoDS_Shape& face);
// Sketch axes on a face: origin is the model origin projected onto the face, "up" as close to +Y
// as the face allows (like the standard views).
gp_Ax3 sketchAxesOnFace(const gp_Pln& face);
// The sketch axes after the face moved: same in-plane directions when the normal did not change
// (only the origin follows the face), fresh axes otherwise.
gp_Ax3 followFace(const gp_Ax3& old, const gp_Pln& face);

} // namespace bl::kernel
