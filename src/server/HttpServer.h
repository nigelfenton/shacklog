#pragma once

// Phase-0 spike HTTP server for shacklog-server.
//
// Built on plain QTcpServer with a tiny request parser — no QHttpServer, no
// cpp-httplib.  This is deliberate for the spike: prove three-surface
// coexistence (HTTP / N3FJP / Qt event loop) without adding a dependency
// that may or may not be available on every build host.  Phase 1 will swap
// this out for either QHttpServer (if installed) or cpp-httplib (header-
// only fallback).
//
// What this implements:
//   GET /            -> 200, "ShackLog Server alive\n"
//   GET /api/state   -> 200, minimal JSON {"status":"alive","version":"..."}
//   anything else    -> 404
//
// Connections are short-lived: one request, one response, close.  HTTP/1.1
// keep-alive is not honoured.  Sufficient for the spike.

#include <QObject>
#include <QTcpServer>

namespace ShackLog::Server {

class HttpServer : public QObject {
    Q_OBJECT

public:
    explicit HttpServer(QObject* parent = nullptr);

    // Start listening on the given port.  Returns false on bind failure
    // (port in use, permission denied, etc.).
    bool start(quint16 port = 8080);

    quint16 port() const;
    bool    isListening() const;

private slots:
    void onNewConnection();

private:
    QTcpServer* m_server;
};

} // namespace ShackLog::Server
