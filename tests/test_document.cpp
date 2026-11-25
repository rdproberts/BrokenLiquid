#include "doc/Document.h"

#include <BRepPrimAPI_MakeBox.hxx>

#include <gtest/gtest.h>

using namespace bl;

namespace {

doc::Body box(const QString& name) {
    doc::Body b;
    b.name = name;
    b.shape = BRepPrimAPI_MakeBox(10, 20, 30).Shape();
    return b;
}

} // namespace

TEST(Document, TransactPushesUndoAndSignals) {
    doc::Document d(doc::DocumentType::Part, QStringLiteral("Part1"));
    int modelSignals = 0, historySignals = 0;
    QObject::connect(&d, &doc::Document::modelChanged, [&] { ++modelSignals; });
    QObject::connect(&d, &doc::Document::historyChanged, [&] { ++historySignals; });

    EXPECT_TRUE(d.isPristine());
    EXPECT_TRUE(d.transact(QStringLiteral("Import Body"), [](doc::PartModel& m) {
        m.bodies.push_back(box(QStringLiteral("Box")));
        return true;
    }));
    EXPECT_EQ(d.model().bodies.size(), 1u);
    EXPECT_TRUE(d.canUndo());
    EXPECT_EQ(d.undoLabel(), QStringLiteral("Import Body"));
    EXPECT_TRUE(d.isModified());
    EXPECT_FALSE(d.isPristine());
    EXPECT_EQ(modelSignals, 1);
    EXPECT_EQ(historySignals, 1);

    d.undo();
    EXPECT_TRUE(d.model().bodies.empty());
    EXPECT_FALSE(d.isModified());
    d.redo();
    ASSERT_EQ(d.model().bodies.size(), 1u);
    EXPECT_EQ(d.model().bodies.front().name, QStringLiteral("Box"));
}

TEST(Document, NoOpTransactionsAreDropped) {
    doc::Document d(doc::DocumentType::Part, QStringLiteral("Part1"));
    EXPECT_FALSE(d.transact(QStringLiteral("Nothing"), [](doc::PartModel&) { return true; }));
    EXPECT_FALSE(d.transact(QStringLiteral("Refused"), [](doc::PartModel& m) {
        m.importCount = 7;
        return false; // the change function can veto
    }));
    EXPECT_FALSE(d.canUndo());
    EXPECT_EQ(d.model().importCount, 0);
}

TEST(Document, UndoRestoresEditedBodies) {
    doc::PartModel initial;
    initial.bodies.push_back(box(QStringLiteral("A")));
    const QUuid id = initial.bodies.front().id;
    doc::Document d(doc::DocumentType::Part, QStringLiteral("Part1"), initial);
    EXPECT_FALSE(d.isModified());

    d.transact(QStringLiteral("Rename Body"), [&](doc::PartModel& m) {
        m.body(id)->name = QStringLiteral("B");
        return true;
    });
    d.transact(QStringLiteral("Hide Body"), [&](doc::PartModel& m) {
        m.body(id)->visible = false;
        return true;
    });
    d.transact(QStringLiteral("Change Units"), [&](doc::PartModel& m) {
        m.units.system = UnitSystem::IPS;
        return true;
    });
    EXPECT_EQ(d.model().units.system, UnitSystem::IPS);
    d.undo();
    EXPECT_EQ(d.model().units.system, UnitSystem::MMGS);
    d.undo();
    EXPECT_TRUE(d.model().body(id)->visible);
    d.undo();
    EXPECT_EQ(d.model().body(id)->name, QStringLiteral("A"));
    EXPECT_FALSE(d.isModified());
}

TEST(Document, SavedStateTracking) {
    doc::Document d(doc::DocumentType::Part, QStringLiteral("Part1"));
    d.transact(QStringLiteral("Import Body"), [](doc::PartModel& m) {
        m.bodies.push_back(box(QStringLiteral("Box")));
        return true;
    });
    d.markSaved();
    EXPECT_FALSE(d.isModified());
    d.undo();
    EXPECT_TRUE(d.isModified());
    d.redo();
    EXPECT_FALSE(d.isModified());
}
