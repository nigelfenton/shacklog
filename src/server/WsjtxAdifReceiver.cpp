#include "WsjtxAdifReceiver.h"

#include "LogbookModel.h"
#include "Qso.h"

#include <QByteArray>
#include <QDebug>
#include <QHash>
#include <QHostAddress>
#include <QString>

namespace ShackLog::Server {

namespace {

// ───────────────────────── ADIF parser ─────────────────────────

// Parse a single ADIF record from `buf`.  Tolerant of preamble + <EOH>
// (header marker) preceding the record.  Returns a hash of lowercased
// field name → value.  Returns an empty hash if the buffer doesn't
// contain a complete record terminated by <EOR>.
QHash<QString, QString> parseAdifRecord(const QByteArray& buf)
{
    QHash<QString, QString> out;
    int i = 0;
    while (i < buf.size()) {
        // Skip whitespace + any preamble text up to next '<'.
        const int lt = buf.indexOf('<', i);
        if (lt < 0) break;
        const int gt = buf.indexOf('>', lt + 1);
        if (gt < 0) break;
        const QByteArray spec = buf.mid(lt + 1, gt - lt - 1);
        i = gt + 1;

        // Special markers: <EOH> ends header (we just continue past it);
        // <EOR> ends the record (we're done).  Case-insensitive.
        const QByteArray marker = spec.trimmed().toUpper();
        if (marker == "EOR") return out;
        if (marker == "EOH") { continue; }

        // Field spec: TAG:length  or  TAG:length:datatype
        const int colon1 = spec.indexOf(':');
        if (colon1 < 0) continue;
        const QString name = QString::fromUtf8(spec.left(colon1))
                                .trimmed().toLower();
        QByteArray rest = spec.mid(colon1 + 1);
        const int colon2 = rest.indexOf(':');
        if (colon2 >= 0) rest = rest.left(colon2);
        bool ok = false;
        const int len = rest.trimmed().toInt(&ok);
        if (!ok || len < 0) continue;
        if (i + len > buf.size()) break;          // truncated
        const QString value = QString::fromUtf8(buf.mid(i, len));
        if (!name.isEmpty()) out.insert(name, value);
        i += len;
    }
    // No <EOR> seen — partial / malformed.  Return whatever we did parse
    // so the caller can decide whether to use it.
    return out;
}

// Construct a Qso from an ADIF field hash.
Qso qsoFromAdif(const QHash<QString, QString>& f)
{
    Qso q;
    q.call         = f.value(QStringLiteral("call")).toUpper();
    q.qsoDate      = f.value(QStringLiteral("qso_date"));
    q.timeOn       = f.value(QStringLiteral("time_on"));
    q.timeOff      = f.value(QStringLiteral("time_off"));
    q.band         = f.value(QStringLiteral("band")).toLower();
    q.freq         = f.value(QStringLiteral("freq")).toDouble();
    q.mode         = f.value(QStringLiteral("mode")).toUpper();
    q.submode      = f.value(QStringLiteral("submode"));
    q.rstSent      = f.value(QStringLiteral("rst_sent"));
    q.rstRcvd      = f.value(QStringLiteral("rst_rcvd"));
    q.gridsquare   = f.value(QStringLiteral("gridsquare"));
    q.name         = f.value(QStringLiteral("name"));
    q.qth          = f.value(QStringLiteral("qth"));
    q.country      = f.value(QStringLiteral("country"));
    q.state        = f.value(QStringLiteral("state"));
    q.cnty         = f.value(QStringLiteral("cnty"));
    q.cont         = f.value(QStringLiteral("cont"));
    q.dxcc         = f.value(QStringLiteral("dxcc")).toInt();
    q.cqz          = f.value(QStringLiteral("cqz")).toInt();
    q.ituz         = f.value(QStringLiteral("ituz")).toInt();
    q.myCall       = f.value(QStringLiteral("station_callsign"));
    q.myGridsquare = f.value(QStringLiteral("my_gridsquare"));
    q.myState      = f.value(QStringLiteral("my_state"));
    q.txPwr        = f.value(QStringLiteral("tx_pwr")).toDouble();
    q.myOperator   = f.value(QStringLiteral("operator"));
    q.contestId    = f.value(QStringLiteral("contest_id"));
    q.srx          = f.value(QStringLiteral("srx")).toInt();
    q.stx          = f.value(QStringLiteral("stx")).toInt();
    q.srxString    = f.value(QStringLiteral("srx_string"));
    q.stxString    = f.value(QStringLiteral("stx_string"));
    q.comment      = f.value(QStringLiteral("comment"));
    q.notes        = f.value(QStringLiteral("notes"));
    return q;
}

} // namespace

// ───────────────────────── WsjtxAdifReceiver ─────────────────────────

WsjtxAdifReceiver::WsjtxAdifReceiver(LogbookModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_socket(new QUdpSocket(this))
{
    connect(m_socket, &QUdpSocket::readyRead,
            this,     &WsjtxAdifReceiver::onReadyRead);
}

bool WsjtxAdifReceiver::start(quint16 port)
{
    // ShareAddress so we coexist gracefully with anything else that may
    // ever want UDP on the same port (e.g. multiple loggers behind a
    // wireguard mesh, etc.).
    const bool ok = m_socket->bind(QHostAddress::Any, port,
                                   QUdpSocket::ShareAddress |
                                   QUdpSocket::ReuseAddressHint);
    if (!ok) {
        qWarning() << "WsjtxAdifReceiver: bind UDP" << port << "failed:"
                   << m_socket->errorString();
        return false;
    }
    qInfo() << "WsjtxAdifReceiver: listening on UDP port"
            << m_socket->localPort();
    return true;
}

quint16 WsjtxAdifReceiver::port() const
{
    return m_socket->localPort();
}

bool WsjtxAdifReceiver::isListening() const
{
    return m_socket->state() == QAbstractSocket::BoundState;
}

void WsjtxAdifReceiver::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray   buf(m_socket->pendingDatagramSize(), Qt::Uninitialized);
        QHostAddress peer;
        quint16      peerPort = 0;
        const qint64 n = m_socket->readDatagram(
            buf.data(), buf.size(), &peer, &peerPort);
        if (n <= 0) continue;
        buf.resize(static_cast<int>(n));

        const QString peerStr = peer.toString();
        qInfo().noquote()
            << QString("WsjtxAdifReceiver: %1 bytes from %2:%3")
                   .arg(n).arg(peerStr).arg(peerPort);

        const auto fields = parseAdifRecord(buf);
        if (fields.isEmpty()) {
            qWarning() << "WsjtxAdifReceiver: empty / unparseable datagram, dropped";
            continue;
        }

        Qso q = qsoFromAdif(fields);

        // Derive band from frequency if missing.
        if (q.band.isEmpty() && q.freq > 0.0)
            q.band = LogbookModel::bandFromFreqMhz(q.freq);

        QStringList missing;
        if (q.call.isEmpty())    missing << "CALL";
        if (q.qsoDate.isEmpty()) missing << "QSO_DATE";
        if (q.timeOn.isEmpty())  missing << "TIME_ON";
        if (q.band.isEmpty())    missing << "BAND";
        if (q.mode.isEmpty())    missing << "MODE";
        if (!missing.isEmpty()) {
            qWarning().noquote()
                << "WsjtxAdifReceiver: ADIF missing required field(s):"
                << missing.join(", ") << "(dropped)";
            continue;
        }

        if (!m_model || !m_model->isOpen()) {
            qWarning() << "WsjtxAdifReceiver: logbook not open, dropped";
            continue;
        }

        const QString actor = QStringLiteral("wsjtx:%1").arg(peerStr);
        if (!m_model->insertQso(q, actor)) {
            qWarning().noquote()
                << "WsjtxAdifReceiver: insert failed —" << m_model->errorString();
            continue;
        }
        qInfo().noquote()
            << QString("WsjtxAdifReceiver: logged id=%1 call=%2 band=%3 mode=%4 actor=%5")
                   .arg(q.id).arg(q.call, q.band, q.mode, actor);
    }
}

} // namespace ShackLog::Server
