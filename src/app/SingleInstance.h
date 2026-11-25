#pragma once

#include <QObject>
#include <QStringList>

class QLocalServer;

namespace bl::app {

// One BrokenLiquid window per user session (PLAN.md 8.3). A second launch forwards its file
// arguments to the running instance and exits.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);

    // Returns true if another instance received `files` (caller should exit).
    bool forwardToRunningInstance(const QStringList& files);
    // Starts listening for later launches.
    void listen();

signals:
    void filesReceived(const QStringList& files);

private:
    QString m_name;
    QLocalServer* m_server = nullptr;
};

} // namespace bl::app
