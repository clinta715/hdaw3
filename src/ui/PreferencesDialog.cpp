#include "PreferencesDialog.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>

static const char* kKeyClipDur = "defaultClipDuration";
static const char* kKeySnap = "snapEnabled";
static const char* kKeySnapDiv = "snapDivision";
static const char* kKeyMcpHost = "mcp/httpHost";
static const char* kKeyMcpPort = "mcp/httpPort";
static const char* kKeyMcpEnabled = "mcp/httpEnabled";

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Preferences");
    setMinimumWidth(400);
    setModal(true);

    auto* mainLayout = new QVBoxLayout(this);

    // MIDI section
    auto* midiGroup = new QGroupBox("MIDI", this);
    auto* midiLayout = new QFormLayout(midiGroup);

    clipDurSpinBox = new QDoubleSpinBox(midiGroup);
    clipDurSpinBox->setRange(0.5, 64.0);
    clipDurSpinBox->setSingleStep(0.5);
    clipDurSpinBox->setDecimals(1);
    clipDurSpinBox->setSuffix("s");
    clipDurSpinBox->setValue(4.0);
    midiLayout->addRow("Default clip duration:", clipDurSpinBox);

    mainLayout->addWidget(midiGroup);

    // Timeline section
    auto* timelineGroup = new QGroupBox("Timeline", this);
    auto* timelineLayout = new QFormLayout(timelineGroup);

    snapCheckBox = new QCheckBox("Enable snap by default", timelineGroup);
    snapCheckBox->setChecked(true);
    timelineLayout->addRow("Snap:", snapCheckBox);

    snapDivisionCombo = new QComboBox(timelineGroup);
    snapDivisionCombo->addItems({"Bar", "Beat", "1/4", "1/8", "1/16", "Off"});
    snapDivisionCombo->setCurrentIndex(1);
    timelineLayout->addRow("Snap division:", snapDivisionCombo);

    mainLayout->addWidget(timelineGroup);

    // MCP section
    auto* mcpGroup = new QGroupBox("MCP Server (HTTP)", this);
    auto* mcpLayout = new QFormLayout(mcpGroup);

    mcpHostEdit = new QLineEdit(mcpGroup);
    mcpHostEdit->setText("127.0.0.1");
    mcpLayout->addRow("Host:", mcpHostEdit);

    auto* mcpHostNote = new QLabel(
        "Any address other than loopback requires authentication, "
        "which HDAW v0.3.x does not provide.", mcpGroup);
    mcpHostNote->setWordWrap(true);
    mcpHostNote->setStyleSheet(QString("color: %1;").arg(ThemeColors::textSecondary().name()));
    mcpLayout->addRow("", mcpHostNote);

    mcpPortSpin = new QSpinBox(mcpGroup);
    mcpPortSpin->setRange(1024, 65535);
    mcpPortSpin->setValue(8765);
    mcpLayout->addRow("Port:", mcpPortSpin);

    mcpAutoStartCheck = new QCheckBox("Auto-start MCP HTTP server at launch", mcpGroup);
    mcpLayout->addRow("", mcpAutoStartCheck);

    mainLayout->addWidget(mcpGroup);

    mainLayout->addStretch();

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* saveBtn = new QPushButton("Save", this);
    connect(saveBtn, &QPushButton::clicked, this, &PreferencesDialog::onSave);
    btnLayout->addWidget(saveBtn);

    auto* applyBtn = new QPushButton("Apply", this);
    connect(applyBtn, &QPushButton::clicked, this, &PreferencesDialog::onApply);
    btnLayout->addWidget(applyBtn);

    auto* cancelBtn = new QPushButton("Cancel", this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    mainLayout->addLayout(btnLayout);

    loadSettings();
}

QSettings& PreferencesDialog::settings()
{
    static QSettings s(kSettingsOrg, kSettingsApp);
    return s;
}

void PreferencesDialog::loadSettings()
{
    auto& settings = PreferencesDialog::settings();
    clipDurSpinBox->setValue(settings.value(kKeyClipDur, 4.0).toDouble());
    snapCheckBox->setChecked(settings.value(kKeySnap, true).toBool());
    snapDivisionCombo->setCurrentIndex(settings.value(kKeySnapDiv, 1).toInt());
    mcpHostEdit->setText(settings.value(kKeyMcpHost, "127.0.0.1").toString());
    mcpPortSpin->setValue(settings.value(kKeyMcpPort, 8765).toInt());
    mcpAutoStartCheck->setChecked(settings.value(kKeyMcpEnabled, false).toBool());
}

void PreferencesDialog::onSave()
{
    onApply();
    accept();
}

void PreferencesDialog::onApply()
{
    auto& settings = PreferencesDialog::settings();
    settings.setValue(kKeyClipDur, clipDurSpinBox->value());
    settings.setValue(kKeySnap, snapCheckBox->isChecked());
    settings.setValue(kKeySnapDiv, snapDivisionCombo->currentIndex());
    settings.setValue(kKeyMcpHost, mcpHostEdit->text());
    settings.setValue(kKeyMcpPort, mcpPortSpin->value());
    settings.setValue(kKeyMcpEnabled, mcpAutoStartCheck->isChecked());
    emit preferencesApplied();
}

// Static accessors
double PreferencesDialog::getDefaultClipDuration()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeyClipDur, 4.0).toDouble();
}

void PreferencesDialog::setDefaultClipDuration(double dur)
{
    auto& settings = PreferencesDialog::settings();
    settings.setValue(kKeyClipDur, dur);
}

bool PreferencesDialog::getSnapEnabled()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeySnap, true).toBool();
}

void PreferencesDialog::setSnapEnabled(bool en)
{
    auto& settings = PreferencesDialog::settings();
    settings.setValue(kKeySnap, en);
}

int PreferencesDialog::getSnapDivision()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeySnapDiv, 1).toInt();
}

void PreferencesDialog::setSnapDivision(int idx)
{
    auto& settings = PreferencesDialog::settings();
    settings.setValue(kKeySnapDiv, idx);
}
