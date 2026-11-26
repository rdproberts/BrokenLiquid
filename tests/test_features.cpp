// Part features (Phase 3): sketch profiles, extrude / cut through the rebuild, persistent face
// references (ADR 0003) and native part files.

#include "doc/Document.h"
#include "doc/PartFile.h"
#include "doc/Rebuild.h"
#include "kernel/Features.h"
#include "kernel/ShapeInfo.h"
#include "sketch/Geometry.h"

#include <BRepPrimAPI_MakeBox.hxx>

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

// A sketch plus its history entry at the end of the history.
doc::SketchFeature& addSketch(doc::PartModel& m, const gp_Ax3& plane, const QString& name) {
    doc::SketchFeature s;
    s.name = name;
    s.planeName = name;
    s.plane = plane;
    doc::Feature entry;
    entry.id = s.id;
    entry.type = doc::FeatureType::Sketch;
    m.features.push_back(entry);
    m.sketches.push_back(std::move(s));
    return m.sketches.back();
}

QUuid addExtrude(doc::PartModel& m, const QUuid& sketch, double depth, bool cut = false,
                 doc::EndCondition end = doc::EndCondition::Blind, bool reverse = false) {
    doc::Feature f;
    f.type = doc::FeatureType::Extrude;
    f.name = cut ? QStringLiteral("ExtrudeCut%1").arg(++m.cutCount) : QStringLiteral("Extrude%1").arg(++m.extrudeCount);
    f.body = QUuid::createUuid();
    f.extrude.sketch = sketch;
    f.extrude.depth = depth;
    f.extrude.cut = cut;
    f.extrude.end = end;
    f.extrude.reverse = reverse;
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

// A sketch on the named face of the first body, like picking the face in the graphics area.
doc::SketchFeature& sketchOnFace(doc::PartModel& m, const QUuid& feature, const char* role, const QString& name) {
    const doc::Body& body = m.bodies.front();
    const std::vector<TopoDS_Face> faces = body.named.find(doc::namingKey(feature) + "/" + role);
    EXPECT_EQ(faces.size(), 1u);
    const gp_Pln plane = *kernel::planeOfFace(faces.front());
    doc::SketchFeature& s = addSketch(m, kernel::sketchAxesOnFace(plane), name);
    s.face = kernel::referenceTo(body.named, faces.front());
    return s;
}

} // namespace

TEST(Features, ProfileLeavesIslandsOut) {
    // Rectangle, a circle inside it and a smaller circle inside that: the even-odd rule extrudes
    // the rectangle minus the circle, plus the small disc.
    sketch::Sketch s;
    const auto rect = sketch::addRectangle(s, P2{0, 0}, P2{40, 30});
    const sketch::Id hole = s.addCircle(20, 15, 5);
    s.addCircle(20, 15, 2);
    const sketch::Contours contours = sketch::findRegions(s);
    ASSERT_EQ(contours.regions.size(), 3u);
    const std::vector<int> regions = kernel::defaultRegions(contours);
    ASSERT_EQ(regions.size(), 2u);

    std::string error;
    const kernel::Profile profile = kernel::buildProfile(s, kFront, contours, regions, &error);
    ASSERT_EQ(profile.faces.size(), 2u) << error;
    const kernel::NamedShape solid = kernel::extrude(profile, gp::DZ(), 10.0, "E", "S", &error);
    ASSERT_FALSE(solid.isNull()) << error;
    const kernel::ShapeStats stats = kernel::computeStats(solid.shape);
    EXPECT_TRUE(stats.valid);
    EXPECT_NEAR(stats.volume, (1200.0 - 25.0 * kPi + 4.0 * kPi) * 10.0, 1e-6);
    for (sketch::Id line : rect) {
        EXPECT_EQ(solid.find("E/Side(S:" + std::to_string(line) + ")").size(), 1u);
    }
    EXPECT_EQ(solid.find("E/Side(S:" + std::to_string(hole) + ")").size(), 1u);
    EXPECT_EQ(solid.find("E/StartCap").size(), 2u); // one per profile face, kept through the union
    EXPECT_EQ(solid.find("E/EndCap").size(), 2u);
}

TEST(Features, SketchOnFaceFollowsTheModel) {
    doc::PartModel m;
    doc::SketchFeature& base = addSketch(m, kFront, QStringLiteral("Sketch1"));
    sketch::addRectangle(base.sketch, P2{0, 0}, P2{40, 30});
    const QUuid baseId = base.id;
    const QUuid boss = addExtrude(m, baseId, 10.0);
    doc::rebuild(m);
    ASSERT_EQ(m.bodies.size(), 1u);
    EXPECT_NEAR(volumeOf(m), 12000.0, 1e-6);

    // Sketch2 on the top face, a circle cut through everything below it.
    doc::SketchFeature& top = sketchOnFace(m, boss, "EndCap", QStringLiteral("Sketch2"));
    EXPECT_NEAR(top.plane.Location().Z(), 10.0, 1e-9);
    top.sketch.addCircle(20, 15, 5);
    const QUuid topId = top.id;
    const QUuid hole = addExtrude(m, topId, 0.0, true, doc::EndCondition::ThroughAll, true);
    doc::PartModel before = m;
    doc::rebuild(m, &before);
    ASSERT_EQ(m.bodies.size(), 1u);
    EXPECT_EQ(m.feature(hole)->state, doc::FeatureState::Ok) << m.feature(hole)->message.toStdString();
    EXPECT_NEAR(volumeOf(m), 12000.0 - 25.0 * kPi * 10.0, 1e-6);

    // Make the boss taller: the sketch moves up with its face and the hole still goes through.
    doc::PartModel previous = m;
    m.feature(boss)->extrude.depth = 25.0;
    doc::rebuild(m, &previous);
    EXPECT_NEAR(m.sketch(topId)->plane.Location().Z(), 25.0, 1e-9);
    EXPECT_EQ(m.feature(topId)->state, doc::FeatureState::Ok);
    EXPECT_NEAR(volumeOf(m), 30000.0 - 25.0 * kPi * 25.0, 1e-6);

    // Widen the base sketch: the top face keeps its name.
    previous = m;
    for (sketch::Point& p : m.sketch(baseId)->sketch.points()) {
        if (std::abs(p.x - 40.0) < 1e-9) { // the right side and the corners joined to it
            p.x = 60.0;
        }
    }
    doc::rebuild(m, &previous);
    EXPECT_EQ(m.feature(topId)->state, doc::FeatureState::Ok);
    EXPECT_NEAR(volumeOf(m), 60.0 * 30.0 * 25.0 - 25.0 * kPi * 25.0, 1e-6);
}

TEST(Features, MissingFaceIsReportedNotGuessed) {
    doc::PartModel m;
    doc::SketchFeature& base = addSketch(m, kFront, QStringLiteral("Sketch1"));
    sketch::addRectangle(base.sketch, P2{0, 0}, P2{40, 30});
    addExtrude(m, base.id, 10.0);
    doc::rebuild(m);
    doc::SketchFeature& block = sketchOnFace(m, m.features[1].id, "EndCap", QStringLiteral("Sketch2"));
    sketch::addRectangle(block.sketch, P2{5, 5}, P2{15, 15});
    const QUuid upper = addExtrude(m, block.id, 10.0);
    doc::rebuild(m);
    doc::SketchFeature& onUpper = sketchOnFace(m, upper, "EndCap", QStringLiteral("Sketch3"));
    onUpper.sketch.addCircle(10, 10, 2);
    const QUuid onUpperId = onUpper.id;
    const QUuid cut = addExtrude(m, onUpperId, 3.0, true, doc::EndCondition::Blind, true);
    doc::rebuild(m);
    EXPECT_EQ(m.feature(cut)->state, doc::FeatureState::Ok);
    EXPECT_NEAR(volumeOf(m), 12000.0 + 1000.0 - 4.0 * kPi * 3.0, 1e-6);

    // Suppressing the upper block takes away the face Sketch3 is on: an error, not a guess.
    doc::PartModel previous = m;
    m.feature(upper)->suppressed = true;
    doc::rebuild(m, &previous);
    EXPECT_EQ(m.feature(upper)->state, doc::FeatureState::Suppressed);
    EXPECT_EQ(m.feature(onUpperId)->state, doc::FeatureState::Error);
    EXPECT_EQ(m.feature(cut)->state, doc::FeatureState::Error);
    EXPECT_NEAR(volumeOf(m), 12000.0, 1e-6);

    previous = m;
    m.feature(upper)->suppressed = false;
    doc::rebuild(m, &previous);
    EXPECT_EQ(m.feature(onUpperId)->state, doc::FeatureState::Ok);
    EXPECT_EQ(m.feature(cut)->state, doc::FeatureState::Ok);
}

TEST(Features, BossesMergeOnlyWhenTheyTouch) {
    doc::PartModel m;
    doc::SketchFeature& a = addSketch(m, kFront, QStringLiteral("Sketch1"));
    sketch::addRectangle(a.sketch, P2{0, 0}, P2{10, 10});
    addExtrude(m, a.id, 5.0);
    doc::SketchFeature& b = addSketch(m, kFront, QStringLiteral("Sketch2"));
    sketch::addRectangle(b.sketch, P2{30, 0}, P2{40, 10});
    addExtrude(m, b.id, 5.0);
    doc::rebuild(m);
    EXPECT_EQ(m.bodies.size(), 2u); // apart: two bodies

    doc::SketchFeature& bridge = addSketch(m, kFront, QStringLiteral("Sketch3"));
    sketch::addRectangle(bridge.sketch, P2{10, 0}, P2{30, 5});
    const QUuid join = addExtrude(m, bridge.id, 5.0);
    doc::PartModel previous = m;
    doc::rebuild(m, &previous);
    ASSERT_EQ(m.bodies.size(), 1u); // the bridge joins both
    EXPECT_NEAR(volumeOf(m), (100.0 + 100.0 + 100.0) * 5.0, 1e-6);
    EXPECT_TRUE(kernel::computeStats(m.bodies.front().shape).valid);

    previous = m;
    m.feature(join)->extrude.merge = false;
    doc::rebuild(m, &previous);
    EXPECT_EQ(m.bodies.size(), 3u);
}

TEST(Features, RebuildReusesUnchangedResults) {
    doc::PartModel m;
    doc::SketchFeature& a = addSketch(m, kFront, QStringLiteral("Sketch1"));
    sketch::addRectangle(a.sketch, P2{0, 0}, P2{10, 10});
    addExtrude(m, a.id, 5.0);
    doc::SketchFeature& b = addSketch(m, kFront, QStringLiteral("Sketch2"));
    b.sketch.addCircle(5, 5, 2);
    const QUuid cut = addExtrude(m, b.id, 0.0, true, doc::EndCondition::ThroughAll);
    doc::rebuild(m);
    const doc::PartModel previous = m;
    m.feature(cut)->extrude.end = doc::EndCondition::Blind;
    m.feature(cut)->extrude.depth = 2.0;
    doc::rebuild(m, &previous);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(m.features[static_cast<size_t>(i)].after, previous.features[static_cast<size_t>(i)].after);
    }
    EXPECT_NE(m.features[3].after, previous.features[3].after);
    EXPECT_NEAR(volumeOf(m), 500.0 - 4.0 * kPi * 2.0, 1e-6);
}

TEST(Features, DocumentRebuildsOnEveryEditAndUndoes) {
    doc::Document d(doc::DocumentType::Part, QStringLiteral("Part1"));
    QUuid sketchId, boss;
    ASSERT_TRUE(d.transact(QStringLiteral("Extrude"), [&](doc::PartModel& m) {
        doc::SketchFeature& s = addSketch(m, kFront, QStringLiteral("Sketch1"));
        sketch::addRectangle(s.sketch, P2{0, 0}, P2{10, 10});
        sketchId = s.id;
        boss = addExtrude(m, sketchId, 5.0);
        return true;
    }));
    EXPECT_NEAR(volumeOf(d.model()), 500.0, 1e-6);
    ASSERT_TRUE(d.transact(QStringLiteral("Edit"), [&](doc::PartModel& m) {
        m.feature(boss)->extrude.depth = 7.0;
        return true;
    }));
    EXPECT_NEAR(volumeOf(d.model()), 700.0, 1e-6);
    d.undo();
    EXPECT_NEAR(volumeOf(d.model()), 500.0, 1e-6);
}

TEST(PartFile, SaveAndOpenRoundTrip) {
    doc::PartModel m;
    doc::addImport(m, BRepPrimAPI_MakeBox(gp_Pnt(0, 0, -20), 10, 10, 20).Shape(), QStringLiteral("Block"),
                   QStringLiteral("C:/models/block.step"));
    doc::SketchFeature& s = addSketch(m, kFront, QStringLiteral("Sketch1"));
    sketch::addRectangle(s.sketch, P2{0, 0}, P2{10, 10});
    s.sketch.addDimension(sketch::ConstraintType::Distance, {s.sketch.curves().front().id}, 10.0);
    const QUuid sketchId = s.id;
    addExtrude(m, sketchId, 5.0);
    m.units.system = UnitSystem::Inch;
    doc::rebuild(m);
    ASSERT_EQ(m.bodies.size(), 1u); // the boss merged into the imported block
    m.bodies.front().name = QStringLiteral("Bracket");
    m.bodies.front().color = QColor(200, 30, 40);

    const QString path = QDir::current().absoluteFilePath(QStringLiteral("test_roundtrip.blpart"));
    QString error;
    ASSERT_TRUE(doc::savePart(m, path, &error)) << error.toStdString();
    ASSERT_TRUE(doc::savePart(m, path, &error)) << error.toStdString(); // again: keeps a .bak
    EXPECT_TRUE(QFile::exists(path + QStringLiteral(".bak")));

    doc::LoadedPart loaded = doc::loadPart(path);
    ASSERT_TRUE(loaded.ok) << loaded.error.toStdString();
    doc::rebuild(loaded.model);
    const doc::PartModel& r = loaded.model;
    EXPECT_EQ(r.features.size(), m.features.size());
    EXPECT_EQ(r.units.system, UnitSystem::Inch);
    EXPECT_EQ(r.extrudeCount, m.extrudeCount);
    ASSERT_NE(r.sketch(sketchId), nullptr);
    EXPECT_TRUE(r.sketch(sketchId)->sketch == m.sketch(sketchId)->sketch);
    ASSERT_EQ(r.bodies.size(), 1u);
    EXPECT_EQ(r.bodies.front().id, m.bodies.front().id);
    EXPECT_EQ(r.bodies.front().name, QStringLiteral("Bracket"));
    EXPECT_EQ(r.bodies.front().color, QColor(200, 30, 40));
    EXPECT_NEAR(volumeOf(r), volumeOf(m), 1e-6);
    EXPECT_NEAR(volumeOf(r), 2000.0 + 500.0, 1e-6);

    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".bak"));
    EXPECT_FALSE(doc::loadPart(path).ok);
}
