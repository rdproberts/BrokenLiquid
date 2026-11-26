#include "doc/Autosave.h"

#include "doc/PartFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace bl::doc {
namespace {

std::unique_ptr<QLockFile> makeLock(const QString& path) {
    auto lock = std::make_unique<QLockFile>(path);
    lock->setStaleLockTime(0); // stale only when its process is gone, never because of age
    return lock;
}

// Everything that belongs to one autosaved document, given the path without the extension.
void removeEntry(const QString& base) {
    for (const char* suffix : {".blpart", ".blpart.bak", ".blpart.saving", ".json"}) {
        QFile::remove(base + QLatin1String(suffix));
    }
}

bool hasEntries(const QString& dir) {
    return !QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty();
}

} // namespace

AutosaveSession::AutosaveSession(const QString& rootDir) : m_root(QDir(rootDir).absolutePath()) {
    const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_dir = QDir(m_root).filePath(name);
    if (!QDir().mkpath(m_root)) {
        spdlog::warn("autosave: cannot create {}", m_root.toStdString());
        return;
    }
    // The lock comes first: a folder without a live lock is a dead session to everyone else.
    auto lock = makeLock(m_dir + QStringLiteral(".lock"));
    if (!lock->tryLock(0) || !QDir().mkpath(m_dir)) {
        spdlog::warn("autosave: cannot start a session in {}", m_root.toStdString());
        return;
    }
    m_lock = std::move(lock);
}

AutosaveSession::~AutosaveSession() {
    m_dead.clear();
    if (m_lock) {
        QDir(m_dir).removeRecursively();
        m_lock.reset(); // unlocks and deletes the lock file
    }
}

QString AutosaveSession::pathFor(const QUuid& id, const char* suffix) const {
    return QDir(m_dir).filePath(id.toString(QUuid::WithoutBraces) + QLatin1String(suffix));
}

bool AutosaveSession::write(const QUuid& id, const PartModel& model, const QString& title,
                            const QString& originalPath, quint64 revision, QString* error) {
    if (!m_lock) {
        return false;
    }
    if (auto it = m_written.find(id); it != m_written.end() && it->second == revision) {
        return true;
    }
    if (!savePart(model, pathFor(id, ".blpart"), error)) {
        return false;
    }
    // The sidecar is written last: when it exists, the copy beside it is complete.
    const QJsonObject info{{QStringLiteral("title"), title},
                           {QStringLiteral("originalPath"), originalPath},
                           {QStringLiteral("saved"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    QSaveFile file(pathFor(id, ".json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(info).toJson()) < 0 || !file.commit()) {
        if (error) {
            *error = QObject::tr("Cannot write %1.").arg(file.fileName());
        }
        return false;
    }
    m_written[id] = revision;
    spdlog::info("autosaved {} (revision {})", title.toStdString(), revision);
    return true;
}

void AutosaveSession::remove(const QUuid& id) {
    if (m_written.erase(id) > 0 || QFileInfo::exists(pathFor(id, ".json"))) {
        removeEntry(pathFor(id, ""));
    }
}

std::vector<QUuid> AutosaveSession::written() const {
    std::vector<QUuid> ids;
    for (const auto& [id, revision] : m_written) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<RecoverableDocument> AutosaveSession::findRecoverable() {
    std::vector<RecoverableDocument> found;
    QDir root(m_root);
    for (const QFileInfo& info : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = info.absoluteFilePath();
        if (dir == m_dir) {
            continue;
        }
        if (!m_dead.contains(dir)) {
            auto lock = makeLock(dir + QStringLiteral(".lock"));
            if (!lock->tryLock(0)) {
                continue; // its window is still open
            }
            if (!hasEntries(dir)) {
                QDir(dir).removeRecursively(); // ended before anything was autosaved
                continue;
            }
            m_dead.emplace(dir, std::move(lock));
        }
        for (const QFileInfo& entry : QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
            QFile file(entry.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
            const QString base = QDir(dir).filePath(entry.completeBaseName());
            RecoverableDocument doc;
            doc.sessionDir = dir;
            // A crash in the middle of an autosave leaves the previous copy as .bak.
            doc.file = QFileInfo::exists(base + QStringLiteral(".blpart")) ? base + QStringLiteral(".blpart")
                                                                           : base + QStringLiteral(".blpart.bak");
            if (!QFileInfo::exists(doc.file)) {
                continue;
            }
            doc.title = json.value(QStringLiteral("title")).toString(entry.completeBaseName());
            doc.originalPath = json.value(QStringLiteral("originalPath")).toString();
            doc.saved = QDateTime::fromString(json.value(QStringLiteral("saved")).toString(), Qt::ISODateWithMs);
            found.push_back(std::move(doc));
        }
    }
    // Lock files whose folder is gone (a crash during start-up).
    for (const QFileInfo& info : root.entryInfoList({QStringLiteral("*.lock")}, QDir::Files)) {
        const QString folder = info.absolutePath() + QLatin1Char('/') + info.completeBaseName();
        if (!QFileInfo::exists(folder)) {
            auto lock = makeLock(info.absoluteFilePath());
            if (lock->tryLock(0)) {
                lock->unlock();
            }
        }
    }
    std::sort(found.begin(), found.end(),
              [](const RecoverableDocument& a, const RecoverableDocument& b) { return a.saved > b.saved; });
    return found;
}

void AutosaveSession::discard(const RecoverableDocument& document) {
    QString base = document.file;
    for (const char* suffix : {".blpart.bak", ".blpart"}) {
        if (base.endsWith(QLatin1String(suffix))) {
            base.chop(static_cast<int>(qstrlen(suffix)));
            break;
        }
    }
    removeEntry(base);
    if (!hasEntries(document.sessionDir)) {
        QDir(document.sessionDir).removeRecursively();
        m_dead.erase(document.sessionDir); // unlocks: the lock file goes too
    }
}

void AutosaveSession::releaseRecoverable() { m_dead.clear(); }

void AutosaveSession::simulateCrash() {
    m_dead.clear();
    m_lock.reset();
    m_written.clear();
}

} // namespace bl::doc
