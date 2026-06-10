#include "PreferencesDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>

static const char* kSettingsOrg = "HDAW";
static const char* kSettingsApp = "HDAW";
static const char* kKeyClipDur = "defaultClipDuration";
static const char* kKeySnap = "snapEnabled";
static const char* kKeySnapDiv = "snapDivision";

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Preferences");
    setMinimumWidth(350);
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

void PreferencesDialog::loadSettings()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    clipDurSpinBox->setValue(settings.value(kKeyClipDur, 4.0).toDouble());
    snapCheckBox->setChecked(settings.value(kKeySnap, true).toBool());
    snapDivisionCombo->setCurrentIndex(settings.value(kKeySnapDiv, 1).toInt());
}

void PreferencesDialog::onSave()
{
    onApply();
    accept();
}

void PreferencesDialog::onApply()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(kKeyClipDur, clipDurSpinBox->value());
    settings.setValue(kKeySnap, snapCheckBox->isChecked());
    settings.setValue(kKeySnapDiv, snapDivisionCombo->currentIndex());
    emit preferencesApplied();
}

// Static accessors
double PreferencesDialog::getDefaultClipDuration()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    return settings.value(kKeyClipDur, 4.0).toDouble();
}

void PreferencesDialog::setDefaultClipDuration(double dur)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(kKeyClipDur, dur);
}

bool PreferencesDialog::getSnapEnabled()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    return settings.value(kKeySnap, true).toBool();
}

void PreferencesDialog::setSnapEnabled(bool en)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(kKeySnap, en);
}

int PreferencesDialog::getSnapDivision()
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    return settings.value(kKeySnapDiv, 1).toInt();
}

void PreferencesDialog::setSnapDivision(int idx)
{
    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(kKeySnapDiv, idx);
}
