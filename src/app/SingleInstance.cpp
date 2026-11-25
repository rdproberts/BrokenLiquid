#include "app/SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>

namespace bl::app {

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {
    m_name = QStringLiteral("BrokenLiquid-%1").arg(qEnvironmentVariable("USERNAME", QStringLiteral("user")));
}

bool SingleInstance::forwardToRunningInstance(const QStringList& files) {
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(300)) {
        return false;
    }
    socket.write(files.join(QLatin1Char('\n')).toUtf8() + "\n\x04");
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

void SingleInstance::listen() {
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_name)) {
        QLocalServer::removeServer(m_name); // stale socket after a crash
        m_server->listen(m_name);
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                const QByteArray data = socket->readAll();
                QStringList files;
                for (const QByteArray& line : data.split('\n')) {
                    const QString f = QString::fromUtf8(line).trimmed();
                    if (!f.isEmpty() && f != QStringLiteral("\x04")) {
                        files << f;
                    }
                }
                emit filesReceived(files);
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}

} // namespace bl::app
