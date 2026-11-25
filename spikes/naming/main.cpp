// Spike: persistent ("topological") naming. Answers PLAN.md §6.5 (the #1 architectural risk):
// "When an upstream sketch or feature changes, can a downstream feature (here a fillet) still
// find the same edge?"
//
// Model: Sketch1 (rectangle, stable segment ids s1..s4) -> Extrude1 -> optional Cut-Extrude hole
// -> Fillet1 on the vertical edge where the side faces of s2 and s3 meet.
//
// Two ways to remember that edge:
//   index naming       "edge #k of the result"                           (what naive code does)
//   generative naming  "edge shared by Extrude1/Side(s2) and Extrude1/Side(s3)",
//                      resolved through the modelling history (Generated / Modified).
//
// Variants rebuild the model after an edit and check which edge each method picks:
//   v1 original, v2 width 40->60 + hole added upstream, v3 = v2 + corner chamfer added to the
//   sketch at the opposite corner, v4 = chamfer at the filleted corner itself (the reference is
//   really gone: the right answer is "missing reference", not a silent wrong pick).
//
// Output (BL_SPIKE_OUT): naming_report.txt. Exit 0 when generative naming is right in v1-v3 and
// reports v4 as missing.

#include "kernel/ShapeInfo.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

enum class Chamfer { None, AtOrigin, AtTarget };

struct Variant {
    const char* name;
    double width, depth, height;
    bool hole;
    Chamfer chamfer;
};

// A face name: role of the face in a feature + the sketch entity it came from.
struct FaceName {
    std::string feature; // "Extrude1"
    std::string role;    // "Side" (side face swept from a sketch segment), "Start", "End"
    std::string source;  // sketch segment id for Side
    std::string str() const { return feature + "/" + role + (source.empty() ? "" : "(" + source + ")"); }
};

// An edge name: the edge shared by two named faces.
struct EdgeName {
    FaceName a, b;
    std::string str() const { return a.str() + " ^ " + b.str(); }
};

std::vector<TopoDS_Shape> toVector(const NCollection_List<TopoDS_Shape>& list) {
    std::vector<TopoDS_Shape> v;
    for (NCollection_List<TopoDS_Shape>::Iterator it(list); it.More(); it.Next()) {
        v.push_back(it.Value());
    }
    return v;
}

// One rebuild of the feature tree, keeping each operation's history.
class Build {
public:
    explicit Build(const Variant& v) : m_variant(v) {
        // Sketch1 on XY. Segment ids are owned by the sketch and survive edits.
        const double w = v.width, d = v.depth, c = 6.0;
        std::vector<std::pair<std::string, std::pair<gp_Pnt, gp_Pnt>>> segs;
        const gp_Pnt p1(0, 0, 0), p2(w, 0, 0), p3(w, d, 0), p4(0, d, 0);
        switch (v.chamfer) {
        case Chamfer::None:
            segs = {{"s1", {p1, p2}}, {"s2", {p2, p3}}, {"s3", {p3, p4}}, {"s4", {p4, p1}}};
            break;
        case Chamfer::AtOrigin: {
            const gp_Pnt q1(c, 0, 0), q5(0, c, 0);
            segs = {{"s1", {q1, p2}}, {"s2", {p2, p3}}, {"s3", {p3, p4}}, {"s4", {p4, q5}}, {"s5", {q5, q1}}};
            break;
        }
        case Chamfer::AtTarget: {
            const gp_Pnt q3(w, d - c, 0), q4(w - c, d, 0);
            segs = {{"s1", {p1, p2}}, {"s2", {p2, q3}}, {"s6", {q3, q4}}, {"s3", {q4, p4}}, {"s4", {p4, p1}}};
            break;
        }
        }
        // Shared vertices so the wire keeps our edges untouched.
        std::vector<TopoDS_Vertex> verts;
        for (const auto& s : segs) {
            verts.push_back(BRepBuilderAPI_MakeVertex(s.second.first).Vertex());
        }
        BRepBuilderAPI_MakeWire wire;
        for (size_t i = 0; i < segs.size(); ++i) {
            const TopoDS_Edge e = BRepBuilderAPI_MakeEdge(verts[i], verts[(i + 1) % verts.size()]).Edge();
            m_sketch[segs[i].first] = e;
            wire.Add(e);
        }
        const TopoDS_Face profile = BRepBuilderAPI_MakeFace(wire.Wire(), true).Face();

        // Extrude1
        m_prism = std::make_unique<BRepPrimAPI_MakePrism>(profile, gp_Vec(0, 0, v.height));
        m_prism->Build();
        m_shape = m_prism->Shape();

        // Cut-Extrude1 (through all), upstream of the fillet.
        if (v.hole) {
            const TopoDS_Shape tool =
                BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(w / 2, d / 2, -1), gp::DZ()), 5.0, v.height + 2).Shape();
            m_cut = std::make_unique<BRepAlgoAPI_Cut>(m_shape, tool);
            m_cut->Build();
            m_shape = m_cut->Shape();
        }
    }

    const TopoDS_Shape& shape() const { return m_shape; }
    const Variant& variant() const { return m_variant; }

    // Faces of the final shape that carry `name` (a face can be split or removed downstream).
    std::vector<TopoDS_Shape> resolve(const FaceName& name) const {
        std::vector<TopoDS_Shape> faces;
        if (name.feature != "Extrude1") {
            return faces;
        }
        if (name.role == "Start") {
            faces.push_back(m_prism->FirstShape());
        } else if (name.role == "End") {
            faces.push_back(m_prism->LastShape());
        } else if (name.role == "Side") {
            const auto it = m_sketch.find(name.source);
            if (it == m_sketch.end()) {
                return faces; // sketch segment deleted
            }
            faces = toVector(m_prism->Generated(it->second));
        }
        if (m_cut) {
            std::vector<TopoDS_Shape> after;
            for (const TopoDS_Shape& f : faces) {
                if (m_cut->IsDeleted(f)) {
                    continue;
                }
                const auto mod = toVector(m_cut->Modified(f));
                if (mod.empty()) {
                    after.push_back(f);
                } else {
                    after.insert(after.end(), mod.begin(), mod.end());
                }
            }
            faces = after;
        }
        return faces;
    }

    // Edges of the final shape shared by a face named `a` and a face named `b`.
    std::vector<TopoDS_Edge> resolve(const EdgeName& name) const {
        NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> edgeFaces;
        TopExp::MapShapesAndAncestors(m_shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
        const auto fa = resolve(name.a), fb = resolve(name.b);
        auto contains = [](const std::vector<TopoDS_Shape>& set, const TopoDS_Shape& s) {
            for (const auto& x : set) {
                if (x.IsSame(s)) {
                    return true;
                }
            }
            return false;
        };
        std::vector<TopoDS_Edge> edges;
        for (int i = 1; i <= edgeFaces.Extent(); ++i) {
            bool inA = false, inB = false;
            for (NCollection_List<TopoDS_Shape>::Iterator it(edgeFaces(i)); it.More(); it.Next()) {
                inA = inA || contains(fa, it.Value());
                inB = inB || contains(fb, it.Value());
            }
            if (inA && inB) {
                edges.push_back(TopoDS::Edge(edgeFaces.FindKey(i)));
            }
        }
        return edges;
    }

    // Ground truth for the check: the vertical edge at the (width, depth) corner.
    bool isTargetCorner(const TopoDS_Edge& e) const {
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(e, v1, v2);
        if (v1.IsNull() || v2.IsNull()) {
            return false;
        }
        const gp_Pnt a = BRep_Tool::Pnt(v1), b = BRep_Tool::Pnt(v2);
        auto at = [&](const gp_Pnt& p) {
            return std::abs(p.X() - m_variant.width) < 1e-6 && std::abs(p.Y() - m_variant.depth) < 1e-6;
        };
        return at(a) && at(b) && std::abs(std::abs(a.Z() - b.Z()) - m_variant.height) < 1e-6;
    }

private:
    Variant m_variant;
    std::map<std::string, TopoDS_Edge> m_sketch;
    std::unique_ptr<BRepPrimAPI_MakePrism> m_prism;
    std::unique_ptr<BRepAlgoAPI_Cut> m_cut;
    TopoDS_Shape m_shape;
};

std::string describe(const TopoDS_Edge& e) {
    TopoDS_Vertex v1, v2;
    TopExp::Vertices(e, v1, v2);
    if (v1.IsNull() || v2.IsNull()) {
        return "(closed edge)";
    }
    const gp_Pnt a = BRep_Tool::Pnt(v1), b = BRep_Tool::Pnt(v2);
    char buf[160];
    std::snprintf(buf, sizeof buf, "(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)", a.X(), a.Y(), a.Z(), b.X(), b.Y(), b.Z());
    return buf;
}

} // namespace

int main() {
    const Variant variants[] = {
        {"v1 original 40x30x10", 40, 30, 10, false, Chamfer::None},
        {"v2 width 60 + hole upstream", 60, 30, 10, true, Chamfer::None},
        {"v3 v2 + chamfer at opposite corner", 60, 30, 10, true, Chamfer::AtOrigin},
        {"v4 chamfer at the filleted corner", 60, 30, 10, true, Chamfer::AtTarget},
    };
    const EdgeName target{{"Extrude1", "Side", "s2"}, {"Extrude1", "Side", "s3"}};
    const double radius = 3.0;

    std::ostringstream report;
    report << "BrokenLiquid spike: persistent naming (generative names vs. edge index)\n"
           << "fillet reference: " << target.str() << "\n\n";

    int recordedIndex = 0;
    bool pass = true;
    for (const Variant& v : variants) {
        const Build build(v);
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edgeMap;
        TopExp::MapShapes(build.shape(), TopAbs_EDGE, edgeMap);
        const auto found = build.resolve(target);
        const bool expectMissing = v.chamfer == Chamfer::AtTarget;

        report << v.name << ": " << edgeMap.Extent() << " edges\n";

        // Generative naming.
        bool genOk = false;
        if (found.size() == 1) {
            const TopoDS_Edge& e = found.front();
            genOk = !expectMissing && build.isTargetCorner(e);
            if (recordedIndex == 0) {
                recordedIndex = edgeMap.FindIndex(e); // what index naming would store at v1
            }
            BRepFilletAPI_MakeFillet fillet(build.shape());
            fillet.Add(radius, e);
            fillet.Build();
            std::string filletInfo = "fillet failed";
            if (fillet.IsDone()) {
                const double before = bl::kernel::computeStats(build.shape()).volume;
                const double after = bl::kernel::computeStats(fillet.Shape()).volume;
                const double expected = (radius * radius - M_PI * radius * radius / 4) * v.height;
                char buf[200];
                std::snprintf(buf, sizeof buf, "Fillet1 ok, %d face(s) generated, removed %.3f mm^3 (expected %.3f)",
                              static_cast<int>(toVector(fillet.Generated(e)).size()), before - after, expected);
                filletInfo = buf;
                genOk = genOk && std::abs((before - after) - expected) < 1e-3 * expected + 1e-6;
            }
            report << "  generative: resolved 1 edge " << describe(e) << " -> " << (genOk ? "correct" : "WRONG") << "; "
                   << filletInfo << "\n";
        } else if (found.empty()) {
            genOk = expectMissing;
            report << "  generative: reference missing -> " << (expectMissing ? "correct (reported to user, fillet shown as error)" : "WRONG")
                   << "\n";
        } else {
            report << "  generative: ambiguous (" << found.size() << " edges) -> WRONG\n";
        }

        // Index naming: reuse the index recorded at v1.
        if (recordedIndex > 0 && recordedIndex <= edgeMap.Extent()) {
            const TopoDS_Edge e = TopoDS::Edge(edgeMap(recordedIndex));
            const bool right = !expectMissing && build.isTargetCorner(e);
            report << "  index #" << recordedIndex << ":   picks " << describe(e) << " -> "
                   << (right ? "correct" : (expectMissing ? "WRONG (silently filleted another edge)" : "WRONG")) << "\n";
        } else {
            report << "  index #" << recordedIndex << ":   out of range\n";
        }
        pass = pass && genOk;
    }
    report << "\nresult: " << (pass ? "PASS" : "FAIL") << " (generative naming correct in every variant)\n";

    const std::filesystem::path out = BL_SPIKE_OUT;
    std::filesystem::create_directories(out);
    std::ofstream(out / "naming_report.txt") << report.str();
    std::fputs(report.str().c_str(), stdout);
    return pass ? 0 : 1;
}
