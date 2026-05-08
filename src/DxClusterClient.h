#pragma once

// DxClusterClient — simple telnet client for DX cluster nodes.
//
// Connects to host:port over TCP, sends `<callsign>-L` as the login
// (the conventional "logger" suffix that lets us coexist with the
// operator's primary cluster session), then parses incoming spot
// lines into SpotData objects.
//
// Supported spot-line forms (matched leniently):
//   DX de N1XX:    14250.0  K1ABC      CQ DX                    1234Z
//   DX de N1XX-#:  14250.0  K1ABC/P    POTA K-0123              1234Z
//
// The freq field is kHz with optional decimal (kHz.fff).  Comments
// (between call and time) are passed through verbatim.
//
// On disconnect the client auto-reconnects with the same exponential
// backoff schedule as TciClient (1, 2, 5, 10, 30 seconds).

#include "SpotData.h"

#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

namespace ShackLog {

class DxClusterClient : public QObject {
    Q_OBJECT
public:
    explicit DxClusterClient(QObject* parent = nullptr);
    ~DxClusterClient() override;

    void connectToCluster(const QString& host, quint16 port, const QString& callsign);
    void disconnectFromCluster();

    bool connected() const { return m_connected; }
    QString lastError() const { return m_lastError; }

signals:
    void connectionChanged(bool connected);
    void spotReceived(const ShackLog::SpotData& spot);
    void rawLine(const QString& line);            // diagnostic

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred();
    void onReconnectTimeout();

private:
    void scheduleReconnect();
    void cancelReconnect();
    void setConnected(bool c);
    void parseLine(const QString& line);
    void maybeSendLogin(const QString& chunk);

    QTcpSocket* m_socket{nullptr};
    QTimer*     m_reconnectTimer{nullptr};
    QTimer*     m_loginTimer{nullptr};

    QString  m_host;
    quint16  m_port{0};
    QString  m_callsign;             // bare callsign, "-L" is appended on send
    bool     m_loginSent{false};
    bool     m_userInitiatedDisconnect{false};
    bool     m_connected{false};
    int      m_reconnectAttempts{0};
    QString  m_lastError;

    QByteArray m_rxBuffer;           // accumulator for partial lines
};

} // namespace ShackLog
