// Spike: hidden-line projection of a solid into standard drawing views, written as SVG, with a
// 2D -> 3D edge map (which model edge produced each drawn line). Answers PLAN.md §6.9 (drawings):
// "Can OCCT HLR give us clean drawing views fast enough, and can we tell which model edge each
// line came from (for dimensions that follow the model)?"
//
// Output (BL_SPIKE_OUT): sample_bracket.step, hlr_views.svg, hlr_report.txt.
// Exit code 0 when >= 95% of visible sharp lines map to a model edge in every view.

#include "kernel/ShapeInfo.h"
#include "kernel/StepIO.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <HLRBRep_PolyAlgo.hxx>
#include <HLRBRep_PolyHLRToShape.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// --- Sample part (Y-up, like the app): L-bracket with two holes and a filleted inner corner -------
TopoDS_Shape makeBracket() {
    const TopoDS_Shape base = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 60, 8, 40).Shape();
    const TopoDS_Shape upright = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 60, 48, 8).Shape();
    BRepAlgoAPI_Fuse fuse(base, upright);
    fuse.Build();
    fuse.SimplifyResult();
    TopoDS_Shape body = fuse.Shape();

    const TopoDS_Shape baseHole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30, -1, 26), gp::DY()), 6, 10).Shape();
    const TopoDS_Shape uprightHole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30, 32, -1), gp::DZ()), 5, 10).Shape();
    body = BRepAlgoAPI_Cut(body, baseHole).Shape();
    body = BRepAlgoAPI_Cut(body, uprightHole).Shape();

    // Inner corner: the straight edge along X at y = 8, z = 8.
    BRepFilletAPI_MakeFillet fillet(body);
    for (TopExp_Explorer ex(body, TopAbs_EDGE); ex.More(); ex.Next()) {
        const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
        BRepAdaptor_Curve c(e);
        if (c.GetType() != GeomAbs_Line) {
            continue;
        }
        const gp_Pnt m = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
        if (std::abs(m.Y() - 8) < 1e-6 && std::abs(m.Z() - 8) < 1e-6 && c.Line().Direction().IsParallel(gp::DX(), 1e-6)) {
            fillet.Add(5.0, e);
        }
    }
    fillet.Build();
    return fillet.IsDone() ? fillet.Shape() : body;
}

// --- Geometry helpers -------------------------------------------------------------------------
struct P2 {
    double x, y;
};
struct Box2 {
    double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = -x0;
    void add(const P2& p) {
        x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
    }
    void merge(const Box2& b) { add(P2{b.x0, b.y0}), add(P2{b.x1, b.y1}); }
    double w() const { return x1 - x0; }
    double h() const { return y1 - y0; }
    bool overlaps(const Box2& b, double tol) const {
        return !(b.x0 > x1 + tol || b.x1 < x0 - tol || b.y0 > y1 + tol || b.y1 < y0 - tol);
    }
};

double segDist(const P2& p, const P2& a, const P2& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

double polyDist(const P2& p, const std::vector<P2>& poly) {
    if (poly.size() == 1) {
        return std::hypot(p.x - poly[0].x, p.y - poly[0].y);
    }
    double best = std::numeric_limits<double>::max();
    for (size_t i = 1; i < poly.size(); ++i) {
        best = std::min(best, segDist(p, poly[i - 1], poly[i]));
    }
    return best;
}

// Points along an edge's 3D curve (lines: just the ends).
std::vector<gp_Pnt> sample3d(const TopoDS_Edge& e, double deflection) {
    std::vector<gp_Pnt> pts;
    try {
        if (BRep_Tool::Degenerated(e)) {
            return pts;
        }
        BRepAdaptor_Curve c(e);
        if (c.GetType() == GeomAbs_Line) {
            pts = {c.Value(c.FirstParameter()), c.Value(c.LastParameter())};
            return pts;
        }
        GCPnts_QuasiUniformDeflection d(c, deflection);
        if (d.IsDone()) {
            for (int i = 1; i <= d.NbPoints(); ++i) {
                pts.push_back(d.Value(i));
            }
        }
    } catch (const Standard_Failure&) {
        pts.clear();
    }
    return pts;
}

// --- HLR -----------------------------------------------------------------------------------------
enum class LineKind { Visible, Smooth, Outline, Hidden, HiddenOutline };
const char* kindName(LineKind k) {
    switch (k) {
    case LineKind::Visible: return "visible";
    case LineKind::Smooth: return "smooth";
    case LineKind::Outline: return "outline";
    case LineKind::Hidden: return "hidden";
    case LineKind::HiddenOutline: return "hidden";
    }
    return "?";
}

struct Line2 {
    LineKind kind;
    std::vector<P2> pts;
    Box2 box;
    int modelEdge = 0;  // 1-based index into the model's edge map, 0 = unmapped
    bool tieBroken = false;
};

struct ModelEdge {
    std::vector<P2> pts;   // projected
    std::vector<double> z; // depth in view coordinates (larger = nearer the viewer)
    Box2 box;
};

struct ViewResult {
    std::string name;
    std::vector<Line2> lines;
    Box2 box;
    double exactMs = 0, polyMs = 0;
    int polyLines = 0;
};

void collect(const TopoDS_Shape& compound, LineKind kind, double deflection, std::vector<Line2>& out) {
    if (compound.IsNull()) {
        return;
    }
    for (TopExp_Explorer ex(compound, TopAbs_EDGE); ex.More(); ex.Next()) {
        Line2 line{kind, {}, {}};
        for (const gp_Pnt& p : sample3d(TopoDS::Edge(ex.Current()), deflection)) {
            line.pts.push_back({p.X(), p.Y()});
            line.box.add(line.pts.back());
        }
        if (line.pts.size() >= 2) {
            out.push_back(std::move(line));
        }
    }
}

int countEdges(const TopoDS_Shape& s) {
    int n = 0;
    if (!s.IsNull()) {
        for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
            ++n;
        }
    }
    return n;
}

ViewResult runView(const char* name, const gp_Ax2& axes, const TopoDS_Shape& shape, const NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>& edges,
                   double tol) {
    ViewResult view{name, {}, {}};
    const HLRAlgo_Projector projector(axes);

    // Exact HLR (B-rep): what drawings will use.
    const auto t0 = Clock::now();
    Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
    algo->Add(shape);
    algo->Projector(projector);
    algo->Update();
    algo->Hide();
    HLRBRep_HLRToShape hlr(algo);
    const TopoDS_Shape visible = hlr.VCompound();
    const TopoDS_Shape smooth = hlr.Rg1LineVCompound();
    const TopoDS_Shape outline = hlr.OutLineVCompound();
    const TopoDS_Shape hidden = hlr.HCompound();
    const TopoDS_Shape hiddenOutline = hlr.OutLineHCompound();
    view.exactMs = ms(t0, Clock::now());

    // Polygonal HLR (mesh based): candidate for fast previews of big assemblies.
    const auto t1 = Clock::now();
    Handle(HLRBRep_PolyAlgo) poly = new HLRBRep_PolyAlgo();
    poly->Load(shape);
    poly->Projector(projector);
    poly->Update();
    HLRBRep_PolyHLRToShape polyOut;
    polyOut.Update(poly);
    view.polyLines = countEdges(polyOut.VCompound()) + countEdges(polyOut.OutLineVCompound()) + countEdges(polyOut.HCompound());
    view.polyMs = ms(t1, Clock::now());

    const double deflection = tol * 0.25;
    collect(visible, LineKind::Visible, deflection, view.lines);
    collect(smooth, LineKind::Smooth, deflection, view.lines);
    collect(outline, LineKind::Outline, deflection, view.lines);
    collect(hidden, LineKind::Hidden, deflection, view.lines);
    collect(hiddenOutline, LineKind::HiddenOutline, deflection, view.lines);

    // Project every model edge with the same projector.
    std::vector<ModelEdge> model(edges.Extent() + 1);
    for (int i = 1; i <= edges.Extent(); ++i) {
        for (gp_Pnt p : sample3d(TopoDS::Edge(edges(i)), deflection)) {
            projector.Transform(p);
            model[i].pts.push_back({p.X(), p.Y()});
            model[i].z.push_back(p.Z());
            model[i].box.add(model[i].pts.back());
        }
    }

    // 2D -> 3D: a drawn line belongs to a model edge when all its sample points (and segment
    // midpoints) lie on that edge's projection. Ties (edges stacked along the view direction) are
    // broken by depth: visible lines take the nearest edge, hidden ones the farthest.
    for (Line2& line : view.lines) {
        view.box.merge(line.box);
        if (line.kind == LineKind::Outline || line.kind == LineKind::HiddenOutline) {
            continue; // silhouettes of curved faces have no model edge (map to the face instead)
        }
        std::vector<P2> probes = line.pts;
        for (size_t i = 1; i < line.pts.size(); ++i) {
            probes.push_back({0.5 * (line.pts[i - 1].x + line.pts[i].x), 0.5 * (line.pts[i - 1].y + line.pts[i].y)});
        }
        std::vector<int> hits;
        for (int i = 1; i < static_cast<int>(model.size()); ++i) {
            if (model[i].pts.size() < 2 || !model[i].box.overlaps(line.box, tol)) {
                continue;
            }
            bool all = true;
            for (const P2& p : probes) {
                if (polyDist(p, model[i].pts) > tol) {
                    all = false;
                    break;
                }
            }
            if (all) {
                hits.push_back(i);
            }
        }
        if (hits.size() == 1) {
            line.modelEdge = hits.front();
        } else if (hits.size() > 1) {
            const bool nearest = line.kind == LineKind::Visible || line.kind == LineKind::Smooth;
            auto depth = [&](int i) {
                double s = 0;
                for (double z : model[i].z) {
                    s += z;
                }
                return s / static_cast<double>(model[i].z.size());
            };
            line.modelEdge = *std::max_element(hits.begin(), hits.end(), [&](int a, int b) {
                return nearest ? depth(a) < depth(b) : depth(a) > depth(b);
            });
            line.tieBroken = true;
        }
    }
    return view;
}

// --- SVG ---------------------------------------------------------------------------------------
std::string svgPath(const Line2& l, double ox, double oy) {
    std::ostringstream d;
    d.setf(std::ios::fixed);
    d.precision(3);
    for (size_t i = 0; i < l.pts.size(); ++i) {
        d << (i == 0 ? "M" : " L") << (l.pts[i].x + ox) << ' ' << -(l.pts[i].y + oy);
    }
    return d.str();
}

} // namespace

int main() {
    const fs::path out = BL_SPIKE_OUT;
    fs::create_directories(out);

    const TopoDS_Shape part = makeBracket();
    const auto stats = bl::kernel::computeStats(part);
    std::string error;
    if (!bl::kernel::exportStep(part, out / "sample_bracket.step", &error)) {
        std::fprintf(stderr, "STEP export failed: %s\n", error.c_str());
    }
    BRepMesh_IncrementalMesh(part, 0.05, false, 0.3, true); // for PolyAlgo

    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(part, TopAbs_EDGE, edges);
    const double diag = std::hypot(stats.bbox[3] - stats.bbox[0], stats.bbox[4] - stats.bbox[1], stats.bbox[5] - stats.bbox[2]);
    const double tol = std::max(1e-3 * diag, 0.02);

    // Y-up views, same as the viewer: Front looks along -Z; Top looks down -Y with -Z up the sheet.
    const gp_Dir iso(1, 1, 1);
    std::vector<ViewResult> views;
    views.push_back(runView("Front", gp_Ax2(gp::Origin(), gp::DZ(), gp::DX()), part, edges, tol));
    views.push_back(runView("Top", gp_Ax2(gp::Origin(), gp::DY(), gp::DX()), part, edges, tol));
    views.push_back(runView("Right", gp_Ax2(gp::Origin(), gp::DX(), gp_Dir(0, 0, -1)), part, edges, tol));
    views.push_back(runView("Isometric", gp_Ax2(gp::Origin(), iso, gp_Dir(1, 0, -1)), part, edges, tol));

    // Third-angle layout: Top above Front, Right to the right, Isometric top-right.
    const double gap = 0.35 * std::max(views[0].box.w(), views[0].box.h());
    const Box2& F = views[0].box;
    struct Offset {
        double x, y;
    };
    const Offset off[4] = {
        {-F.x0, -F.y0},
        {-F.x0, F.h() + gap - views[1].box.y0},
        {F.w() + gap - views[2].box.x0, -F.y0},
        {F.w() + gap - views[3].box.x0, F.h() + gap - views[3].box.y0},
    };
    Box2 sheet;
    for (int v = 0; v < 4; ++v) {
        sheet.add(P2{views[v].box.x0 + off[v].x, views[v].box.y0 + off[v].y});
        sheet.add(P2{views[v].box.x1 + off[v].x, views[v].box.y1 + off[v].y});
    }
    const double margin = 0.08 * std::max(sheet.w(), sheet.h());

    std::ofstream svg(out / "hlr_views.svg");
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << sheet.x0 - margin << ' ' << -(sheet.y1 + margin) << ' '
        << sheet.w() + 2 * margin << ' ' << sheet.h() + 2 * margin << "\" width=\"1100\">\n"
        << "<style>path{fill:none;stroke-linecap:round;stroke-linejoin:round}"
           ".visible,.outline{stroke:#111;stroke-width:.5}.smooth{stroke:#9aa;stroke-width:.3}"
           ".hidden{stroke:#555;stroke-width:.3;stroke-dasharray:2 1.2}.unmapped{stroke:#e33}"
           "text{font:4px sans-serif;fill:#456}</style>\n"
        << "<rect x=\"" << sheet.x0 - margin << "\" y=\"" << -(sheet.y1 + margin) << "\" width=\"" << sheet.w() + 2 * margin
        << "\" height=\"" << sheet.h() + 2 * margin << "\" fill=\"#fff\"/>\n";

    std::ostringstream report;
    report << "BrokenLiquid spike: HLR -> SVG with 2D->3D edge map\n"
           << "part: solids=" << stats.solids << " faces=" << stats.faces << " edges=" << stats.edges
           << " volume=" << stats.volume << " valid=" << stats.valid << "\n"
           << "mapping tolerance: " << tol << " mm\n\n";
    bool pass = true;
    for (int v = 0; v < 4; ++v) {
        const ViewResult& view = views[v];
        int counts[5] = {}, mapped = 0, ties = 0, sharp = 0, sharpMapped = 0;
        svg << "<g id=\"" << view.name << "\">\n";
        for (const Line2& l : view.lines) {
            ++counts[static_cast<int>(l.kind)];
            const bool needsMap = l.kind != LineKind::Outline && l.kind != LineKind::HiddenOutline;
            mapped += l.modelEdge ? 1 : 0;
            ties += l.tieBroken ? 1 : 0;
            if (l.kind == LineKind::Visible) {
                ++sharp;
                sharpMapped += l.modelEdge ? 1 : 0;
            }
            svg << "<path class=\"" << kindName(l.kind) << (needsMap && !l.modelEdge ? " unmapped" : "") << "\"";
            if (l.modelEdge) {
                svg << " data-edge=\"" << l.modelEdge << "\"";
            }
            svg << " d=\"" << svgPath(l, off[v].x, off[v].y) << "\"/>\n";
        }
        svg << "<text x=\"" << view.box.x0 + off[v].x << "\" y=\"" << -(view.box.y0 + off[v].y) + 8 << "\">" << view.name
            << "</text>\n</g>\n";

        const double rate = sharp ? 100.0 * sharpMapped / sharp : 100.0;
        pass = pass && rate >= 95.0;
        char line[512];
        std::snprintf(line, sizeof line,
                      "%-9s exact %7.1f ms | poly %6.1f ms (%d lines) | visible %3d smooth %3d outline %3d hidden %3d "
                      "| mapped %3d (ties %d) | visible-sharp mapped %5.1f%%\n",
                      view.name.c_str(), view.exactMs, view.polyMs, view.polyLines, counts[0], counts[1], counts[2],
                      counts[3] + counts[4], mapped, ties, rate);
        report << line;
    }
    svg << "</svg>\n";
    report << "\nresult: " << (pass ? "PASS" : "FAIL") << " (>= 95% of visible sharp lines mapped in every view)\n";

    std::ofstream(out / "hlr_report.txt") << report.str();
    std::fputs(report.str().c_str(), stdout);
    std::printf("wrote %s\n", bl::kernel::toUtf8(out / "hlr_views.svg").c_str());
    return pass ? 0 : 1;
}
