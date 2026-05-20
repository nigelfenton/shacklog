#include "N3fjpServer.h"

#include <QByteArray>
#include <QDebug>
#include <QStringConverter>
#include <QTcpSocket>

namespace ShackLog::Server {

namespace {

constexpr const char* kProgramName     = "ShackLog";
constexpr const char* kProgramVersion  = "0.1.0-spike";
constexpr const char* kCompatApiLabel  = "N3FJP-compat-2.2";

// Strip <CMD>...</CMD> envelope and return the inner content, or empty
// QString if the frame is malformed.
QString unwrapCmd(const QString& frame)
{
    QString s = frame.trimmed();
    if (!s.startsWith(QLatin1String("<CMD>"), Qt::CaseInsensitive) ||
        !s.endsWith(QLatin1String("</CMD>"),  Qt::CaseInsensitive))
        return {};
    return s.mid(5, s.size() - 5 - 6);  // strip 5 open + 6 close chars
}

// Extract the command tag (first <TAG>) from the unwrapped content.
QString commandTag(const QString& inner)
{
    const int lt = inner.indexOf('<');
    const int gt = inner.indexOf('>', lt + 1);
    if (lt < 0 || gt < 0) return {};
    return inner.mid(lt + 1, gt - lt - 1).toUpper();
}

} // namespace

N3fjpServer::N3fjpServer(QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &N3fjpServer::onNewConnection);
}

bool N3fjpServer::start(quint16 port)
{
    if (!m_server->listen(QHostAddress::Any, port)) {
        qWarning() << "N3fjpServer: bind to port" << port << "failed:"
                   << m_server->errorString();
        return false;
    }
    qInfo() << "N3fjpServer: listening on port" << m_server->serverPort();
    return true;
}

quint16 N3fjpServer::port() const           { return m_server->serverPort(); }
bool    N3fjpServer::isListening() const    { return m_server->isListening(); }

void N3fjpServer::onNewConnection()
{
    while (auto* client = m_server->nextPendingConnection()) {
        qInfo() << "N3fjpServer: client connected from"
                << client->peerAddress().toString();

        connect(client, &QTcpSocket::readyRead,
                this,   &N3fjpServer::onSocketReadyRead);
        connect(client, &QTcpSocket::disconnected,
                client, &QTcpSocket::deleteLater);
    }
}

void N3fjpServer::onSocketReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // N3FJP frames are CR+LF-terminated.  Accumulate until we have a
    // complete frame; handle one at a time.
    static thread_local QHash<QTcpSocket*, QByteArray> buffers;
    QByteArray& buf = buffers[socket];
    buf += socket->readAll();

    for (;;) {
        const int eol = buf.indexOf("\r\n");
        if (eol < 0) break;
        const QByteArray frameBytes = buf.left(eol);
        buf.remove(0, eol + 2);

        // A bare CR+LF is the documented disconnect signal.
        if (frameBytes.isEmpty()) {
            qInfo() << "N3fjpServer: client requested disconnect";
            socket->disconnectFromHost();
            buffers.remove(socket);
            return;
        }

        const QString frame = QString::fromUtf8(frameBytes);
        const QString response = handleFrame(frame);
        if (!response.isEmpty()) {
            socket->write(response.toUtf8() + "\r\n");
        }
    }
}

QString N3fjpServer::handleFrame(const QString& frame) const
{
    const QString inner = unwrapCmd(frame);
    if (inner.isEmpty()) {
        qDebug() << "N3fjpServer: malformed frame ignored:" << frame;
        return {};
    }
    const QString cmd = commandTag(inner);

    if (cmd == QLatin1String("PROGRAM")) {
        // <CMD><PROGRAMRESPONSE><PGM>ShackLog</PGM><VER>...</VER><APIVER>N3FJP-compat-2.2</APIVER></CMD>
        // Identifies as ShackLog (NEVER as N3FJP) per design doc §13.
        return QStringLiteral(
            "<CMD><PROGRAMRESPONSE>"
            "<PGM>%1</PGM>"
            "<VER>%2</VER>"
            "<APIVER>%3</APIVER>"
            "</CMD>")
            .arg(QLatin1String(kProgramName),
                 QLatin1String(kProgramVersion),
                 QLatin1String(kCompatApiLabel));
    }

    if (cmd == QLatin1String("APIVER")) {
        return QStringLiteral(
            "<CMD><APIVERRESPONSE><APIVER>%1</APIVER></CMD>")
            .arg(QLatin1String(kCompatApiLabel));
    }

    // Unsupported commands — politely tell the client so it doesn't hang.
    qDebug() << "N3fjpServer: unsupported command (spike):" << cmd;
    return QStringLiteral(
        "<CMD><UNSUPPORTED><COMMAND>%1</COMMAND>"
        "<REASON>spike build; only PROGRAM and APIVER implemented</REASON>"
        "</CMD>")
        .arg(cmd);
}

} // namespace ShackLog::Server
