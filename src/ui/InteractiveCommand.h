#pragma once

#include "core/Units.h"

#include <QObject>
#include <QString>

namespace bl::doc {
class Document;
}

namespace bl::ui {

class DocumentView;
class PropertyPanel;

// What a running command can reach. Implemented by the main window.
class CommandHost {
public:
    virtual ~CommandHost() = default;
    virtual PropertyPanel& panel() = 0;
    virtual DocumentView* activeView() = 0;
    virtual doc::Document* activeDocument() = 0;
    virtual Units units() const = 0;
    virtual void showStatus(const QString& text, int ms = 4000) = 0;
};

// A command that takes input in the Property Panel (PLAN.md 7.3) and finishes with OK or Cancel.
// Life cycle: start() -> selectionChanged()* / unitsChanged()* -> accept() or cancel().
class InteractiveCommand : public QObject {
    Q_OBJECT
public:
    explicit InteractiveCommand(CommandHost& host) : m_host(host) {}

    virtual QString id() const = 0;
    virtual QString title() const = 0;
    virtual QString iconName() const = 0;
    virtual bool confirms() const { return true; } // false: tool window with a Close button only

    virtual void start() = 0;
    virtual void selectionChanged() {}
    virtual void unitsChanged() {}
    // OK pressed: apply. Return false to keep the command open (e.g. missing input).
    virtual bool accept() { return true; }
    // Cancel pressed or another command started: undo any preview.
    virtual void cancel() {}

signals:
    void finishRequested(); // the command wants to close itself (as if OK was pressed)

protected:
    CommandHost& m_host;
};

} // namespace bl::ui
