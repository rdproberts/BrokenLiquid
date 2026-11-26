#include "doc/Document.h"

#include "doc/Rebuild.h"

#include <spdlog/spdlog.h>

namespace bl::doc {
namespace {

// Cheap structural comparison: did the transaction change anything?
bool sameModel(const PartModel& a, const PartModel& b) {
    if (a.units != b.units || a.importCount != b.importCount || a.bodies.size() != b.bodies.size() ||
        a.sketchCount != b.sketchCount || a.sketches.size() != b.sketches.size() || a.rollback != b.rollback ||
        a.extrudeCount != b.extrudeCount || a.cutCount != b.cutCount || a.features.size() != b.features.size()) {
        return false;
    }
    for (size_t i = 0; i < a.features.size(); ++i) {
        if (!a.features[i].sameInput(b.features[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < a.sketches.size(); ++i) {
        const SketchFeature& x = a.sketches[i];
        const SketchFeature& y = b.sketches[i];
        if (x.id != y.id || x.name != y.name || x.visible != y.visible || x.planeName != y.planeName ||
            x.status != y.status || !(x.sketch == y.sketch) || x.face != y.face ||
            !x.plane.Location().IsEqual(y.plane.Location(), 0.0) || !x.plane.Direction().IsEqual(y.plane.Direction(), 0.0) ||
            !x.plane.XDirection().IsEqual(y.plane.XDirection(), 0.0)) {
            return false;
        }
    }
    for (size_t i = 0; i < a.bodies.size(); ++i) {
        const Body& x = a.bodies[i];
        const Body& y = b.bodies[i];
        if (x.id != y.id || x.name != y.name || x.visible != y.visible || x.color != y.color ||
            !x.shape.IsSame(y.shape) || x.sourceFile != y.sourceFile) {
            return false;
        }
    }
    return true;
}

} // namespace

Document::Document(DocumentType type, QString title, PartModel initial, QObject* parent)
    : QObject(parent), m_type(type), m_title(std::move(title)), m_model(std::move(initial)) {
    rebuild(m_model);
}

void Document::setTitle(const QString& title) {
    if (title != m_title) {
        m_title = title;
        emit titleChanged(title);
    }
}

bool Document::transact(const QString& label, const std::function<bool(PartModel&)>& change) {
    PartModel next = m_model;
    if (!change(next)) {
        return false;
    }
    rebuild(next, &m_model);
    if (sameModel(next, m_model)) {
        return false;
    }
    spdlog::debug("transaction '{}' on {}", label.toStdString(), m_title.toStdString());
    m_history.push(label.toStdString(), m_model, next);
    setModel(std::move(next));
    emit historyChanged();
    return true;
}

void Document::undo() {
    if (!canUndo()) {
        return;
    }
    setModel(m_history.undo());
    emit historyChanged();
}

void Document::redo() {
    if (!canRedo()) {
        return;
    }
    setModel(m_history.redo());
    emit historyChanged();
}

void Document::regenerate() {
    rebuild(m_model);
    ++m_revision;
    emit modelChanged();
}

void Document::markSaved() {
    m_history.setClean();
    emit historyChanged();
}

void Document::markRecovered() {
    m_history.setDirty();
    emit historyChanged();
}

void Document::setModel(PartModel model) {
    m_model = std::move(model);
    ++m_revision;
    emit modelChanged();
}

} // namespace bl::doc
