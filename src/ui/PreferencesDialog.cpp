#include "PreferencesDialog.h"
#include "Theme.h"
#include "../engine/AudioEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QDir>
#include <QFileDialog>

static const char* kKeyClipDur = "defaultClipDuration";
static const char* kKeySnap = "snapEnabled";
static const char* kKeySnapDiv = "snapDivision";
static const char* kKeyPianoRollSnap = "pianoRoll/snapEnabled";
static const char* kKeyPianoRollSnapDiv = "pianoRoll/snapDivision";
static const char* kKeyMcpHost = "mcp/httpHost";
static const char* kKeyMcpPort = "mcp/httpPort";
static const char* kKeyMcpEnabled = "mcp/httpEnabled";

PreferencesDialog::PreferencesDialog(AudioEngine* engine, QWidget* parent)
    : QDialog(parent), audioEngine(engine)
{
    setWindowTitle("Preferences");
    setMinimumWidth(500);
    setModal(true);

    auto* mainLayout = new QVBoxLayout(this);

    // Audio section (only if engine is available)
    if (audioEngine != nullptr)
    {
        auto* audioGroup = new QGroupBox("Audio", this);
        auto* audioLayout = new QFormLayout(audioGroup);
        buildAudioSettings(audioLayout);
        mainLayout->addWidget(audioGroup);
    }

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

    countInBarsSpin = new QSpinBox(midiGroup);
    countInBarsSpin->setRange(0, 8);
    countInBarsSpin->setSuffix(" bars");
    countInBarsSpin->setToolTip("0 = no count-in");
    midiLayout->addRow("Count-in:", countInBarsSpin);

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

    // Default Directories section
    auto* dirsGroup = new QGroupBox("Default Directories", this);
    auto* dirsLayout = new QFormLayout(dirsGroup);

    auto makeDirRow = [&](QLineEdit*& edit) {
        auto* row = new QWidget(dirsGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        edit = new QLineEdit(row);
        edit->setPlaceholderText("(not set)");
        edit->setReadOnly(true);
        rowLayout->addWidget(edit, 1);

        auto* browseBtn = new QPushButton("Browse...", row);
        auto* clearBtn = new QPushButton("Clear", row);
        rowLayout->addWidget(browseBtn);
        rowLayout->addWidget(clearBtn);

        connect(browseBtn, &QPushButton::clicked, this, [this, edit]() {
            QString dir = QFileDialog::getExistingDirectory(this, "Choose Directory",
                edit->text().isEmpty() ? QDir::homePath() : edit->text());
            if (!dir.isEmpty())
                edit->setText(QDir::toNativeSeparators(dir));
        });
        connect(clearBtn, &QPushButton::clicked, this, [edit]() {
            edit->clear();
        });

        return row;
    };

    dirsLayout->addRow("Project folder:", makeDirRow(defaultProjectDirEdit));
    dirsLayout->addRow("Audio samples:", makeDirRow(defaultAudioDirEdit));
    dirsLayout->addRow("MIDI files:", makeDirRow(defaultMidiDirEdit));

    mainLayout->addWidget(dirsGroup);

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

void PreferencesDialog::buildAudioSettings(QFormLayout* layout)
{
    deviceTypeCombo = new QComboBox(layout->parentWidget());
    layout->addRow("Driver:", deviceTypeCombo);

    outputDeviceCombo = new QComboBox(layout->parentWidget());
    layout->addRow("Output device:", outputDeviceCombo);

    inputDeviceCombo = new QComboBox(layout->parentWidget());
    layout->addRow("Input device:", inputDeviceCombo);

    sampleRateCombo = new QComboBox(layout->parentWidget());
    layout->addRow("Sample rate:", sampleRateCombo);

    bufferSizeCombo = new QComboBox(layout->parentWidget());
    layout->addRow("Buffer size:", bufferSizeCombo);

    latencyLabel = new QLabel("Latency: --", layout->parentWidget());
    latencyLabel->setStyleSheet(QString("color: %1;").arg(ThemeColors::textSecondary().name()));
    layout->addRow("", latencyLabel);

    // Populate and connect
    refreshAudioDevices();

    connect(deviceTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        applyAudioDeviceType();
        refreshAudioDevices();
    });
    connect(outputDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        applyAudioOutputDevice();
        refreshSampleRatesAndBufferSizes();
    });
    connect(inputDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        applyAudioInputDevice();
    });
    connect(sampleRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        applySampleRate();
    });
    connect(bufferSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        applyBufferSize();
    });
}

void PreferencesDialog::refreshAudioDevices()
{
    if (audioEngine == nullptr) return;
    auto& dm = audioEngine->getDeviceManager();

    // Populate device types
    deviceTypeCombo->blockSignals(true);
    deviceTypeCombo->clear();
    for (auto* type : dm.getAvailableDeviceTypes())
        deviceTypeCombo->addItem(QString::fromUtf8(type->getTypeName().toRawUTF8()),
                                 QString::fromUtf8(type->getTypeName().toRawUTF8()));

    auto currentTypeName = dm.getCurrentAudioDeviceType();
    int idx = deviceTypeCombo->findData(QString::fromUtf8(currentTypeName.toRawUTF8()));
    deviceTypeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    deviceTypeCombo->blockSignals(false);

    // Populate output and input devices for current type
    auto setup = dm.getAudioDeviceSetup();
    auto* devType = dm.getCurrentDeviceTypeObject();
    if (devType != nullptr)
    {
        outputDeviceCombo->blockSignals(true);
        outputDeviceCombo->clear();
        outputDeviceCombo->addItem("(none)");
        for (const auto& name : devType->getDeviceNames(false))
            outputDeviceCombo->addItem(QString::fromUtf8(name.toRawUTF8()));
        int oi = outputDeviceCombo->findText(QString::fromUtf8(setup.outputDeviceName.toRawUTF8()));
        outputDeviceCombo->setCurrentIndex(oi >= 0 ? oi : 0);
        outputDeviceCombo->blockSignals(false);

        inputDeviceCombo->blockSignals(true);
        inputDeviceCombo->clear();
        inputDeviceCombo->addItem("(none)");
        for (const auto& name : devType->getDeviceNames(true))
            inputDeviceCombo->addItem(QString::fromUtf8(name.toRawUTF8()));
        int ii = inputDeviceCombo->findText(QString::fromUtf8(setup.inputDeviceName.toRawUTF8()));
        inputDeviceCombo->setCurrentIndex(ii >= 0 ? ii : 0);
        inputDeviceCombo->blockSignals(false);
    }

    refreshSampleRatesAndBufferSizes();
}

void PreferencesDialog::refreshSampleRatesAndBufferSizes()
{
    if (audioEngine == nullptr) return;
    auto* dev = audioEngine->getDeviceManager().getCurrentAudioDevice();
    if (dev == nullptr)
    {
        sampleRateCombo->clear();
        bufferSizeCombo->clear();
        latencyLabel->setText("Latency: --");
        return;
    }

    double sr = dev->getCurrentSampleRate();
    int bs = dev->getCurrentBufferSizeSamples();

    sampleRateCombo->blockSignals(true);
    sampleRateCombo->clear();
    for (double rate : dev->getAvailableSampleRates())
    {
        QString label = QString::number(static_cast<int>(rate));
        sampleRateCombo->addItem(label, rate);
    }
    int si = sampleRateCombo->findData(sr);
    sampleRateCombo->setCurrentIndex(si >= 0 ? si : 0);
    sampleRateCombo->blockSignals(false);

    bufferSizeCombo->blockSignals(true);
    bufferSizeCombo->clear();
    for (int buf : dev->getAvailableBufferSizes())
    {
        QString label = QString::number(buf);
        bufferSizeCombo->addItem(label, buf);
    }
    int bi = bufferSizeCombo->findData(bs);
    bufferSizeCombo->setCurrentIndex(bi >= 0 ? bi : 0);
    bufferSizeCombo->blockSignals(false);

    double latencyMs = static_cast<double>(bs) / sr * 1000.0;
    latencyLabel->setText(QString("Latency: %1 ms").arg(latencyMs, 0, 'f', 1));
}

void PreferencesDialog::applyAudioDeviceType()
{
    if (audioEngine == nullptr) return;
    auto typeName = deviceTypeCombo->currentData().toString().toStdString();
    audioEngine->getDeviceManager().setCurrentAudioDeviceType(
        juce::String::fromUTF8(typeName.c_str()), true);
}

void PreferencesDialog::applyAudioOutputDevice()
{
    if (audioEngine == nullptr) return;
    auto& dm = audioEngine->getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    QString name = outputDeviceCombo->currentText();
    setup.outputDeviceName = (name == "(none)") ? juce::String() : juce::String::fromUTF8(name.toUtf8().constData());
    dm.setAudioDeviceSetup(setup, true);
}

void PreferencesDialog::applyAudioInputDevice()
{
    if (audioEngine == nullptr) return;
    auto& dm = audioEngine->getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    QString name = inputDeviceCombo->currentText();
    setup.inputDeviceName = (name == "(none)") ? juce::String() : juce::String::fromUTF8(name.toUtf8().constData());
    dm.setAudioDeviceSetup(setup, true);
}

void PreferencesDialog::applySampleRate()
{
    if (audioEngine == nullptr) return;
    auto& dm = audioEngine->getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    setup.sampleRate = sampleRateCombo->currentData().toDouble();
    dm.setAudioDeviceSetup(setup, true);
    refreshSampleRatesAndBufferSizes();
}

void PreferencesDialog::applyBufferSize()
{
    if (audioEngine == nullptr) return;
    auto& dm = audioEngine->getDeviceManager();
    auto setup = dm.getAudioDeviceSetup();
    setup.bufferSize = bufferSizeCombo->currentData().toInt();
    dm.setAudioDeviceSetup(setup, true);
    refreshSampleRatesAndBufferSizes();
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
    if (countInBarsSpin != nullptr)
        countInBarsSpin->setValue(settings.value(kKeyCountInBars, 1).toInt());
    defaultProjectDirEdit->setText(settings.value(kKeyDefaultProjectDir).toString());
    defaultAudioDirEdit->setText(settings.value(kKeyDefaultAudioDir).toString());
    defaultMidiDirEdit->setText(settings.value(kKeyDefaultMidiDir).toString());
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
    if (countInBarsSpin != nullptr)
        settings.setValue(kKeyCountInBars, countInBarsSpin->value());
    settings.setValue(kKeyDefaultProjectDir, defaultProjectDirEdit->text());
    settings.setValue(kKeyDefaultAudioDir, defaultAudioDirEdit->text());
    settings.setValue(kKeyDefaultMidiDir, defaultMidiDirEdit->text());
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

bool PreferencesDialog::getPianoRollSnapEnabled()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeyPianoRollSnap, true).toBool();
}

void PreferencesDialog::setPianoRollSnapEnabled(bool en)
{
    auto& settings = PreferencesDialog::settings();
    settings.setValue(kKeyPianoRollSnap, en);
}

int PreferencesDialog::getPianoRollSnapDivision()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeyPianoRollSnapDiv, 4).toInt(); // default: 1/16
}

void PreferencesDialog::setPianoRollSnapDivision(int idx)
{
    auto& settings = PreferencesDialog::settings();
    settings.setValue(kKeyPianoRollSnapDiv, idx);
}

QString PreferencesDialog::getDefaultProjectDir()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeyDefaultProjectDir).toString();
}

QString PreferencesDialog::getDefaultAudioDir()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeyDefaultAudioDir).toString();
}

QString PreferencesDialog::getDefaultMidiDir()
{
    auto& settings = PreferencesDialog::settings();
    return settings.value(kKeyDefaultMidiDir).toString();
}
