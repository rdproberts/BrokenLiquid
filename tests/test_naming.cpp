// Revolve, fillet and chamfer, and the naming torture suite (ADR 0003): upstream edits must keep
// edge and face references on the same geometry, report a reference that really went away, and
// never guess silently.

#include "doc/PartFile.h"
#include "doc/Rebuild.h"
#include "kernel/Features.h"
#include "kernel/ShapeInfo.h"
#include "sketch/Geometry.h"

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>

#include <QDir>
#include <QFile>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace bl;
using sketch::P2;

namespace {

constexpr double kPi = std::numbers::pi;
const gp_Ax3 kFront(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));

// Volume removed by a fillet of radius r along a straight 90 degree edge of length l.
double filletLoss(double r, double l) { return (r * r - kPi * r * r / 4.0) * l; }

doc::SketchFeature& addSketch(doc::PartModel& m, const gp_Ax3& plane, const QString& name, int at = -1) {
    doc::SketchFeature s;
    s.name = name;
    s.planeName = name;
    s.plane = plane;
    doc::Feature entry;
    entry.id = s.id;
    entry.type = doc::FeatureType::Sketch;
    if (at < 0) {
        m.features.push_back(entry);
    } else {
        m.features.insert(m.features.begin() + at, entry);
    }
    m.sketches.push_back(std::move(s));
    return m.sketches.back();
}

QUuid addExtrude(doc::PartModel& m, const QUuid& sketch, double depth, bool cut = false,
                 doc::EndCondition end = doc::EndCondition::Blind, bool reverse = false, int at = -1) {
    doc::Feature f;
    f.type = doc::FeatureType::Extrude;
    f.name = QStringLiteral("Extrude");
    f.body = QUuid::createUuid();
    f.extrude.sketch = sketch;
    f.extrude.depth = depth;
    f.extrude.cut = cut;
    f.extrude.end = end;
    f.extrude.reverse = reverse;
    if (at < 0) {
        m.features.push_back(f);
    } else {
        m.features.insert(m.features.begin() + at, f);
    }
    return f.id;
}

QUuid addRevolve(doc::PartModel& m, const QUuid& sketch, double angle, bool cut = false, bool midPlane = false) {
    doc::Feature f;
    f.type = doc::FeatureType::Revolve;
    f.name = QStringLiteral("Revolve");
    f.body = QUuid::createUuid();
    f.revolve.sketch = sketch;
    f.revolve.angle = angle;
    f.revolve.cut = cut;
    f.revolve.midPlane = midPlane;
    m.features.push_back(f);
    return f.id;
}

double volumeOf(const doc::PartModel& m) {
    double v = 0.0;
    for (const doc::Body& b : m.bodies) {
        v += kernel::computeStats(b.shape).volume;
    }
    return v;
}

bool allValid(const doc::PartModel& m) {
    for (const doc::Body& b : m.bodies) {
        if (!kernel::computeStats(b.shape).valid) {
            return false;
        }
    }
    return true;
}

std::string sideName(const QUuid& feature, const QUuid& sketch, sketch::Id curve) {
    return doc::namingKey(feature) + "/Side(" + doc::namingKey(sketch) + ":" + std::to_string(curve) + ")";
}
std::string capName(const QUuid& feature, const char* cap) { return doc::namingKey(feature) + "/" + cap; }

// The reference to the edge of the first body between the faces named `a` and `b`, as picking it
// in the graphics area would make.
kernel::EdgeRef pickEdge(const doc::PartModel& m, const std::string& a, const std::string& b) {
    const kernel::NamedShape& body = m.bodies.front().named;
    const std::string wanted = a < b ? a + "^" + b : b + "^" + a;
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(body.shape, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge e = TopoDS::Edge(edges(i));
        if (kernel::edgeName(body, e) == wanted) {
            return kernel::edgeReferenceTo(body, e);
        }
    }
    ADD_FAILURE() << "no edge " << wanted;
    return {};
}

QUuid addEdgeFeature(doc::PartModel& m, doc::FeatureType type, std::vector<kernel::EdgeRef> edges, double size) {
    doc::Feature f;
    f.type = type;
    f.name = type == doc::FeatureType::Fillet ? QStringLiteral("Fillet") : QStringLiteral("Chamfer");
    if (type == doc::FeatureType::Fillet) {
        f.fillet.edges = std::move(edges);
        f.fillet.radius = size;
    } else {
        f.chamfer.edges = std::move(edges);
        f.chamfer.distance = size;
    }
    m.features.push_back(f);
    return f.id;
}

// Rebuilds everything after an edit made in place (no cached results to compare against).
void rebuildFrom(doc::PartModel& m) {
    for (doc::Feature& f : m.features) {
        f.after.reset();
    }
    doc::rebuild(m);
}

const char* state(const doc::PartModel& m, const QUuid& id) {
    switch (m.feature(id)->state) {
    case doc::FeatureState::Ok: return "ok";
    case doc::FeatureState::Warning: return "warning";
    case doc::FeatureState::Error: return "error";
    case doc::FeatureState::Suppressed: return "suppressed";
    case doc::FeatureState::RolledBack: return "rolled back";
    }
    return "";
}

// A 40 x 30 x 10 block from a rectangle on the Front plane.
struct Block {
    doc::PartModel m;
    QUuid sketch, boss;
    std::array<sketch::Id, 4> lines{}; // bottom, right, top, left
};
Block makeBlock() {
    Block b;
    doc::SketchFeature& s = addSketch(b.m, kFront, QStringLiteral("Sketch1"));
    b.lines = sketch::addRectangle(s.sketch, P2{0, 0}, P2{40, 30});
    b.sketch = s.id;
    b.boss = addExtrude(b.m, b.sketch, 10.0);
    doc::rebuild(b.m);
    return b;
}

gp_Pnt centroidOfFaces(const TopoDS_Shape& faces) {
    GProp_GProps props;
    BRepGProp::SurfaceProperties(faces, props);
    return props.CentreOfMass();
}

} // namespace

// --- Edge references ---------------------------------------------------------------------------------

TEST(EdgeNaming, EdgesAreNamedByTheirTwoFaces) {
    Block b = makeBlock();
    const kernel::NamedShape& body = b.m.bodies.front().named;
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(body.shape, TopAbs_EDGE, edges);
    ASSERT_EQ(edges.Extent(), 12);
    for (int i = 1; i <= edges.Extent(); ++i) {
        const TopoDS_Edge e = TopoDS::Edge(edges(i));
        const kernel::EdgeRef ref = kernel::edgeReferenceTo(body, e);
        EXPECT_FALSE(ref.face1.empty());
        const kernel::ResolvedEdge r = kernel::resolveEdge({&body}, ref);
        EXPECT_EQ(r.status, kernel::RefStatus::Exact) << ref.name();
        EXPECT_TRUE(r.edge.IsSame(e)) << ref.name();
    }
    const kernel::EdgeRef corner =
        pickEdge(b.m, sideName(b.boss, b.sketch, b.lines[1]), capName(b.boss, "EndCap"));
    EXPECT_TRUE(corner.signature.straight);
    EXPECT_NEAR(corner.signature.length, 30.0, 1e-9);
    EXPECT_NEAR(corner.signature.middle.X(), 40.0, 1e-9);
    EXPECT_NEAR(corner.signature.middle.Z(), 10.0, 1e-9);
}

// --- Revolve -----------------------------------------------------------------------------------------

TEST(Features, RevolveTurnsTheProfileAboutTheCenterline) {
    doc::PartModel m;
    doc::SketchFeature& s = addSketch(m, kFront, QStringLiteral("Sketch1"));
    const sketch::Id axis = s.sketch.addLine(0, 0, 50, 0);
    s.sketch.curve(axis)->construction = true;
    // A stepped shaft profile sitting on the axis: radius 10 for 30 mm, then radius 5 for 20 mm.
    s.sketch.addLine(0, 0, 50, 0);
    s.sketch.addLine(50, 0, 50, 5);
    s.sketch.addLine(50, 5, 30, 5);
    s.sketch.addLine(30, 5, 30, 10);
    s.sketch.addLine(30, 10, 0, 10);
    s.sketch.addLine(0, 10, 0, 0);
    const QUuid sketchId = s.id;
    const QUuid shaft = addRevolve(m, sketchId, 360.0);
    doc::rebuild(m);
    ASSERT_EQ(m.bodies.size(), 1u) << m.feature(shaft)->message.toStdString();
    EXPECT_STREQ(state(m, shaft), "ok");
    EXPECT_TRUE(allValid(m));
    const double full = kPi * (100.0 * 30.0 + 25.0 * 20.0);
    EXPECT_NEAR(volumeOf(m), full, 1e-6 * full);
    EXPECT_TRUE(m.bodies.front().named.find(capName(shaft, "StartCap")).empty()); // a full turn has no caps

    // A quarter turn, then a quarter turn about the middle: same volume, caps named.
    m.feature(shaft)->revolve.angle = 90.0;
    rebuildFrom(m);
    EXPECT_NEAR(volumeOf(m), full / 4.0, 1e-6 * full);
    EXPECT_EQ(m.bodies.front().named.find(capName(shaft, "StartCap")).size(), 1u);
    EXPECT_EQ(m.bodies.front().named.find(capName(shaft, "EndCap")).size(), 1u);
    m.feature(shaft)->revolve.midPlane = true;
    rebuildFrom(m);
    EXPECT_NEAR(volumeOf(m), full / 4.0, 1e-6 * full);
    Bnd_Box box;
    BRepBndLib::Add(m.bodies.front().shape, box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    EXPECT_NEAR(z0, -z1, 1e-3); // symmetric about the sketch plane

    // A groove cut by a second revolve.
    m.feature(shaft)->revolve.angle = 360.0;
    m.feature(shaft)->revolve.midPlane = false;
    doc::SketchFeature& g = addSketch(m, kFront, QStringLiteral("Sketch2"));
    const sketch::Id axis2 = g.sketch.addLine(0, 0, 10, 0);
    g.sketch.curve(axis2)->construction = true;
    sketch::addRectangle(g.sketch, P2{10, 8}, P2{12, 12});
    const QUuid groove = addRevolve(m, g.id, 360.0, true);
    rebuildFrom(m);
    EXPECT_STREQ(state(m, groove), "ok");
    EXPECT_NEAR(volumeOf(m), full - kPi * (100.0 - 64.0) * 2.0, 1e-6 * full);
}

TEST(Features, RevolveRefusesAProfileAcrossTheAxis) {
    doc::PartModel m;
    doc::SketchFeature& s = addSketch(m, kFront, QStringLiteral("Sketch1"));
    sketch::addRectangle(s.sketch, P2{0, -5}, P2{20, 5});
    const QUuid noAxis = addRevolve(m, s.id, 360.0);
    doc::rebuild(m);
    EXPECT_STREQ(state(m, noAxis), "error"); // no centerline
    const sketch::Id axis = m.sketch(s.id)->sketch.addLine(0, 0, 20, 0);
    m.sketch(s.id)->sketch.curve(axis)->construction = true;
    rebuildFrom(m);
    EXPECT_STREQ(state(m, noAxis), "error");
    EXPECT_NE(m.feature(noAxis)->message.indexOf(QStringLiteral("crosses")), -1);
}

// --- Fillet / chamfer --------------------------------------------------------------------------------

TEST(Features, FilletAndChamferNameTheirFaces) {
    Block b = makeBlock();
    const std::string right = sideName(b.boss, b.sketch, b.lines[1]);
    const std::string top = capName(b.boss, "EndCap");
    const QUuid fillet = addEdgeFeature(b.m, doc::FeatureType::Fillet, {pickEdge(b.m, right, top)}, 3.0);
    rebuildFrom(b.m);
    EXPECT_STREQ(state(b.m, fillet), "ok");
    EXPECT_TRUE(allValid(b.m));
    EXPECT_NEAR(volumeOf(b.m), 12000.0 - filletLoss(3.0, 30.0), 1e-6);
    const kernel::NamedShape& body = b.m.bodies.front().named;
    const std::string blend =
        doc::namingKey(fillet) + "/Blend(" + (right < top ? right + "^" + top : top + "^" + right) + ")";
    EXPECT_EQ(body.find(blend).size(), 1u);
    EXPECT_EQ(body.find(top).size(), 1u); // the trimmed top face keeps its name

    // Chamfer the opposite top edge: 2 on one face, 4 on the other; flip swaps them.
    const std::string left = sideName(b.boss, b.sketch, b.lines[3]);
    const QUuid chamfer = addEdgeFeature(b.m, doc::FeatureType::Chamfer, {pickEdge(b.m, left, top)}, 2.0);
    b.m.feature(chamfer)->chamfer.type = doc::ChamferType::TwoDistances;
    b.m.feature(chamfer)->chamfer.distance2 = 4.0;
    rebuildFrom(b.m);
    EXPECT_STREQ(state(b.m, chamfer), "ok");
    EXPECT_NEAR(volumeOf(b.m), 12000.0 - filletLoss(3.0, 30.0) - 0.5 * 2.0 * 4.0 * 30.0, 1e-6);
    const std::string key = doc::namingKey(chamfer) + "/";
    const gp_Pnt c1 = centroidOfFaces(kernel::facesNamed(b.m.bodies.front().named, key));
    b.m.feature(chamfer)->chamfer.flip = true;
    rebuildFrom(b.m);
    EXPECT_NEAR(volumeOf(b.m), 12000.0 - filletLoss(3.0, 30.0) - 0.5 * 2.0 * 4.0 * 30.0, 1e-6);
    const gp_Pnt c2 = centroidOfFaces(kernel::facesNamed(b.m.bodies.front().named, key));
    EXPECT_GT(c1.Distance(c2), 0.5); // the bevel leans the other way
    EXPECT_NEAR(c1.X() + c2.X(), 3.0, 1e-6); // bevel centres at x = 1 and x = 2, one each way
}

TEST(Features, FilletTooLargeIsAnErrorNotACrash) {
    Block b = makeBlock();
    const QUuid fillet = addEdgeFeature(
        b.m, doc::FeatureType::Fillet,
        {pickEdge(b.m, sideName(b.boss, b.sketch, b.lines[1]), capName(b.boss, "EndCap"))}, 25.0);
    rebuildFrom(b.m);
    EXPECT_STREQ(state(b.m, fillet), "error");
    EXPECT_NEAR(volumeOf(b.m), 12000.0, 1e-6); // the failed feature changes nothing
}

// --- Naming torture suite --------------------------------------------------------------------------

TEST(NamingTorture, FilletFollowsItsEdgeThroughUpstreamEdits) {
    Block b = makeBlock();
    doc::PartModel& m = b.m;
    const std::string right = sideName(b.boss, b.sketch, b.lines[1]);
    const QUuid fillet =
        addEdgeFeature(m, doc::FeatureType::Fillet, {pickEdge(m, right, capName(b.boss, "EndCap"))}, 3.0);
    rebuildFrom(m);
    ASSERT_STREQ(state(m, fillet), "ok");

    // 1. Dimension change: the block gets wider. The fillet stays on the right top edge.
    for (sketch::Point& p : m.sketch(b.sketch)->sketch.points()) {
        if (std::abs(p.x - 40.0) < 1e-9) {
            p.x = 60.0;
        }
    }
    rebuildFrom(m);
    EXPECT_STREQ(state(m, fillet), "ok");
    EXPECT_NEAR(volumeOf(m), 60.0 * 30.0 * 10.0 - filletLoss(3.0, 30.0), 1e-6);
    const TopoDS_Shape blend = kernel::facesNamed(m.bodies.front().named, doc::namingKey(fillet) + "/");
    EXPECT_NEAR(centroidOfFaces(blend).X(), 60.0 - 3.0 + 3.0 * 2.0 / kPi, 1e-3); // quarter cylinder about x = 57

    // 2. Depth change.
    m.feature(b.boss)->extrude.depth = 20.0;
    rebuildFrom(m);
    EXPECT_STREQ(state(m, fillet), "ok");
    EXPECT_NEAR(volumeOf(m), 60.0 * 30.0 * 20.0 - filletLoss(3.0, 30.0), 1e-6);

    // 3. Segments inserted in the sketch: the top line becomes a notch. The right edge is untouched.
    doc::SketchFeature& s = *m.sketch(b.sketch);
    s.sketch.remove(b.lines[2]);
    s.sketch.addLine(60, 30, 40, 30);
    s.sketch.addLine(40, 30, 40, 25);
    s.sketch.addLine(40, 25, 20, 25);
    s.sketch.addLine(20, 25, 20, 30);
    s.sketch.addLine(20, 30, 0, 30);
    rebuildFrom(m);
    EXPECT_STREQ(state(m, fillet), "ok") << m.feature(fillet)->message.toStdString();
    EXPECT_NEAR(volumeOf(m), (60.0 * 30.0 - 20.0 * 5.0) * 20.0 - filletLoss(3.0, 30.0), 1e-6);

    // 4. A feature inserted upstream: a hole through the block, before the fillet.
    const int at = m.featureIndex(fillet);
    doc::SketchFeature& h = addSketch(m, kFront, QStringLiteral("Hole"), at);
    h.sketch.addCircle(10, 10, 3);
    const QUuid hole = addExtrude(m, h.id, 20.0, false, doc::EndCondition::Blind, false, at + 1);
    m.feature(hole)->extrude.cut = true;
    rebuildFrom(m);
    EXPECT_STREQ(state(m, hole), "ok");
    EXPECT_STREQ(state(m, fillet), "ok");
    EXPECT_NEAR(volumeOf(m), (60.0 * 30.0 - 20.0 * 5.0 - 9.0 * kPi) * 20.0 - filletLoss(3.0, 30.0), 1e-6);
    EXPECT_TRUE(allValid(m));

    // 5. A slot across the filleted edge splits it: both halves carry the name, the closer one is
    //    taken and the feature says so.
    const int at2 = m.featureIndex(fillet);
    doc::SketchFeature& slot = addSketch(m, kFront, QStringLiteral("Slot"), at2);
    sketch::addRectangle(slot.sketch, P2{50, 18}, P2{70, 20});
    const QUuid slotCut = addExtrude(m, slot.id, 20.0, true, doc::EndCondition::Blind, false, at2 + 1);
    rebuildFrom(m);
    EXPECT_STREQ(state(m, slotCut), "ok");
    EXPECT_STREQ(state(m, fillet), "warning");
    EXPECT_TRUE(allValid(m));

    // 6. The edge itself goes away (a chamfer upstream replaces it): reported, not guessed.
    m.feature(slotCut)->suppressed = true;
    rebuildFrom(m);
    EXPECT_STREQ(state(m, fillet), "ok");
    const int at3 = m.featureIndex(fillet);
    doc::Feature bevel;
    bevel.type = doc::FeatureType::Chamfer;
    bevel.name = QStringLiteral("Chamfer");
    bevel.chamfer.edges = {m.feature(fillet)->fillet.edges.front()};
    bevel.chamfer.distance = 2.0;
    m.features.insert(m.features.begin() + at3, bevel);
    rebuildFrom(m);
    EXPECT_STREQ(state(m, bevel.id), "ok");
    EXPECT_STREQ(state(m, fillet), "error");
    EXPECT_TRUE(allValid(m));

    // 7. Suppress the chamfer again: the fillet is back on its edge.
    m.feature(bevel.id)->suppressed = true;
    rebuildFrom(m);
    EXPECT_STREQ(state(m, fillet), "ok");
}

TEST(NamingTorture, SketchOnAFilletedFaceAndEdgesOfRevolvedFaces) {
    // A sketch on the top face survives a fillet on that face's edge (the face is trimmed, not
    // replaced), and edges between revolved faces resolve after the profile changes.
    Block b = makeBlock();
    doc::PartModel& m = b.m;
    const std::string top = capName(b.boss, "EndCap");
    addEdgeFeature(m, doc::FeatureType::Fillet, {pickEdge(m, sideName(b.boss, b.sketch, b.lines[0]), top)}, 2.0);
    rebuildFrom(m);
    const std::vector<TopoDS_Face> faces = m.bodies.front().named.find(top);
    ASSERT_EQ(faces.size(), 1u);
    doc::SketchFeature& s = addSketch(m, kernel::sketchAxesOnFace(*kernel::planeOfFace(faces.front())),
                                      QStringLiteral("OnTop"));
    s.face = kernel::referenceTo(m.bodies.front().named, faces.front());
    s.sketch.addCircle(20, 15, 4);
    const QUuid onTop = s.id;
    const QUuid boss = addExtrude(m, onTop, 5.0);
    rebuildFrom(m);
    EXPECT_STREQ(state(m, onTop), "ok");
    EXPECT_STREQ(state(m, boss), "ok");
    m.feature(b.boss)->extrude.depth = 15.0;
    rebuildFrom(m);
    EXPECT_STREQ(state(m, onTop), "ok");
    EXPECT_NEAR(m.sketch(onTop)->plane.Location().Z(), 15.0, 1e-9);

    // Revolved: fillet the shoulder edge of a stepped shaft, then lengthen the shaft.
    doc::PartModel r;
    doc::SketchFeature& p = addSketch(r, kFront, QStringLiteral("Profile"));
    const sketch::Id axis = p.sketch.addLine(0, 0, 50, 0);
    p.sketch.curve(axis)->construction = true;
    p.sketch.addLine(0, 0, 50, 0);
    p.sketch.addLine(50, 0, 50, 5);
    const sketch::Id small = p.sketch.addLine(50, 5, 30, 5);
    const sketch::Id shoulder = p.sketch.addLine(30, 5, 30, 10);
    p.sketch.addLine(30, 10, 0, 10);
    p.sketch.addLine(0, 10, 0, 0);
    const QUuid profile = p.id;
    const QUuid shaft = addRevolve(r, profile, 360.0);
    doc::rebuild(r);
    const QUuid round = addEdgeFeature(
        r, doc::FeatureType::Fillet, {pickEdge(r, sideName(shaft, profile, small), sideName(shaft, profile, shoulder))},
        1.0);
    rebuildFrom(r);
    EXPECT_STREQ(state(r, round), "ok") << r.feature(round)->message.toStdString();
    const double before = volumeOf(r);
    for (sketch::Point& q : r.sketch(profile)->sketch.points()) {
        if (std::abs(q.x - 50.0) < 1e-9) {
            q.x = 70.0;
        }
    }
    rebuildFrom(r);
    EXPECT_STREQ(state(r, round), "ok");
    EXPECT_NEAR(volumeOf(r) - before, kPi * 25.0 * 20.0, 1e-3);
}

TEST(PartFile, NewFeaturesRoundTrip) {
    Block b = makeBlock();
    doc::PartModel& m = b.m;
    addEdgeFeature(m, doc::FeatureType::Fillet,
                   {pickEdge(m, sideName(b.boss, b.sketch, b.lines[1]), capName(b.boss, "EndCap"))}, 3.0);
    const QUuid chamfer = addEdgeFeature(
        m, doc::FeatureType::Chamfer, {pickEdge(m, sideName(b.boss, b.sketch, b.lines[3]), capName(b.boss, "EndCap"))}, 1.5);
    m.feature(chamfer)->chamfer.type = doc::ChamferType::TwoDistances;
    m.feature(chamfer)->chamfer.flip = true;
    doc::SketchFeature& s = addSketch(m, kFront, QStringLiteral("Ring"));
    const sketch::Id axis = s.sketch.addLine(60, 0, 60, 30);
    s.sketch.curve(axis)->construction = true;
    sketch::addRectangle(s.sketch, P2{65, 10}, P2{70, 20});
    const QUuid ring = addRevolve(m, s.id, 180.0);
    m.feature(ring)->revolve.axis = axis;
    m.feature(ring)->revolve.reverse = true;
    m.filletCount = 1;
    m.revolveCount = 1;
    rebuildFrom(m);
    for (const doc::Feature& f : m.features) {
        EXPECT_EQ(f.state, doc::FeatureState::Ok) << m.featureName(f).toStdString() << ": " << f.message.toStdString();
    }

    const QString path = QDir::current().absoluteFilePath(QStringLiteral("test_naming_roundtrip.blpart"));
    QString error;
    ASSERT_TRUE(doc::savePart(m, path, &error)) << error.toStdString();
    doc::LoadedPart loaded = doc::loadPart(path);
    QFile::remove(path);
    ASSERT_TRUE(loaded.ok) << loaded.error.toStdString();
    doc::rebuild(loaded.model);
    const doc::PartModel& r = loaded.model;
    ASSERT_EQ(r.features.size(), m.features.size());
    for (size_t i = 0; i < r.features.size(); ++i) {
        EXPECT_TRUE(r.features[i].sameInput(m.features[i])) << i;
        EXPECT_EQ(r.features[i].state, doc::FeatureState::Ok) << i;
    }
    EXPECT_EQ(r.filletCount, 1);
    EXPECT_EQ(r.revolveCount, 1);
    EXPECT_NEAR(volumeOf(r), volumeOf(m), 1e-6);
}
