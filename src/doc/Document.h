#pragma once

#include "core/UndoHistory.h"
#include "doc/PartModel.h"

#include <QObject>
#include <QString>
#include <QUuid>

#include <functional>

namespace bl::doc {

enum class DocumentType { Part, Assembly, Drawing };

// An open document. All edits go through named transactions so they can be undone
// (PLAN.md 6.3). Phase 1 supports part documents holding imported bodies.
class Document : public QObject {
    Q_OBJECT
public:
    Document(DocumentType type, QString title, PartModel initial = {}, QObject* parent = nullptr);

    DocumentType type() const { return m_type; }
    // Identifies the open document for this session (autosave file names); not saved.
    const QUuid& id() const { return m_id; }
    // Goes up with every change to the model (edits, undo, redo, rebuild).
    quint64 revision() const { return m_revision; }
    QString title() const { return m_title; }
    void setTitle(const QString& title);
    // File the document was opened from or last saved to (a .blpart, or the STEP it was imported from).
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }

    const PartModel& model() const { return m_model; }

    // Runs `change` on a copy of the model. If it returns true and the model differs, the copy
    // becomes current and an undo entry named `label` is recorded. Returns whether it applied.
    bool transact(const QString& label, const std::function<bool(PartModel&)>& change);

    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    QString undoLabel() const { return QString::fromStdString(m_history.undoLabel()); }
    QString redoLabel() const { return QString::fromStdString(m_history.redoLabel()); }
    void undo();
    void redo();

    bool isModified() const { return !m_history.isClean(); }
    // Rebuilds every feature from scratch (Edit > Rebuild); no undo step.
    void regenerate();
    void markSaved();
    // The model did not come from its file as is (recovered from an autosave): it counts as modified.
    void markRecovered();

    // A fresh, unchanged document (no bodies, no history): opening a file may reuse it.
    bool isPristine() const { return m_model.features.empty() && m_model.sketches.empty() && !canUndo() && !canRedo(); }

signals:
    void modelChanged();
    void historyChanged();
    void titleChanged(const QString& title);

private:
    void setModel(PartModel model);

    QUuid m_id = QUuid::createUuid();
    DocumentType m_type;
    QString m_title;
    QString m_filePath;
    PartModel m_model;
    UndoHistory<PartModel> m_history;
    quint64 m_revision = 0;
};

} // namespace bl::doc
