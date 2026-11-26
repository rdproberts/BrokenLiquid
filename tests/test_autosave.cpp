#include "doc/Autosave.h"
#include "doc/Document.h"
#include "doc/PartFile.h"

#include <BRepPrimAPI_MakeBox.hxx>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>

using namespace bl;

namespace {

doc::PartModel boxModel(const QString& name) {
    doc::PartModel m;
    doc::addImport(m, BRepPrimAPI_MakeBox(10, 20, 30).Shape(), name, QString());
    return m;
}

int entries(const QString& dir) { return static_cast<int>(QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files).size()); }

} // namespace

TEST(Autosave, NormalExitLeavesNothing) {
    QTemporaryDir root;
    QString dir;
    {
        doc::AutosaveSession session(root.path());
        ASSERT_TRUE(session.isValid());
        dir = session.dir();
        ASSERT_TRUE(session.write(QUuid::createUuid(), boxModel(QStringLiteral("Box")), QStringLiteral("Part1"), {}, 1));
        EXPECT_EQ(entries(dir), 1);
    }
    EXPECT_FALSE(QFileInfo::exists(dir));
    EXPECT_TRUE(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

TEST(Autosave, WritesOnlyNewRevisionsAndRemoves) {
    QTemporaryDir root;
    doc::AutosaveSession session(root.path());
    const QUuid id = QUuid::createUuid();
    ASSERT_TRUE(session.write(id, boxModel(QStringLiteral("Box")), QStringLiteral("Part1"), {}, 3));
    const QString file = QDir(session.dir()).filePath(id.toString(QUuid::WithoutBraces) + QStringLiteral(".blpart"));
    ASSERT_TRUE(QFile::remove(file));
    ASSERT_TRUE(session.write(id, boxModel(QStringLiteral("Box")), QStringLiteral("Part1"), {}, 3));
    EXPECT_FALSE(QFileInfo::exists(file)); // same revision: not written again
    ASSERT_TRUE(session.write(id, boxModel(QStringLiteral("Box")), QStringLiteral("Part1"), {}, 4));
    EXPECT_TRUE(QFileInfo::exists(file));
    ASSERT_EQ(session.written().size(), 1u);

    session.remove(id);
    EXPECT_TRUE(session.written().empty());
    EXPECT_EQ(entries(session.dir()), 0);
    EXPECT_FALSE(QFileInfo::exists(file));
}

TEST(Autosave, RunningSessionIsNotRecovered) {
    QTemporaryDir root;
    doc::AutosaveSession first(root.path());
    ASSERT_TRUE(first.write(QUuid::createUuid(), boxModel(QStringLiteral("Box")), QStringLiteral("Part1"), {}, 1));
    doc::AutosaveSession second(root.path());
    EXPECT_TRUE(second.findRecoverable().empty());
}

TEST(Autosave, CrashedSessionIsRecoveredOnce) {
    QTemporaryDir root;
    const QString original = QDir(root.path()).filePath(QStringLiteral("bracket.blpart"));
    {
        doc::AutosaveSession crashed(root.path());
        ASSERT_TRUE(crashed.write(QUuid::createUuid(), boxModel(QStringLiteral("Box")), QStringLiteral("bracket"),
                                  original, 7));
        ASSERT_TRUE(crashed.write(QUuid::createUuid(), boxModel(QStringLiteral("Other")), QStringLiteral("Part2"), {}, 2));
        crashed.simulateCrash();
    }

    doc::AutosaveSession next(root.path());
    std::vector<doc::RecoverableDocument> found = next.findRecoverable();
    ASSERT_EQ(found.size(), 2u);
    const auto bracket = std::find_if(found.begin(), found.end(),
                                      [](const doc::RecoverableDocument& r) { return r.title == QLatin1String("bracket"); });
    ASSERT_NE(bracket, found.end());
    EXPECT_EQ(bracket->originalPath, original);
    EXPECT_TRUE(bracket->saved.isValid());

    // While `next` holds the dead session, another window does not see it.
    doc::AutosaveSession other(root.path());
    EXPECT_TRUE(other.findRecoverable().empty());

    // Recover: the copy loads as a part and counts as modified.
    doc::LoadedPart loaded = doc::loadPart(bracket->file);
    ASSERT_TRUE(loaded.ok) << loaded.error.toStdString();
    doc::Document d(doc::DocumentType::Part, bracket->title, std::move(loaded.model));
    EXPECT_FALSE(d.isModified());
    d.markRecovered();
    EXPECT_TRUE(d.isModified());
    ASSERT_EQ(d.model().bodies.size(), 1u);
    EXPECT_EQ(d.model().bodies.front().name, QStringLiteral("Box"));

    const QString deadDir = bracket->sessionDir;
    next.discard(*bracket);
    EXPECT_TRUE(QFileInfo::exists(deadDir)); // Part2 is still in it
    next.releaseRecoverable();               // "Ask Later"

    // The next start sees Part2 only; discarding it removes the dead session completely.
    found = other.findRecoverable();
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found.front().title, QStringLiteral("Part2"));
    other.discard(found.front());
    EXPECT_FALSE(QFileInfo::exists(deadDir));
    EXPECT_FALSE(QFileInfo::exists(deadDir + QStringLiteral(".lock")));
    EXPECT_TRUE(other.findRecoverable().empty());
}

TEST(Autosave, EmptyCrashedSessionIsCleanedUp) {
    QTemporaryDir root;
    QString dir;
    {
        doc::AutosaveSession crashed(root.path());
        dir = crashed.dir();
        crashed.simulateCrash();
    }
    ASSERT_TRUE(QFileInfo::exists(dir));
    doc::AutosaveSession next(root.path());
    EXPECT_TRUE(next.findRecoverable().empty());
    EXPECT_FALSE(QFileInfo::exists(dir));
}

TEST(Document, RevisionFollowsChanges) {
    doc::Document d(doc::DocumentType::Part, QStringLiteral("Part1"));
    const quint64 start = d.revision();
    d.transact(QStringLiteral("Import Body"), [](doc::PartModel& m) {
        doc::addImport(m, BRepPrimAPI_MakeBox(1, 2, 3).Shape(), QStringLiteral("Box"), QString());
        return true;
    });
    EXPECT_GT(d.revision(), start);
    const quint64 edited = d.revision();
    d.undo();
    EXPECT_GT(d.revision(), edited);
    EXPECT_NE(d.id(), doc::Document(doc::DocumentType::Part, QStringLiteral("Part2")).id());
}
