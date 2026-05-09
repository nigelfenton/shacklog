#include "SettingsDialog.h"

#include "LogbookModel.h"
#include "AetherSettingsReader.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QTabWidget>

namespace ShackLog {

namespace {
const QStringList kContestIds = {
    "",
    "CQ-WW-CW", "CQ-WW-SSB", "CQ-WW-RTTY",
    "CQ-WPX-CW", "CQ-WPX-SSB", "CQ-WPX-RTTY",
    "ARRL-DX-CW", "ARRL-DX-SSB",
    "ARRL-FD",
    "ARRL-SS-CW", "ARRL-SS-SSB",
    "ARRL-10", "ARRL-160",
    "IARU-HF",
    "WAEDC-CW", "WAEDC-SSB",
    "RDXC", "IOTA",
    "BARTG-RTTY",
    "NAQP-CW", "NAQP-SSB",
};

const QStringList kCatOp      = { "SINGLE-OP", "MULTI-OP", "CHECKLOG" };
const QStringList kCatAssist  = { "NON-ASSISTED", "ASSISTED" };
const QStringList kCatBand    = { "ALL", "160M", "80M", "40M", "20M", "15M", "10M", "6M", "2M" };
const QStringList kCatMode    = { "MIXED", "CW", "SSB", "RTTY", "DIGI", "FM" };
const QStringList kCatPower   = { "HIGH", "LOW", "QRP" };
const QStringList kCatStation = { "FIXED", "MOBILE", "PORTABLE", "ROVER", "HQ", "SCHOOL" };
const QStringList kCatTx      = { "ONE", "TWO", "LIMITED", "UNLIMITED", "SWL" };
} // namespace

SettingsDialog::SettingsDialog(LogbookModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle("ShackLog Settings");
    setMinimumWidth(560);
    buildUI();
    populate();
}

void SettingsDialog::buildUI()
{
    auto* tabs = new QTabWidget;

    // ── Operator ────────────────────────────────────────────────────────
    auto* op = new QWidget;
    auto* opL = new QFormLayout(op);
    m_myCall       = new QLineEdit;
    m_myGrid       = new QLineEdit;
    m_myState      = new QLineEdit;
    m_defaultTxPwr = new QDoubleSpinBox;
    m_defaultTxPwr->setRange(0.0, 99999.0);
    m_defaultTxPwr->setDecimals(1);
    m_defaultTxPwr->setSuffix(" W");
    m_myOperator   = new QLineEdit;
    opL->addRow("My call",       m_myCall);
    opL->addRow("My grid",       m_myGrid);
    opL->addRow("My state",      m_myState);
    opL->addRow("Default power", m_defaultTxPwr);
    opL->addRow("Operator",      m_myOperator);
    tabs->addTab(op, "Operator");

    // ── TCI ─────────────────────────────────────────────────────────────
    auto* tci = new QWidget;
    auto* tciL = new QFormLayout(tci);
    m_tciHost = new QLineEdit;
    m_tciHost->setPlaceholderText("127.0.0.1");
    m_tciPort = new QSpinBox;
    m_tciPort->setRange(1, 65535);
    m_tciAutoConnect = new QCheckBox("Connect to TCI server on launch");
    tciL->addRow("Host", m_tciHost);
    tciL->addRow("Port", m_tciPort);
    tciL->addRow(m_tciAutoConnect);
    tabs->addTab(tci, "TCI");

    // ── DX Cluster ──────────────────────────────────────────────────────
    auto* dxc = new QWidget;
    auto* dxcL = new QFormLayout(dxc);
    m_dxcEnable     = new QCheckBox("Enable DX cluster spotting (auto-fill CALL on QSY)");
    m_dxcAutoDetect = new QCheckBox("Auto-detect cluster from AetherSDR's settings file");
    m_dxcHost       = new QLineEdit;
    m_dxcHost->setPlaceholderText("dxc.nc7j.com");
    m_dxcPort       = new QSpinBox;
    m_dxcPort->setRange(1, 65535);
    m_dxcPort->setValue(7300);
    m_dxcCallsign   = new QLineEdit;
    m_dxcCallsign->setPlaceholderText("G0JKN");
    m_dxcLoginSuffix = new QComboBox;
    m_dxcLoginSuffix->setEditable(true);
    m_dxcLoginSuffix->addItem("-2",  "-2");      // DXSpider preferred
    m_dxcLoginSuffix->addItem("-1",  "-1");
    m_dxcLoginSuffix->addItem("-3",  "-3");
    m_dxcLoginSuffix->addItem("-L (CC Cluster / AR-Cluster)", "-L");
    m_dxcLoginSuffix->addItem("(none — bare callsign)",       "");
    m_dxcDetected   = new QLabel("(no AetherSDR config detected)");
    m_dxcDetected->setStyleSheet("QLabel { color: #6b8099; font-size: 10px; }");
    m_dxcDetected->setWordWrap(true);

    dxcL->addRow(m_dxcEnable);
    dxcL->addRow(m_dxcAutoDetect);
    dxcL->addRow("Detected", m_dxcDetected);
    dxcL->addRow("Host (override)",     m_dxcHost);
    dxcL->addRow("Port (override)",     m_dxcPort);
    dxcL->addRow("Callsign (override)", m_dxcCallsign);
    dxcL->addRow("Login suffix",        m_dxcLoginSuffix);

    // ── POTA section (lives on the same tab — both feed the SpotIndex) ──
    auto* potaSep = new QLabel("─── POTA (api.pota.app) ───");
    potaSep->setStyleSheet("QLabel { color: #6b8099; font-size: 9px; "
                           "font-weight: bold; letter-spacing: 0.08em; }");
    dxcL->addRow(potaSep);

    m_potaEnable  = new QCheckBox("Enable POTA spotting (Parks On The Air HTTP feed)");
    m_potaPollSec = new QSpinBox;
    m_potaPollSec->setRange(5, 600);
    m_potaPollSec->setSuffix(" s");
    dxcL->addRow(m_potaEnable);
    dxcL->addRow("POTA poll interval", m_potaPollSec);

    auto refreshDxcEditable = [this]() {
        const bool manual = !m_dxcAutoDetect->isChecked();
        m_dxcHost->setEnabled(manual);
        m_dxcPort->setEnabled(manual);
        m_dxcCallsign->setEnabled(manual);
    };
    connect(m_dxcAutoDetect, &QCheckBox::toggled, this, [refreshDxcEditable](bool){
        refreshDxcEditable();
    });

    tabs->addTab(dxc, "DX Cluster");

    // ── Contest ─────────────────────────────────────────────────────────
    auto* ctst = new QWidget;
    auto* ctstL = new QFormLayout(ctst);
    m_contestMode = new QCheckBox("Contest mode (show contest panel in main window)");
    m_contestId   = new QComboBox;
    m_contestId->setEditable(true);
    m_contestId->addItems(kContestIds);
    m_stxNext = new QSpinBox;
    m_stxNext->setRange(1, 999999);
    ctstL->addRow(m_contestMode);
    ctstL->addRow("Contest ID",      m_contestId);
    ctstL->addRow("Next STX serial", m_stxNext);
    tabs->addTab(ctst, "Contest");

    // ── Cabrillo ────────────────────────────────────────────────────────
    auto* cab = new QWidget;
    auto* cabL = new QFormLayout(cab);
    m_cbName     = new QLineEdit;
    m_cbAddress  = new QLineEdit;
    m_cbEmail    = new QLineEdit;
    m_cbClub     = new QLineEdit;
    m_cbLocation = new QLineEdit;
    auto buildCombo = [](const QStringList& items) {
        auto* c = new QComboBox; c->addItems(items); return c;
    };
    m_cbCatOp          = buildCombo(kCatOp);
    m_cbCatAssisted    = buildCombo(kCatAssist);
    m_cbCatBand        = buildCombo(kCatBand);
    m_cbCatMode        = buildCombo(kCatMode);
    m_cbCatPower       = buildCombo(kCatPower);
    m_cbCatStation     = buildCombo(kCatStation);
    m_cbCatTransmitter = buildCombo(kCatTx);
    cabL->addRow("Name",          m_cbName);
    cabL->addRow("Address",       m_cbAddress);
    cabL->addRow("Email",         m_cbEmail);
    cabL->addRow("Club",          m_cbClub);
    cabL->addRow("Location",      m_cbLocation);
    cabL->addRow("Operator cat",  m_cbCatOp);
    cabL->addRow("Assisted",      m_cbCatAssisted);
    cabL->addRow("Band cat",      m_cbCatBand);
    cabL->addRow("Mode cat",      m_cbCatMode);
    cabL->addRow("Power cat",     m_cbCatPower);
    cabL->addRow("Station cat",   m_cbCatStation);
    cabL->addRow("Transmitter",   m_cbCatTransmitter);
    tabs->addTab(cab, "Cabrillo");

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* main = new QVBoxLayout(this);
    main->addWidget(tabs);
    main->addWidget(btns);
}

void SettingsDialog::populate()
{
    m_myCall->setText(m_model->myCall());
    m_myGrid->setText(m_model->myGridsquare());
    m_myState->setText(m_model->myState());
    m_defaultTxPwr->setValue(m_model->defaultTxPwr());
    m_myOperator->setText(m_model->settingValue("MY_OPERATOR"));

    m_tciHost->setText(m_model->settingValue("TCI_HOST", "127.0.0.1"));
    m_tciPort->setValue(m_model->settingValue("TCI_PORT", "40001").toInt());
    m_tciAutoConnect->setChecked(m_model->settingValue("TCI_AUTOCONNECT", "1") == "1");

    // DX Cluster — auto-detect from AetherSDR's config, default to "on" if
    // a cluster is configured there; otherwise off until the user opts in.
    const auto aether = AetherSettingsReader::readDxClusterConfig();
    if (aether.found) {
        m_dxcDetected->setText(
            QString("AetherSDR: %1:%2 as %3 (login will be sent as %3-L)")
                .arg(aether.host).arg(aether.port).arg(aether.callsign));
    } else {
        m_dxcDetected->setText(
            "(AetherSDR config not found at " + AetherSettingsReader::defaultSettingsPath()
            + " — uncheck auto-detect to enter cluster details manually)");
    }
    const QString defEnable = aether.found ? "1" : "0";
    m_dxcEnable->setChecked(m_model->settingValue("DXC_ENABLE", defEnable) == "1");
    m_dxcAutoDetect->setChecked(m_model->settingValue("DXC_AUTODETECT", "1") == "1");
    m_dxcHost->setText(m_model->settingValue("DXC_HOST",
                            aether.found ? aether.host : QString("dxc.nc7j.com")));
    m_dxcPort->setValue(m_model->settingValue("DXC_PORT",
                            QString::number(aether.found ? aether.port : 7300)).toInt());
    m_dxcCallsign->setText(m_model->settingValue("DXC_CALLSIGN", aether.callsign));
    const QString storedSuffix = m_model->settingValue("DXC_LOGIN_SUFFIX", "-2");
    int suffixIdx = -1;
    for (int i = 0; i < m_dxcLoginSuffix->count(); ++i) {
        if (m_dxcLoginSuffix->itemData(i).toString() == storedSuffix) {
            suffixIdx = i;
            break;
        }
    }
    if (suffixIdx >= 0) m_dxcLoginSuffix->setCurrentIndex(suffixIdx);
    else                m_dxcLoginSuffix->setEditText(storedSuffix);

    m_potaEnable->setChecked(m_model->settingValue("POTA_ENABLE", "1") == "1");
    m_potaPollSec->setValue(m_model->settingValue("POTA_POLL_SEC", "30").toInt());

    const bool manual = !m_dxcAutoDetect->isChecked();
    m_dxcHost->setEnabled(manual);
    m_dxcPort->setEnabled(manual);
    m_dxcCallsign->setEnabled(manual);

    m_contestMode->setChecked(m_model->contestMode());
    const QString cid = m_model->contestId();
    int idx = m_contestId->findText(cid);
    if (idx >= 0) m_contestId->setCurrentIndex(idx);
    else          m_contestId->setEditText(cid);
    m_stxNext->setValue(m_model->settingValue("CONTEST_STX_NEXT", "1").toInt());

    m_cbName->setText(m_model->settingValue("CABRILLO_NAME"));
    m_cbAddress->setText(m_model->settingValue("CABRILLO_ADDRESS"));
    m_cbEmail->setText(m_model->settingValue("CABRILLO_EMAIL"));
    m_cbClub->setText(m_model->settingValue("CABRILLO_CLUB"));
    m_cbLocation->setText(m_model->settingValue("CABRILLO_LOCATION"));
    auto setCombo = [](QComboBox* c, const QString& v) {
        const int i = c->findText(v);
        if (i >= 0) c->setCurrentIndex(i);
    };
    setCombo(m_cbCatOp,          m_model->settingValue("CABRILLO_CAT_OPERATOR",     "SINGLE-OP"));
    setCombo(m_cbCatAssisted,    m_model->settingValue("CABRILLO_CAT_ASSISTED",     "NON-ASSISTED"));
    setCombo(m_cbCatBand,        m_model->settingValue("CABRILLO_CAT_BAND",         "ALL"));
    setCombo(m_cbCatMode,        m_model->settingValue("CABRILLO_CAT_MODE",         "MIXED"));
    setCombo(m_cbCatPower,       m_model->settingValue("CABRILLO_CAT_POWER",        "HIGH"));
    setCombo(m_cbCatStation,     m_model->settingValue("CABRILLO_CAT_STATION",      "FIXED"));
    setCombo(m_cbCatTransmitter, m_model->settingValue("CABRILLO_CAT_TRANSMITTER",  "ONE"));
}

void SettingsDialog::onAccept()
{
    m_model->setSetting("MY_CALL",          m_myCall->text().trimmed().toUpper());
    m_model->setSetting("MY_GRIDSQUARE",    m_myGrid->text().trimmed());
    m_model->setSetting("MY_STATE",         m_myState->text().trimmed().toUpper());
    m_model->setSetting("DEFAULT_TX_PWR",   QString::number(m_defaultTxPwr->value(), 'f', 1));
    m_model->setSetting("MY_OPERATOR",      m_myOperator->text().trimmed().toUpper());

    m_model->setSetting("TCI_HOST",         m_tciHost->text().trimmed().isEmpty()
                                              ? QStringLiteral("127.0.0.1")
                                              : m_tciHost->text().trimmed());
    m_model->setSetting("TCI_PORT",         QString::number(m_tciPort->value()));
    m_model->setSetting("TCI_AUTOCONNECT",  m_tciAutoConnect->isChecked() ? "1" : "0");

    m_model->setSetting("DXC_ENABLE",       m_dxcEnable->isChecked() ? "1" : "0");
    m_model->setSetting("DXC_AUTODETECT",   m_dxcAutoDetect->isChecked() ? "1" : "0");
    m_model->setSetting("DXC_HOST",         m_dxcHost->text().trimmed());
    m_model->setSetting("DXC_PORT",         QString::number(m_dxcPort->value()));
    m_model->setSetting("DXC_CALLSIGN",     m_dxcCallsign->text().trimmed().toUpper());
    // Stored value is the canonical suffix string (the part actually appended
    // to the callsign).  If the user typed something custom we save the raw
    // text; otherwise we save the data() slot of the selected combo entry.
    {
        const int idx = m_dxcLoginSuffix->currentIndex();
        QString suffix;
        if (idx >= 0 && m_dxcLoginSuffix->currentText() == m_dxcLoginSuffix->itemText(idx)) {
            suffix = m_dxcLoginSuffix->itemData(idx).toString();
        } else {
            suffix = m_dxcLoginSuffix->currentText().trimmed();
        }
        m_model->setSetting("DXC_LOGIN_SUFFIX", suffix);
    }

    m_model->setSetting("POTA_ENABLE",   m_potaEnable->isChecked() ? "1" : "0");
    m_model->setSetting("POTA_POLL_SEC", QString::number(m_potaPollSec->value()));

    m_model->setSetting("CONTEST_MODE",     m_contestMode->isChecked() ? "1" : "0");
    m_model->setSetting("CONTEST_ID",       m_contestId->currentText().trimmed().toUpper());
    m_model->setSetting("CONTEST_STX_NEXT", QString::number(m_stxNext->value()));

    m_model->setSetting("CABRILLO_NAME",      m_cbName->text().trimmed());
    m_model->setSetting("CABRILLO_ADDRESS",   m_cbAddress->text().trimmed());
    m_model->setSetting("CABRILLO_EMAIL",     m_cbEmail->text().trimmed());
    m_model->setSetting("CABRILLO_CLUB",      m_cbClub->text().trimmed());
    m_model->setSetting("CABRILLO_LOCATION",  m_cbLocation->text().trimmed());
    m_model->setSetting("CABRILLO_CAT_OPERATOR",    m_cbCatOp->currentText());
    m_model->setSetting("CABRILLO_CAT_ASSISTED",    m_cbCatAssisted->currentText());
    m_model->setSetting("CABRILLO_CAT_BAND",        m_cbCatBand->currentText());
    m_model->setSetting("CABRILLO_CAT_MODE",        m_cbCatMode->currentText());
    m_model->setSetting("CABRILLO_CAT_POWER",       m_cbCatPower->currentText());
    m_model->setSetting("CABRILLO_CAT_STATION",     m_cbCatStation->currentText());
    m_model->setSetting("CABRILLO_CAT_TRANSMITTER", m_cbCatTransmitter->currentText());

    accept();
}

} // namespace ShackLog
