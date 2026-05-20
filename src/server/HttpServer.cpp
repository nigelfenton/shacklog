#include "HttpServer.h"

#include <QByteArray>
#include <QDebug>
#include <QTcpSocket>

namespace ShackLog::Server {

namespace {

// Build a minimal HTTP/1.1 response.  No keep-alive, no chunked, no TLS —
// the spike asks for nothing more.
QByteArray buildResponse(int status,
                         const QByteArray& reason,
                         const QByteArray& contentType,
                         const QByteArray& body)
{
    QByteArray r;
    r.reserve(128 + body.size());
    r += "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    r += "Content-Type: " + contentType + "\r\n";
    r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    r += "Connection: close\r\n";
    r += "Server: ShackLog/0.1 (spike)\r\n";
    r += "\r\n";
    r += body;
    return r;
}

// Parse just enough of the request line to dispatch on method + path.
// Returns (method, path).  Body and headers are ignored for the spike.
static std::pair<QByteArray, QByteArray>
parseRequestLine(const QByteArray& raw)
{
    const int eol = raw.indexOf("\r\n");
    if (eol < 0) return {{}, {}};
    const QByteArray line = raw.left(eol);
    const int sp1 = line.indexOf(' ');
    if (sp1 < 0) return {{}, {}};
    const int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp2 < 0) return {{}, {}};
    return { line.mid(0, sp1), line.mid(sp1 + 1, sp2 - sp1 - 1) };
}

} // namespace

HttpServer::HttpServer(QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &HttpServer::onNewConnection);
}

bool HttpServer::start(quint16 port)
{
    if (!m_server->listen(QHostAddress::Any, port)) {
        qWarning() << "HttpServer: bind to port" << port << "failed:"
                   << m_server->errorString();
        return false;
    }
    qInfo() << "HttpServer: listening on port" << m_server->serverPort();
    return true;
}

quint16 HttpServer::port() const            { return m_server->serverPort(); }
bool    HttpServer::isListening() const     { return m_server->isListening(); }

void HttpServer::onNewConnection()
{
    while (auto* client = m_server->nextPendingConnection()) {
        // Auto-cleanup once disconnected.
        connect(client, &QTcpSocket::disconnected,
                client, &QTcpSocket::deleteLater);

        // Read the first packet; we trust the request line is in it for
        // the spike.  Real HTTP would need readyRead-loop accumulation.
        connect(client, &QTcpSocket::readyRead, client, [client] {
            const QByteArray raw = client->readAll();
            const auto [method, path] = parseRequestLine(raw);

            QByteArray response;
            if (method == "GET" && path == "/") {
                response = buildResponse(
                    200, "OK", "text/plain; charset=utf-8",
                    "ShackLog Server alive\n");
            } else if (method == "GET" && path == "/api/state") {
                response = buildResponse(
                    200, "OK", "application/json",
                    R"({"status":"alive","version":"0.1.0-spike","phase":0})");
            } else {
                response = buildResponse(
                    404, "Not Found", "text/plain; charset=utf-8",
                    "Not Found\n");
            }
            client->write(response);
            client->disconnectFromHost();
        });
    }
}

} // namespace ShackLog::Server
