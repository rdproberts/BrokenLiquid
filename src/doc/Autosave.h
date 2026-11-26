#pragma once

// Autosave and crash recovery (PLAN.md 8.3 step 7, 9.1).
//
// Each running BrokenLiquid owns a session folder in the autosave directory:
//   <root>/<session>.lock       held while the session runs (QLockFile: pid, app name, host)
//   <root>/<session>/<doc>.blpart  last autosaved copy of a modified document
//   <root>/<session>/<doc>.json    its title, original file and time (written after the copy)
// A normal exit removes both. After a crash or power cut they stay behind; the next start finds
// folders whose lock it can take (the owner is gone) and offers to recover their documents.

#include "doc/PartModel.h"

#include <QDateTime>
#include <QString>
#include <QUuid>

#include <map>
#include <memory>
#include <vector>

class QLockFile;

namespace bl::doc {

struct RecoverableDocument {
    QString sessionDir;   // folder it was found in
    QString file;         // the autosaved .blpart
    QString title;
    QString originalPath; // file it was opened from or saved to; empty if never saved
    QDateTime saved;      // when it was autosaved (UTC)
};

class AutosaveSession {
public:
    explicit AutosaveSession(const QString& rootDir);
    ~AutosaveSession(); // a normal exit: removes the session folder and its lock
    AutosaveSession(const AutosaveSession&) = delete;
    AutosaveSession& operator=(const AutosaveSession&) = delete;

    bool isValid() const { return m_lock != nullptr; }
    QString dir() const { return m_dir; }

    // Writes `model` unless revision `revision` of document `id` is already written.
    bool write(const QUuid& id, const PartModel& model, const QString& title, const QString& originalPath,
               quint64 revision, QString* error = nullptr);
    // Drops the autosave of `id` (saved, closed or no longer modified).
    void remove(const QUuid& id);
    std::vector<QUuid> written() const;

    // Documents left behind by sessions that did not exit normally. Their folders stay locked by
    // this session until discard() or releaseRecoverable(), so two windows never recover the same file.
    std::vector<RecoverableDocument> findRecoverable();
    // Deletes a recovered or unwanted document; the dead session goes once it is empty.
    void discard(const RecoverableDocument& document);
    // Leaves the remaining documents for the next start ("ask me later").
    void releaseRecoverable();

    // Tests: leaves the folder behind as a crash would (the lock is released, the files stay).
    void simulateCrash();

private:
    QString pathFor(const QUuid& id, const char* suffix) const;

    QString m_root;
    QString m_dir;
    std::unique_ptr<QLockFile> m_lock;
    std::map<QUuid, quint64> m_written; // document -> revision on disk
    std::map<QString, std::unique_ptr<QLockFile>> m_dead; // dead session folder -> its lock, now ours
};

} // namespace bl::doc
