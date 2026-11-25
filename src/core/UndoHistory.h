#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <utility>

namespace bl {

// Snapshot-based undo history (PLAN.md 6.3: "undo restores the document model, then regenerates").
// Each entry stores the model before and after one named transaction. Models are value types whose
// heavy parts (OCCT shapes) are reference counted, so snapshots are cheap.
template <class Model>
class UndoHistory {
public:
    explicit UndoHistory(size_t limit = 500) : m_limit(limit) {}

    void push(std::string label, Model before, Model after) {
        m_undo.push_back({std::move(label), std::move(before), std::move(after)});
        if (m_undo.size() > m_limit) {
            m_undo.pop_front();
            if (m_clean >= 0) {
                --m_clean; // the clean state may have fallen off the front (becomes -1 = unreachable)
            }
        }
        m_redo.clear();
        if (m_clean > static_cast<long>(m_undo.size()) - 1) {
            m_clean = kUnreachable; // clean state was on the discarded redo branch
        }
    }

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    std::string undoLabel() const { return m_undo.empty() ? std::string() : m_undo.back().label; }
    std::string redoLabel() const { return m_redo.empty() ? std::string() : m_redo.back().label; }

    // Returns the model to restore.
    const Model& undo() {
        m_redo.push_back(std::move(m_undo.back()));
        m_undo.pop_back();
        return m_redo.back().before;
    }
    const Model& redo() {
        m_undo.push_back(std::move(m_redo.back()));
        m_redo.pop_back();
        return m_undo.back().after;
    }

    // Saved-state tracking: the document is unmodified when the undo depth equals the clean index.
    void setClean() { m_clean = static_cast<long>(m_undo.size()); }
    bool isClean() const { return m_clean == static_cast<long>(m_undo.size()); }

    void clear() {
        m_undo.clear();
        m_redo.clear();
        m_clean = 0;
    }
    size_t undoCount() const { return m_undo.size(); }
    size_t redoCount() const { return m_redo.size(); }

private:
    static constexpr long kUnreachable = -1000000;
    struct Entry {
        std::string label;
        Model before;
        Model after;
    };
    std::deque<Entry> m_undo;
    std::deque<Entry> m_redo;
    size_t m_limit;
    long m_clean = 0;
};

} // namespace bl
