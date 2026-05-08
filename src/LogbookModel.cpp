#include "LogbookModel.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QUuid>
#include <QVariant>
#include <QDateTime>

namespace ShackLog {

namespace {

constexpr int kCurrentSchemaVersion = 1;
const QString kAdifProgramId = "ShackLog";

QString fmtFreq(double mhz) {
    if (mhz <= 0.0) return {};
    QString s = QString::number(mhz, 'f', 6);
    while (s.endsWith('0')) s.chop(1);
    if (s.endsWith('.')) s.chop(1);
    return s;
}
QString fmtPwr(double w) {
    if (w <= 0.0) return {};
    return QString::number(w, 'f', 1);
}
QString fmtInt(int n) {
    if (n == 0) return {};
    return QString::number(n);
}
QString isoNowUtc() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

// Cabrillo two-letter mode tag from an ADIF base mode.
QString cabrilloModeFromAdif(const QString& adifMode) {
    const QString m = adifMode.toUpper();
    if (m == "SSB" || m == "AM" || m == "USB" || m == "LSB") return "PH";
    if (m == "CW")   return "CW";
    if (m == "RTTY") return "RY";
    if (m == "FM")   return "FM";
    return "DG";   // catch-all for digital modes
}

int cabrilloFreqKhz(double mhz) {
    if (mhz <= 0.0) return 0;
    return static_cast<int>(mhz * 1000.0 + 0.5);
}

} // namespace

// ───────────────────────── lifecycle ─────────────────────────

LogbookModel::LogbookModel(QObject* parent)
    : QObject(parent),
      m_connectionName(QString("shacklog_%1").arg(QUuid::createUuid().toString(QUuid::Id128)))
{
}

LogbookModel::~LogbookModel()
{
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase{};
    QSqlDatabase::removeDatabase(m_connectionName);
}

// ───────────────────────── open / migrate ─────────────────────────

bool LogbookModel::open(const QString& path)
{
    QString dbPath = path;
    if (dbPath.isEmpty()) {
        const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir{}.mkpath(dataDir);
        dbPath = dataDir + "/shacklog.sqlite";
    } else {
        QDir{}.mkpath(QFileInfo{dbPath}.absolutePath());
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        m_lastError = QString("open %1: %2").arg(dbPath, m_db.lastError().text());
        return false;
    }

    QSqlQuery prag{m_db};
    prag.exec("PRAGMA foreign_keys = ON");
    prag.exec("PRAGMA journal_mode = WAL");
    prag.exec("PRAGMA synchronous = NORMAL");

    if (!migrateSchema()) {
        m_lastError = QString("migrate: %1").arg(m_lastError);
        m_db.close();
        return false;
    }
    return true;
}

int LogbookModel::schemaVersion() const
{
    QSqlQuery q{m_db};
    if (!q.exec("PRAGMA user_version") || !q.next()) return 0;
    return q.value(0).toInt();
}

bool LogbookModel::setSchemaVersion(int v)
{
    QSqlQuery q{m_db};
    return q.exec(QString("PRAGMA user_version = %1").arg(v));
}

bool LogbookModel::migrateSchema()
{
    int v = schemaVersion();

    // v0 → v1: initial schema.
    if (v < 1) {
        QSqlQuery q{m_db};
        const char* createQsos =
            "CREATE TABLE IF NOT EXISTS qsos ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  call TEXT NOT NULL,"
            "  qso_date TEXT NOT NULL,"
            "  time_on TEXT NOT NULL,"
            "  time_off TEXT,"
            "  band TEXT,"
            "  freq REAL,"
            "  mode TEXT,"
            "  submode TEXT,"
            "  rst_sent TEXT,"
            "  rst_rcvd TEXT,"
            "  name TEXT,"
            "  qth TEXT,"
            "  gridsquare TEXT,"
            "  dxcc INTEGER,"
            "  country TEXT,"
            "  state TEXT,"
            "  cnty TEXT,"
            "  cont TEXT,"
            "  cqz INTEGER,"
            "  ituz INTEGER,"
            "  my_call TEXT,"
            "  my_gridsquare TEXT,"
            "  my_state TEXT,"
            "  tx_pwr REAL,"
            "  my_operator TEXT,"
            "  contest_id TEXT,"
            "  srx INTEGER,"
            "  stx INTEGER,"
            "  srx_string TEXT,"
            "  stx_string TEXT,"
            "  comment TEXT,"
            "  notes TEXT,"
            "  qsl_sent TEXT,"
            "  qsl_rcvd TEXT,"
            "  lotw_sent TEXT,"
            "  lotw_rcvd TEXT,"
            "  eqsl_sent TEXT,"
            "  eqsl_rcvd TEXT,"
            "  created_at TEXT NOT NULL,"
            "  updated_at TEXT NOT NULL"
            ")";
        if (!q.exec(createQsos)) {
            m_lastError = q.lastError().text();
            return false;
        }
        if (!q.exec("CREATE INDEX IF NOT EXISTS idx_qsos_call ON qsos(call)")) {
            m_lastError = q.lastError().text(); return false;
        }
        if (!q.exec("CREATE INDEX IF NOT EXISTS idx_qsos_date ON qsos(qso_date)")) {
            m_lastError = q.lastError().text(); return false;
        }
        if (!q.exec("CREATE INDEX IF NOT EXISTS idx_qsos_band ON qsos(band)")) {
            m_lastError = q.lastError().text(); return false;
        }
        if (!q.exec("CREATE INDEX IF NOT EXISTS idx_qsos_contest ON qsos(contest_id)")) {
            m_lastError = q.lastError().text(); return false;
        }
        const char* createSettings =
            "CREATE TABLE IF NOT EXISTS settings ("
            "  key TEXT PRIMARY KEY,"
            "  value TEXT"
            ")";
        if (!q.exec(createSettings)) {
            m_lastError = q.lastError().text(); return false;
        }
        if (!setSchemaVersion(1)) {
            m_lastError = "could not set user_version";
            return false;
        }
        v = 1;
    }

    if (v != kCurrentSchemaVersion) {
        m_lastError = QString("schema version %1 not understood (current=%2)")
                          .arg(v).arg(kCurrentSchemaVersion);
        return false;
    }
    return true;
}

// ───────────────────────── CRUD ─────────────────────────

bool LogbookModel::insertQso(Qso& qso)
{
    if (!m_db.isOpen()) { m_lastError = "db not open"; return false; }
    qso.call = qso.call.trimmed().toUpper();
    if (qso.createdAt.isEmpty()) qso.createdAt = isoNowUtc();
    qso.updatedAt = isoNowUtc();

    QSqlQuery q{m_db};
    q.prepare(
        "INSERT INTO qsos ("
        " call, qso_date, time_on, time_off, band, freq, mode, submode, rst_sent, rst_rcvd,"
        " name, qth, gridsquare, dxcc, country, state, cnty, cont, cqz, ituz,"
        " my_call, my_gridsquare, my_state, tx_pwr, my_operator,"
        " contest_id, srx, stx, srx_string, stx_string,"
        " comment, notes,"
        " qsl_sent, qsl_rcvd, lotw_sent, lotw_rcvd, eqsl_sent, eqsl_rcvd,"
        " created_at, updated_at"
        ") VALUES ("
        " :call, :qso_date, :time_on, :time_off, :band, :freq, :mode, :submode, :rst_sent, :rst_rcvd,"
        " :name, :qth, :gridsquare, :dxcc, :country, :state, :cnty, :cont, :cqz, :ituz,"
        " :my_call, :my_gridsquare, :my_state, :tx_pwr, :my_operator,"
        " :contest_id, :srx, :stx, :srx_string, :stx_string,"
        " :comment, :notes,"
        " :qsl_sent, :qsl_rcvd, :lotw_sent, :lotw_rcvd, :eqsl_sent, :eqsl_rcvd,"
        " :created_at, :updated_at"
        ")"
    );
    q.bindValue(":call", qso.call);
    q.bindValue(":qso_date", qso.qsoDate);
    q.bindValue(":time_on", qso.timeOn);
    q.bindValue(":time_off", qso.timeOff);
    q.bindValue(":band", qso.band);
    q.bindValue(":freq", qso.freq);
    q.bindValue(":mode", qso.mode);
    q.bindValue(":submode", qso.submode);
    q.bindValue(":rst_sent", qso.rstSent);
    q.bindValue(":rst_rcvd", qso.rstRcvd);
    q.bindValue(":name", qso.name);
    q.bindValue(":qth", qso.qth);
    q.bindValue(":gridsquare", qso.gridsquare);
    q.bindValue(":dxcc", qso.dxcc);
    q.bindValue(":country", qso.country);
    q.bindValue(":state", qso.state);
    q.bindValue(":cnty", qso.cnty);
    q.bindValue(":cont", qso.cont);
    q.bindValue(":cqz", qso.cqz);
    q.bindValue(":ituz", qso.ituz);
    q.bindValue(":my_call", qso.myCall);
    q.bindValue(":my_gridsquare", qso.myGridsquare);
    q.bindValue(":my_state", qso.myState);
    q.bindValue(":tx_pwr", qso.txPwr);
    q.bindValue(":my_operator", qso.myOperator);
    q.bindValue(":contest_id", qso.contestId);
    q.bindValue(":srx", qso.srx);
    q.bindValue(":stx", qso.stx);
    q.bindValue(":srx_string", qso.srxString);
    q.bindValue(":stx_string", qso.stxString);
    q.bindValue(":comment", qso.comment);
    q.bindValue(":notes", qso.notes);
    q.bindValue(":qsl_sent", qso.qslSent);
    q.bindValue(":qsl_rcvd", qso.qslRcvd);
    q.bindValue(":lotw_sent", qso.lotwSent);
    q.bindValue(":lotw_rcvd", qso.lotwRcvd);
    q.bindValue(":eqsl_sent", qso.eqslSent);
    q.bindValue(":eqsl_rcvd", qso.eqslRcvd);
    q.bindValue(":created_at", qso.createdAt);
    q.bindValue(":updated_at", qso.updatedAt);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    qso.id = q.lastInsertId().toLongLong();
    emit qsoAdded(qso.id);
    return true;
}

bool LogbookModel::updateQso(const Qso& qsoIn)
{
    if (!m_db.isOpen())   { m_lastError = "db not open"; return false; }
    if (qsoIn.id < 0)     { m_lastError = "qso id invalid"; return false; }

    Qso qso = qsoIn;
    qso.call = qso.call.trimmed().toUpper();
    qso.updatedAt = isoNowUtc();

    QSqlQuery q{m_db};
    q.prepare(
        "UPDATE qsos SET"
        " call=:call, qso_date=:qso_date, time_on=:time_on, time_off=:time_off,"
        " band=:band, freq=:freq, mode=:mode, submode=:submode,"
        " rst_sent=:rst_sent, rst_rcvd=:rst_rcvd,"
        " name=:name, qth=:qth, gridsquare=:gridsquare, dxcc=:dxcc,"
        " country=:country, state=:state, cnty=:cnty, cont=:cont,"
        " cqz=:cqz, ituz=:ituz,"
        " my_call=:my_call, my_gridsquare=:my_gridsquare, my_state=:my_state,"
        " tx_pwr=:tx_pwr, my_operator=:my_operator,"
        " contest_id=:contest_id, srx=:srx, stx=:stx,"
        " srx_string=:srx_string, stx_string=:stx_string,"
        " comment=:comment, notes=:notes,"
        " qsl_sent=:qsl_sent, qsl_rcvd=:qsl_rcvd,"
        " lotw_sent=:lotw_sent, lotw_rcvd=:lotw_rcvd,"
        " eqsl_sent=:eqsl_sent, eqsl_rcvd=:eqsl_rcvd,"
        " updated_at=:updated_at"
        " WHERE id=:id"
    );
    q.bindValue(":id", qso.id);
    q.bindValue(":call", qso.call);
    q.bindValue(":qso_date", qso.qsoDate);
    q.bindValue(":time_on", qso.timeOn);
    q.bindValue(":time_off", qso.timeOff);
    q.bindValue(":band", qso.band);
    q.bindValue(":freq", qso.freq);
    q.bindValue(":mode", qso.mode);
    q.bindValue(":submode", qso.submode);
    q.bindValue(":rst_sent", qso.rstSent);
    q.bindValue(":rst_rcvd", qso.rstRcvd);
    q.bindValue(":name", qso.name);
    q.bindValue(":qth", qso.qth);
    q.bindValue(":gridsquare", qso.gridsquare);
    q.bindValue(":dxcc", qso.dxcc);
    q.bindValue(":country", qso.country);
    q.bindValue(":state", qso.state);
    q.bindValue(":cnty", qso.cnty);
    q.bindValue(":cont", qso.cont);
    q.bindValue(":cqz", qso.cqz);
    q.bindValue(":ituz", qso.ituz);
    q.bindValue(":my_call", qso.myCall);
    q.bindValue(":my_gridsquare", qso.myGridsquare);
    q.bindValue(":my_state", qso.myState);
    q.bindValue(":tx_pwr", qso.txPwr);
    q.bindValue(":my_operator", qso.myOperator);
    q.bindValue(":contest_id", qso.contestId);
    q.bindValue(":srx", qso.srx);
    q.bindValue(":stx", qso.stx);
    q.bindValue(":srx_string", qso.srxString);
    q.bindValue(":stx_string", qso.stxString);
    q.bindValue(":comment", qso.comment);
    q.bindValue(":notes", qso.notes);
    q.bindValue(":qsl_sent", qso.qslSent);
    q.bindValue(":qsl_rcvd", qso.qslRcvd);
    q.bindValue(":lotw_sent", qso.lotwSent);
    q.bindValue(":lotw_rcvd", qso.lotwRcvd);
    q.bindValue(":eqsl_sent", qso.eqslSent);
    q.bindValue(":eqsl_rcvd", qso.eqslRcvd);
    q.bindValue(":updated_at", qso.updatedAt);

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    emit qsoUpdated(qso.id);
    return true;
}

bool LogbookModel::deleteQso(qint64 id)
{
    if (!m_db.isOpen()) { m_lastError = "db not open"; return false; }
    QSqlQuery q{m_db};
    q.prepare("DELETE FROM qsos WHERE id = :id");
    q.bindValue(":id", id);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    emit qsoDeleted(id);
    return true;
}

Qso LogbookModel::getQso(qint64 id, bool* ok) const
{
    if (ok) *ok = false;
    Qso q;
    if (!m_db.isOpen()) return q;
    QSqlQuery sel{m_db};
    sel.prepare("SELECT * FROM qsos WHERE id = :id");
    sel.bindValue(":id", id);
    if (!sel.exec() || !sel.next()) return q;
    q = qsoFromRow(sel);
    if (ok) *ok = true;
    return q;
}

QString LogbookModel::filterToSql(const QueryFilter& filter, QVariantList* binds) const
{
    QStringList where;
    if (!filter.text.isEmpty()) {
        where << "(call LIKE ? OR name LIKE ? OR qth LIKE ? OR gridsquare LIKE ? OR comment LIKE ?)";
        const QString pat = "%" + filter.text + "%";
        for (int i = 0; i < 5; ++i) binds->append(pat);
    }
    if (!filter.band.isEmpty()) { where << "band = ?"; binds->append(filter.band); }
    if (!filter.mode.isEmpty()) { where << "mode = ?"; binds->append(filter.mode); }
    if (!filter.contestId.isEmpty()) {
        if (filter.contestId == "<NONE>") {
            where << "(contest_id IS NULL OR contest_id = '')";
        } else {
            where << "contest_id = ?";
            binds->append(filter.contestId);
        }
    }
    if (!filter.dateFrom.isEmpty()) { where << "qso_date >= ?"; binds->append(filter.dateFrom); }
    if (!filter.dateTo.isEmpty())   { where << "qso_date <= ?"; binds->append(filter.dateTo);   }
    if (where.isEmpty()) return {};
    return " WHERE " + where.join(" AND ");
}

QVector<Qso> LogbookModel::queryQsos(const QueryFilter& filter) const
{
    QVector<Qso> out;
    if (!m_db.isOpen()) return out;

    QVariantList binds;
    QString sql = "SELECT * FROM qsos" + filterToSql(filter, &binds)
                + " ORDER BY qso_date DESC, time_on DESC, id DESC";
    if (filter.limit > 0) sql += QString(" LIMIT %1").arg(filter.limit);

    QSqlQuery q{m_db};
    q.prepare(sql);
    for (const auto& v : binds) q.addBindValue(v);
    if (!q.exec()) {
        const_cast<LogbookModel*>(this)->m_lastError = q.lastError().text();
        return out;
    }
    while (q.next()) out.append(qsoFromRow(q));
    return out;
}

int LogbookModel::countQsos(const QueryFilter& filter) const
{
    if (!m_db.isOpen()) return 0;
    QVariantList binds;
    QString sql = "SELECT COUNT(*) FROM qsos" + filterToSql(filter, &binds);
    QSqlQuery q{m_db};
    q.prepare(sql);
    for (const auto& v : binds) q.addBindValue(v);
    if (!q.exec() || !q.next()) return 0;
    return q.value(0).toInt();
}

bool LogbookModel::isDuplicate(const QString& call,
                               const QString& band,
                               const QString& mode,
                               int windowSeconds) const
{
    if (!m_db.isOpen() || call.isEmpty()) return false;
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-windowSeconds);
    QSqlQuery q{m_db};
    q.prepare(
        "SELECT 1 FROM qsos"
        " WHERE call = :call AND band = :band AND mode = :mode"
        "   AND created_at >= :cutoff"
        " LIMIT 1"
    );
    q.bindValue(":call", call.trimmed().toUpper());
    q.bindValue(":band", band);
    q.bindValue(":mode", mode);
    q.bindValue(":cutoff", cutoff.toString(Qt::ISODate));
    if (!q.exec()) return false;
    return q.next();
}

Qso LogbookModel::qsoFromRow(QSqlQuery& q)
{
    Qso o;
    QSqlRecord r = q.record();
    auto s = [&](const char* f) { return r.value(QString::fromLatin1(f)).toString(); };
    auto i = [&](const char* f) { return r.value(QString::fromLatin1(f)).toInt(); };
    auto d = [&](const char* f) { return r.value(QString::fromLatin1(f)).toDouble(); };

    o.id           = r.value(QStringLiteral("id")).toLongLong();
    o.call         = s("call");
    o.qsoDate      = s("qso_date");
    o.timeOn       = s("time_on");
    o.timeOff      = s("time_off");
    o.band         = s("band");
    o.freq         = d("freq");
    o.mode         = s("mode");
    o.submode      = s("submode");
    o.rstSent      = s("rst_sent");
    o.rstRcvd      = s("rst_rcvd");
    o.name         = s("name");
    o.qth          = s("qth");
    o.gridsquare   = s("gridsquare");
    o.dxcc         = i("dxcc");
    o.country      = s("country");
    o.state        = s("state");
    o.cnty         = s("cnty");
    o.cont         = s("cont");
    o.cqz          = i("cqz");
    o.ituz         = i("ituz");
    o.myCall       = s("my_call");
    o.myGridsquare = s("my_gridsquare");
    o.myState      = s("my_state");
    o.txPwr        = d("tx_pwr");
    o.myOperator   = s("my_operator");
    o.contestId    = s("contest_id");
    o.srx          = i("srx");
    o.stx          = i("stx");
    o.srxString    = s("srx_string");
    o.stxString    = s("stx_string");
    o.comment      = s("comment");
    o.notes        = s("notes");
    o.qslSent      = s("qsl_sent");
    o.qslRcvd      = s("qsl_rcvd");
    o.lotwSent     = s("lotw_sent");
    o.lotwRcvd     = s("lotw_rcvd");
    o.eqslSent     = s("eqsl_sent");
    o.eqslRcvd     = s("eqsl_rcvd");
    o.createdAt    = s("created_at");
    o.updatedAt    = s("updated_at");
    return o;
}

// ───────────────────────── settings ─────────────────────────

QString LogbookModel::settingValue(const QString& key, const QString& defaultValue) const
{
    if (!m_db.isOpen()) return defaultValue;
    QSqlQuery q{m_db};
    q.prepare("SELECT value FROM settings WHERE key = :k");
    q.bindValue(":k", key);
    if (!q.exec() || !q.next()) return defaultValue;
    return q.value(0).toString();
}

bool LogbookModel::setSetting(const QString& key, const QString& value)
{
    if (!m_db.isOpen()) { m_lastError = "db not open"; return false; }
    QSqlQuery q{m_db};
    q.prepare("INSERT INTO settings(key, value) VALUES(:k, :v)"
              " ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    q.bindValue(":k", key);
    q.bindValue(":v", value);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    emit settingChanged(key);
    return true;
}

double LogbookModel::defaultTxPwr() const
{
    bool ok = false;
    const double v = settingValue("DEFAULT_TX_PWR").toDouble(&ok);
    return ok ? v : 0.0;
}

// ───────────────────────── ADIF export ─────────────────────────

QString LogbookModel::adifField(const QString& tag, const QString& value)
{
    if (value.isEmpty()) return {};
    const QByteArray utf8 = value.toUtf8();
    return QString("<%1:%2>%3 ").arg(tag, QString::number(utf8.size()), value);
}

int LogbookModel::exportAdif(const QString& filePath, const QueryFilter& filter) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        const_cast<LogbookModel*>(this)->m_lastError = f.errorString();
        return -1;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);

    out << "ADIF Export from " << kAdifProgramId << "\n\n";
    out << adifField("ADIF_VER",          "3.1.4");
    out << adifField("PROGRAMID",         kAdifProgramId);
    const QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd HHmmss");
    out << adifField("CREATED_TIMESTAMP", stamp);
    out << "<EOH>\n\n";

    const QVector<Qso> rows = queryQsos(filter);
    for (const Qso& q : rows) {
        out << adifField("CALL",       q.call);
        out << adifField("QSO_DATE",   q.qsoDate);
        out << adifField("TIME_ON",    q.timeOn);
        out << adifField("TIME_OFF",   q.timeOff);
        out << adifField("BAND",       q.band);
        out << adifField("FREQ",       fmtFreq(q.freq));
        out << adifField("MODE",       q.mode);
        out << adifField("SUBMODE",    q.submode);
        out << adifField("RST_SENT",   q.rstSent);
        out << adifField("RST_RCVD",   q.rstRcvd);
        out << adifField("NAME",       q.name);
        out << adifField("QTH",        q.qth);
        out << adifField("GRIDSQUARE", q.gridsquare);
        out << adifField("DXCC",       fmtInt(q.dxcc));
        out << adifField("COUNTRY",    q.country);
        out << adifField("STATE",      q.state);
        out << adifField("CNTY",       q.cnty);
        out << adifField("CONT",       q.cont);
        out << adifField("CQZ",        fmtInt(q.cqz));
        out << adifField("ITUZ",       fmtInt(q.ituz));
        out << adifField("STATION_CALLSIGN", q.myCall);
        out << adifField("MY_GRIDSQUARE",    q.myGridsquare);
        out << adifField("MY_STATE",         q.myState);
        out << adifField("TX_PWR",     fmtPwr(q.txPwr));
        out << adifField("OPERATOR",   q.myOperator);
        out << adifField("CONTEST_ID", q.contestId);
        out << adifField("SRX",        fmtInt(q.srx));
        out << adifField("STX",        fmtInt(q.stx));
        out << adifField("SRX_STRING", q.srxString);
        out << adifField("STX_STRING", q.stxString);
        out << adifField("COMMENT",    q.comment);
        out << adifField("NOTES",      q.notes);
        out << adifField("QSL_SENT",   q.qslSent);
        out << adifField("QSL_RCVD",   q.qslRcvd);
        out << adifField("LOTW_QSL_SENT", q.lotwSent);
        out << adifField("LOTW_QSL_RCVD", q.lotwRcvd);
        out << adifField("EQSL_QSL_SENT", q.eqslSent);
        out << adifField("EQSL_QSL_RCVD", q.eqslRcvd);
        out << "<EOR>\n";
    }
    out.flush();
    f.close();
    return rows.size();
}

// ───────────────────────── Cabrillo export ─────────────────────────

int LogbookModel::exportCabrillo(const QString& filePath,
                                 const QString& contestId,
                                 const QueryFilter& filter) const
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        const_cast<LogbookModel*>(this)->m_lastError = f.errorString();
        return -1;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);

    auto setting = [&](const QString& k, const QString& def = {}) {
        return settingValue(k, def);
    };

    out << "START-OF-LOG: 3.0\r\n";
    out << "CONTEST: " << contestId << "\r\n";
    out << "CALLSIGN: " << setting("MY_CALL") << "\r\n";
    out << "CATEGORY-OPERATOR: "    << setting("CABRILLO_CAT_OPERATOR",    "SINGLE-OP")    << "\r\n";
    out << "CATEGORY-ASSISTED: "    << setting("CABRILLO_CAT_ASSISTED",    "NON-ASSISTED") << "\r\n";
    out << "CATEGORY-BAND: "        << setting("CABRILLO_CAT_BAND",        "ALL")          << "\r\n";
    out << "CATEGORY-MODE: "        << setting("CABRILLO_CAT_MODE",        "MIXED")        << "\r\n";
    out << "CATEGORY-POWER: "       << setting("CABRILLO_CAT_POWER",       "HIGH")         << "\r\n";
    out << "CATEGORY-STATION: "     << setting("CABRILLO_CAT_STATION",     "FIXED")        << "\r\n";
    out << "CATEGORY-TRANSMITTER: " << setting("CABRILLO_CAT_TRANSMITTER", "ONE")          << "\r\n";
    if (!setting("CABRILLO_CLUB").isEmpty())
        out << "CLUB: " << setting("CABRILLO_CLUB") << "\r\n";
    if (!setting("CABRILLO_LOCATION").isEmpty())
        out << "LOCATION: " << setting("CABRILLO_LOCATION") << "\r\n";
    if (!setting("CABRILLO_NAME").isEmpty())
        out << "NAME: " << setting("CABRILLO_NAME") << "\r\n";
    if (!setting("CABRILLO_ADDRESS").isEmpty())
        out << "ADDRESS: " << setting("CABRILLO_ADDRESS") << "\r\n";
    if (!setting("CABRILLO_EMAIL").isEmpty())
        out << "EMAIL: " << setting("CABRILLO_EMAIL") << "\r\n";
    out << "CREATED-BY: " << kAdifProgramId << "\r\n";

    QueryFilter filt = filter;
    filt.contestId = contestId;
    const QVector<Qso> rows = queryQsos(filt);

    for (const Qso& q : rows) {
        const int    freqKhz = cabrilloFreqKhz(q.freq);
        const QString cabMode = cabrilloModeFromAdif(q.mode);

        QString date = q.qsoDate;
        if (date.size() == 8) date = date.left(4) + "-" + date.mid(4, 2) + "-" + date.mid(6, 2);

        QString time = q.timeOn.left(4);

        QString sentExch = !q.stxString.isEmpty()
                          ? q.stxString
                          : (q.stx > 0 ? QString::number(q.stx).rightJustified(3, '0') : QString{});
        QString rcvdExch = !q.srxString.isEmpty()
                          ? q.srxString
                          : (q.srx > 0 ? QString::number(q.srx).rightJustified(3, '0') : QString{});

        out << "QSO: "
            << QString("%1").arg(freqKhz, 5, 10, QChar(' ')) << " "
            << cabMode << " "
            << date << " "
            << time << " "
            << q.myCall.leftJustified(13, ' ') << " "
            << q.rstSent.leftJustified(3, ' ')  << " "
            << sentExch.leftJustified(6, ' ')   << " "
            << q.call.leftJustified(13, ' ') << " "
            << q.rstRcvd.leftJustified(3, ' ')  << " "
            << rcvdExch.leftJustified(6, ' ')
            << "\r\n";
    }
    out << "END-OF-LOG:\r\n";
    out.flush();
    f.close();
    return rows.size();
}

// ───────────────────────── static helpers ─────────────────────────

QString LogbookModel::bandFromFreqMhz(double mhz)
{
    struct B { double lo, hi; const char* name; };
    static const B bands[] = {
        { 0.1357,  0.1378,  "2200m" },
        { 0.472,   0.479,   "630m"  },
        { 1.8,     2.0,     "160m"  },
        { 3.5,     4.0,     "80m"   },
        { 5.06,    5.45,    "60m"   },
        { 7.0,     7.3,     "40m"   },
        { 10.1,    10.15,   "30m"   },
        { 14.0,    14.35,   "20m"   },
        { 18.068,  18.168,  "17m"   },
        { 21.0,    21.45,   "15m"   },
        { 24.89,   24.99,   "12m"   },
        { 28.0,    29.7,    "10m"   },
        { 50.0,    54.0,    "6m"    },
        { 70.0,    70.5,    "4m"    },
        { 144.0,   148.0,   "2m"    },
        { 222.0,   225.0,   "1.25m" },
        { 420.0,   450.0,   "70cm"  },
        { 902.0,   928.0,   "33cm"  },
        { 1240.0,  1300.0,  "23cm"  },
        { 2300.0,  2450.0,  "13cm"  },
        { 3300.0,  3500.0,  "9cm"   },
        { 5650.0,  5925.0,  "6cm"   },
        { 10000.0, 10500.0, "3cm"   },
    };
    for (const auto& b : bands) {
        if (mhz >= b.lo && mhz <= b.hi) return QString::fromLatin1(b.name);
    }
    return {};
}

void LogbookModel::adifModeFromTciMode(const QString& tciMode,
                                       QString* adifMode,
                                       QString* adifSubmode)
{
    QString mode, sub;
    const QString m = tciMode.toUpper();
    if      (m == "USB")  { mode = "SSB"; sub = "USB"; }
    else if (m == "LSB")  { mode = "SSB"; sub = "LSB"; }
    else if (m == "CW" || m == "CWL" || m == "CWU") { mode = "CW"; }
    else if (m == "AM"  || m == "SAM")              { mode = "AM"; }
    else if (m == "FM"  || m == "NFM" || m == "DFM") { mode = "FM"; }
    else if (m == "RTTY")                           { mode = "RTTY"; }
    // DIGU/DIGL/DIGI — leave empty so the entry form forces a pick (the
    // actual digital mode lives in the soundcard program, not in TCI).
    if (adifMode)    *adifMode = mode;
    if (adifSubmode) *adifSubmode = sub;
}

} // namespace ShackLog
