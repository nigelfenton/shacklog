#pragma once

// TciClient — WebSocket client speaking the EESDR TCI protocol.
//
// TCI (Transceiver Control Interface) is the wire protocol used by
// AetherSDR, ExpertSDR2, SunSDR / Expert Electronics radios.  Messages are
// short ASCII text, each terminated with `;`, of the form
//   command:arg1,arg2,...;
// We connect, send `start;` to request event streaming, then watch for
// `vfo:` and `mode:` events on RX 0 / VFO 0 (the primary receiver).
//
// Frequency on the wire is in Hz; we normalise to MHz for the rest of the
// app since that's how operators talk about freqs and how ADIF stores them.
//
// Auto-reconnect: if the socket closes for any reason, we retry with an
// exponential backoff (1, 2, 5, 10, 30 s) until the user explicitly calls
// disconnectFromServer().

#include <QObject>
#include <QString>
#include <QUrl>

class QWebSocket;
class QTimer;

namespace ShackLog {

class TciClient : public QObject {
    Q_OBJECT

public:
    explicit TciClient(QObject* parent = nullptr);
    ~TciClient() override;

    // Open ws://host:port and request event streaming.  Cancels any
    // previous connection.  host is an IP or hostname; port is typically
    // 40001 for AetherSDR, 50001 for ExpertSDR2.
    void connectToServer(const QString& host, quint16 port);

    // Permanent disconnect — does not auto-reconnect.
    void disconnectFromServer();

    bool    connected()             const { return m_connected; }
    double  currentFrequencyMhz()   const { return m_freqMhz; }
    QString currentMode()           const { return m_mode; }
    QString serverProtocolName()    const { return m_protoName; }
    QString serverProtocolVersion() const { return m_protoVersion; }
    QString lastError()             const { return m_lastError; }
    QUrl    currentUrl()            const { return m_url; }

signals:
    void connectionChanged(bool connected);
    void frequencyChanged(double mhz);
    void modeChanged(const QString& mode);
    void serverInfoChanged(const QString& name, const QString& version);
    // Diagnostic — every line received, after stripping the trailing ';'.
    void rawMessageReceived(const QString& line);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString& message);
    void onErrorOccurred();
    void onReconnectTimeout();

private:
    void send(const QString& cmd);
    void parseLine(const QString& line);
    void scheduleReconnect();
    void cancelReconnect();
    void setConnected(bool c);

    QWebSocket* m_socket{nullptr};
    QTimer*     m_reconnectTimer{nullptr};

    QUrl    m_url;
    bool    m_userInitiatedDisconnect{false};
    bool    m_connected{false};
    int     m_reconnectAttempts{0};

    double  m_freqMhz{0.0};
    QString m_mode;
    QString m_protoName;
    QString m_protoVersion;
    QString m_lastError;
};

} // namespace ShackLog
