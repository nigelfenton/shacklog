#include "HttpServer.h"

#include "LogbookModel.h"
#include "Qso.h"

#include <QByteArray>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

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
    r += "Server: ShackLog/0.1 (phase-1a)\r\n";
    r += "\r\n";
    r += body;
    return r;
}

// Parse just enough of the request line to dispatch on method + path.
// Returns (method, fullPath_with_query).
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

// Serialise a Qso to JSON.  Field names follow ADIF lowercase to match
// the column naming convention in LogbookModel.
QJsonObject qsoToJson(const Qso& q)
{
    QJsonObject o;
    o["id"]            = static_cast<qint64>(q.id);
    o["call"]          = q.call;
    o["qso_date"]      = q.qsoDate;
    o["time_on"]       = q.timeOn;
    if (!q.timeOff.isEmpty())     o["time_off"]     = q.timeOff;
    o["band"]          = q.band;
    o["mode"]          = q.mode;
    if (q.freq > 0.0)             o["freq"]         = q.freq;
    if (!q.submode.isEmpty())     o["submode"]      = q.submode;
    if (!q.rstSent.isEmpty())     o["rst_sent"]     = q.rstSent;
    if (!q.rstRcvd.isEmpty())     o["rst_rcvd"]     = q.rstRcvd;
    if (!q.name.isEmpty())        o["name"]         = q.name;
    if (!q.qth.isEmpty())         o["qth"]          = q.qth;
    if (!q.gridsquare.isEmpty())  o["gridsquare"]   = q.gridsquare;
    if (q.dxcc)                   o["dxcc"]         = q.dxcc;
    if (!q.country.isEmpty())     o["country"]      = q.country;
    if (!q.state.isEmpty())       o["state"]        = q.state;
    if (!q.contestId.isEmpty())   o["contest_id"]   = q.contestId;
    if (!q.myCall.isEmpty())      o["my_call"]      = q.myCall;
    if (q.txPwr > 0.0)            o["tx_pwr"]       = q.txPwr;
    if (!q.comment.isEmpty())     o["comment"]      = q.comment;
    if (!q.notes.isEmpty())       o["notes"]        = q.notes;
    if (!q.createdAt.isEmpty())   o["created_at"]   = q.createdAt;
    if (!q.updatedAt.isEmpty())   o["updated_at"]   = q.updatedAt;
    return o;
}

} // namespace

HttpServer::HttpServer(LogbookModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
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
        connect(client, &QTcpSocket::disconnected,
                client, &QTcpSocket::deleteLater);

        connect(client, &QTcpSocket::readyRead, client, [client, this] {
            const QByteArray raw = client->readAll();
            const auto [method, fullPath] = parseRequestLine(raw);

            // Split path and query.  QUrl handles the parsing for us.
            const QUrl url(QString::fromUtf8(fullPath));
            const QString path = url.path();
            const QUrlQuery qs(url);

            QByteArray response;

            if (method == "GET" && path == "/") {
                response = buildResponse(
                    200, "OK", "text/plain; charset=utf-8",
                    "ShackLog Server alive\n");
            }
            else if (method == "GET" && path == "/api/state") {
                QJsonObject state;
                state["status"]   = "alive";
                state["version"]  = "0.1.0-phase1a";
                state["phase"]    = QStringLiteral("1a");
                if (m_model && m_model->isOpen()) {
                    state["db_path"]   = m_model->databasePath();
                    state["qso_count"] = m_model->countQsos();
                } else {
                    state["db_path"]   = QJsonValue();
                    state["qso_count"] = 0;
                    state["db_error"]  = "logbook not open";
                }
                response = buildResponse(
                    200, "OK", "application/json",
                    QJsonDocument(state).toJson(QJsonDocument::Compact));
            }
            else if (method == "GET" && path == "/api/qsos") {
                if (!m_model || !m_model->isOpen()) {
                    response = buildResponse(
                        503, "Service Unavailable", "application/json",
                        R"({"error":"logbook not open"})");
                } else {
                    LogbookFilter f;
                    bool ok = false;
                    const int lim = qs.queryItemValue("limit").toInt(&ok);
                    f.limit = (ok && lim > 0) ? qMin(lim, 1000) : 50;

                    const auto rows = m_model->queryQsos(f);
                    QJsonArray arr;
                    for (const auto& q : rows) arr.append(qsoToJson(q));
                    QJsonObject body;
                    body["count"] = static_cast<int>(rows.size());
                    body["limit"] = f.limit;
                    body["qsos"]  = arr;
                    response = buildResponse(
                        200, "OK", "application/json",
                        QJsonDocument(body).toJson(QJsonDocument::Compact));
                }
            }
            else {
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
