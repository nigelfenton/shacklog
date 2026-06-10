#pragma once

// LogbookModel — SQLite-backed ham radio logbook.
//
// Single SQLite database file living under QStandardPaths::AppLocalDataLocation
// (per-user, persists across runs).  One `qsos` table holds all QSO records
// using ADIF-aligned column names; one `settings` table holds operator
// defaults (MY_CALL, MY_GRIDSQUARE, default TX_PWR, current contest mode etc).
//
// Schema is migrated automatically on open() — schema_version pragma drives a
// linear migration chain.  Adding a column == bump the version + add a
// migration step in migrateSchema().
//
// All operations are synchronous; the on-disk DB is small enough (tens of
// thousands of QSOs == a few MB) that operations finish in well under a
// millisecond and a worker thread isn't worth the complexity.

#include "Qso.h"

#include <QObject>
#include <QString>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVector>

namespace ShackLog {

// Query filter for queryQsos / countQsos / export.  Lives at namespace
// scope rather than nested inside LogbookModel because GCC and Clang
// (correctly, per the C++ standard) reject default arguments that
// reference a nested struct whose default member initializers haven't
// been processed yet — and `int limit{0}` here is one of those.
struct LogbookFilter {
    QString text;
    QString band;
    QString mode;
    QString contestId;               // "<NONE>" filters to non-contest QSOs only
    QString dateFrom;                // ADIF YYYYMMDD inclusive
    QString dateTo;
    int     limit{0};
};

class LogbookModel : public QObject {
    Q_OBJECT

public:
    explicit LogbookModel(QObject* parent = nullptr);
    ~LogbookModel() override;

    // Open or create the logbook database.  If `path` is empty, defaults to
    //   <AppLocalDataLocation>/shacklog.sqlite
    bool open(const QString& path = {});
    bool isOpen() const { return m_db.isOpen(); }
    QString databasePath() const { return m_db.databaseName(); }
    QString errorString()  const { return m_lastError; }

    // ── CRUD ──────────────────────────────────────────────────────────
    // The `actor` argument (schema v2+) names whoever caused this mutation
    // for the audit log — "desktop", "http-api", "n3fjp:WSJT-X", a station
    // ID, etc.  Default keeps the desktop's call sites working unchanged.
    bool insertQso(Qso& qso, const QString& actor = QStringLiteral("system"));
    bool updateQso(const Qso& qso, const QString& actor = QStringLiteral("system"));
    bool deleteQso(qint64 id, const QString& actor = QStringLiteral("system"));
    Qso  getQso(qint64 id, bool* ok = nullptr) const;

    QVector<Qso> queryQsos(const LogbookFilter& filter = {}) const;
    int          countQsos(const LogbookFilter& filter = {}) const;

    bool isDuplicate(const QString& call,
                     const QString& band,
                     const QString& mode,
                     int windowSeconds = 3600) const;

    // ── Settings ──────────────────────────────────────────────────────
    QString settingValue(const QString& key, const QString& defaultValue = {}) const;
    bool    setSetting(const QString& key, const QString& value);

    QString myCall() const            { return settingValue("MY_CALL"); }
    QString myGridsquare() const      { return settingValue("MY_GRIDSQUARE"); }
    QString myState() const           { return settingValue("MY_STATE"); }
    double  defaultTxPwr() const;
    bool    contestMode() const       { return settingValue("CONTEST_MODE") == "1"; }
    QString contestId() const         { return settingValue("CONTEST_ID"); }

    // ── Import ────────────────────────────────────────────────────────
    struct AdifImportResult {
        bool ok{false};       // false == file-level failure; see errorString()
        int  imported{0};
        int  duplicates{0};   // same call+date+band+mode, TIME_ON equal to the minute
        int  invalid{0};      // missing required fields, or insert failure
    };
    AdifImportResult importAdif(const QString& filePath,
                                const QString& actor = QStringLiteral("adif-import"));

    // ── Export ────────────────────────────────────────────────────────
    int exportAdif(const QString& filePath, const LogbookFilter& filter = {}) const;
    int exportCabrillo(const QString& filePath,
                       const QString& contestId,
                       const LogbookFilter& filter = {}) const;

    // ── Static helpers ────────────────────────────────────────────────
    // Map a frequency in MHz to an ADIF band string ("20m", "70cm", ...).
    static QString bandFromFreqMhz(double mhz);

    // Map a TCI / Flex slice mode (USB/LSB/CW/AM/FM/DIGU/DIGL/...) to an
    // ADIF base mode + optional submode.  Submode is empty when not
    // applicable; mode may also be empty for ambiguous digital slots
    // (DIGU/DIGL/DIGI) — the entry form must then prompt the operator.
    static void    adifModeFromTciMode(const QString& tciMode,
                                       QString* adifMode,
                                       QString* adifSubmode);

    // Compose an ADIF "<TAG:length>value " field (trailing space included
    // for readability — ADIF parsers ignore whitespace between fields).
    // Returns empty string if value is empty (omit empty fields per spec).
    static QString adifField(const QString& tag, const QString& value);

signals:
    void qsoAdded(qint64 id);
    void qsoUpdated(qint64 id);
    void qsoDeleted(qint64 id);
    void settingChanged(const QString& key);

private:
    bool migrateSchema();
    int  schemaVersion() const;
    bool setSchemaVersion(int v);

    static Qso qsoFromRow(class QSqlQuery& q);
    QString    filterToSql(const LogbookFilter& filter, QVariantList* binds) const;
    bool       importDuplicateExists(const Qso& q) const;

    QSqlDatabase m_db;
    QString      m_lastError;
    QString      m_connectionName;
};

} // namespace ShackLog
