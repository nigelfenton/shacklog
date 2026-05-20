#pragma once

// Phase-0 spike N3FJP-protocol-compatible TCP server.
//
// Listens on port 1100 (the N3FJP default) and parses <CMD>...</CMD>
// envelopes.  For the spike we implement just ONE command — <PROGRAM> —
// returning a correctly-shaped <PROGRAMRESPONSE> that explicitly identifies
// this as "ShackLog" with an "N3FJP-compat" API version label (never as
// N3FJP itself — see design doc §13 ethical posture).
//
// Phase 1 will extend the dispatcher to the FD-minimum command subset
// (<APIVER>, <READBMF>, <UPDATEANDLOG>, <SETUPDATESTATE>, <ENTEREVENT> push,
// etc., per design doc §6.2).

#include <QObject>
#include <QString>
#include <QTcpServer>

class QTcpSocket;

namespace ShackLog::Server {

class N3fjpServer : public QObject {
    Q_OBJECT

public:
    explicit N3fjpServer(QObject* parent = nullptr);

    bool start(quint16 port = 1100);

    quint16 port() const;
    bool    isListening() const;

private slots:
    void onNewConnection();
    void onSocketReadyRead();

private:
    // Parse + dispatch one CR+LF-terminated frame.  Returns the response
    // to send (empty == no response for this frame).
    QString handleFrame(const QString& frame) const;

    QTcpServer* m_server;
};

} // namespace ShackLog::Server
