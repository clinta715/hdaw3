#include "TimelineToolbar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>
#include "../engine/PluginManager.h"

TimelineToolbar::TimelineToolbar(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet(
        "TimelineToolbar { background: rgba(28, 28, 31, 220); border-bottom: 1px solid #3a3a3e; }");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    // Add Track button with template dropdown
    addTrackBtn = new QToolButton(this);
    addTrackBtn->setText("+");
    addTrackBtn->setFixedSize(22, 22);
    addTrackBtn->setToolTip("Add Track");
    addTrackBtn->setStyleSheet(
        "QToolButton { font-weight: bold; font-size: 11pt; }"
        "QToolButton:hover { border-color: #d97706; }");
    addTrackBtn->setPopupMode(QToolButton::MenuButtonPopup);

    trackMenu = new QMenu(addTrackBtn);

    auto* emptyAction = trackMenu->addAction("Empty Track");
    connect(emptyAction, &QAction::triggered, this, &TimelineToolbar::addTrackClicked);

    trackMenu->addSeparator();
    auto* withEqAction = trackMenu->addAction("Track with EQ");
    connect(withEqAction, &QAction::triggered, this, [this]() { emit addTrackWithFX("eq"); });
    auto* withCompAction = trackMenu->addAction("Track with Compressor");
    connect(withCompAction, &QAction::triggered, this, [this]() { emit addTrackWithFX("compressor"); });
    auto* withRevAction = trackMenu->addAction("Track with Reverb");
    connect(withRevAction, &QAction::triggered, this, [this]() { emit addTrackWithFX("reverb"); });
    auto* withDelAction = trackMenu->addAction("Track with Delay");
    connect(withDelAction, &QAction::triggered, this, [this]() { emit addTrackWithFX("delay"); });

    addTrackBtn->setMenu(trackMenu);
    connect(addTrackBtn, &QToolButton::clicked, this, &TimelineToolbar::addTrackClicked);
    layout->addWidget(addTrackBtn);

    layout->addSpacing(4);

    // Snap toggle
    snapBtn = new QPushButton("Snap", this);
    snapBtn->setCheckable(true);
    snapBtn->setChecked(true);
    snapBtn->setFixedHeight(22);
    connect(snapBtn, &QPushButton::toggled, this, &TimelineToolbar::snapToggleChanged);
    layout->addWidget(snapBtn);

    // Snap division
    snapCombo = new QComboBox(this);
    snapCombo->addItems({"Bar", "Beat", "1/4", "1/8", "1/16", "Off"});
    snapCombo->setCurrentIndex(1);
    snapCombo->setFixedHeight(22);
    connect(snapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TimelineToolbar::snapDivisionChanged);
    layout->addWidget(snapCombo);

    layout->addSpacing(8);

    // Zoom controls
    zoomOutBtn = new QPushButton("-", this);
    zoomOutBtn->setFixedSize(22, 22);
    connect(zoomOutBtn, &QPushButton::clicked, this, &TimelineToolbar::zoomOutClicked);
    layout->addWidget(zoomOutBtn);

    zoomInBtn = new QPushButton("+", this);
    zoomInBtn->setFixedSize(22, 22);
    connect(zoomInBtn, &QPushButton::clicked, this, &TimelineToolbar::zoomInClicked);
    layout->addWidget(zoomInBtn);

    layout->addSpacing(8);

    // Grid type
    auto* gridLabel = new QLabel("Grid:", this);
    layout->addWidget(gridLabel);

    gridCombo = new QComboBox(this);
    gridCombo->addItems({"Bars:Beats", "Time"});
    gridCombo->setFixedHeight(22);
    connect(gridCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) { emit gridTypeChanged(idx == 0); });
    layout->addWidget(gridCombo);

    layout->addSpacing(8);

    // BPM spinbox
    auto* bpmLabel = new QLabel("BPM:", this);
    layout->addWidget(bpmLabel);

    bpmSpinBox = new QDoubleSpinBox(this);
    bpmSpinBox->setRange(20.0, 999.0);
    bpmSpinBox->setValue(120.0);
    bpmSpinBox->setSingleStep(1.0);
    bpmSpinBox->setDecimals(0);
    bpmSpinBox->setFixedWidth(50);
    bpmSpinBox->setFixedHeight(22);
    bpmSpinBox->setStyleSheet(
        "QDoubleSpinBox { background: #1a1a1e; color: #e8e8ec; border: 1px solid #3a3a40; "
        "border-radius: 2px; padding: 1px 2px; }");
    connect(bpmSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TimelineToolbar::bpmChanged);
    layout->addWidget(bpmSpinBox);

    // Time signature
    timeSigCombo = new QComboBox(this);
    timeSigCombo->addItems({"2/4", "3/4", "4/4", "5/4", "6/8", "7/8"});
    timeSigCombo->setCurrentText("4/4");
    timeSigCombo->setFixedHeight(22);
    timeSigCombo->setFixedWidth(48);
    timeSigCombo->setStyleSheet(
        "QComboBox { background: #1a1a1e; color: #e8e8ec; border: 1px solid #3a3a40; "
        "border-radius: 2px; padding: 1px 2px; font-family: monospace; font-size: 8pt; }");
    connect(timeSigCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TimelineToolbar::onTimeSigChanged);
    layout->addWidget(timeSigCombo);

    // MIDI device selector
    auto* midiLabel = new QLabel("MIDI:", this);
    midiLabel->setStyleSheet("QLabel { color: #a8a8b0; font-size: 7pt; }");
    layout->addWidget(midiLabel);

    midiDeviceCombo = new QComboBox(this);
    midiDeviceCombo->addItem("None");
    midiDeviceCombo->setFixedHeight(22);
    midiDeviceCombo->setFixedWidth(100);
    midiDeviceCombo->setStyleSheet(
        "QComboBox { background: #1a1a1e; color: #e8e8ec; border: 1px solid #3a3a40; "
        "border-radius: 2px; padding: 1px 2px; font-size: 7pt; }");
    connect(midiDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TimelineToolbar::onMidiDeviceChanged);
    layout->addWidget(midiDeviceCombo);

    layout->addSpacing(4);

    // Metronome toggle
    metronomeBtn = new QPushButton("Met", this);
    metronomeBtn->setCheckable(true);
    metronomeBtn->setFixedHeight(22);
    metronomeBtn->setStyleSheet(
        "QPushButton { color: #a8a8b0; }"
        "QPushButton:checked { color: #10b981; }");
    connect(metronomeBtn, &QPushButton::toggled, this, &TimelineToolbar::metronomeToggled);
    layout->addWidget(metronomeBtn);

    layout->addSpacing(4);

    // Default clip length
    auto* clipLenLabel = new QLabel("Clip:", this);
    clipLenLabel->setStyleSheet("QLabel { color: #a8a8b0; font-size: 7pt; }");
    layout->addWidget(clipLenLabel);

    defaultClipLenSpinBox = new QDoubleSpinBox(this);
    defaultClipLenSpinBox->setRange(0.5, 64.0);
    defaultClipLenSpinBox->setValue(4.0);
    defaultClipLenSpinBox->setSingleStep(0.5);
    defaultClipLenSpinBox->setDecimals(1);
    defaultClipLenSpinBox->setSuffix("s");
    defaultClipLenSpinBox->setFixedWidth(50);
    defaultClipLenSpinBox->setFixedHeight(22);
    defaultClipLenSpinBox->setToolTip("Default MIDI clip length (double-click to create)");
    defaultClipLenSpinBox->setStyleSheet(
        "QDoubleSpinBox { background: #1a1a1e; color: #e8e8ec; border: 1px solid #3a3a40; "
        "border-radius: 2px; padding: 1px 2px; }");
    connect(defaultClipLenSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TimelineToolbar::defaultClipLenChanged);
    layout->addWidget(defaultClipLenSpinBox);

    layout->addSpacing(8);

    // Loop toggle
    loopBtn = new QPushButton("Loop", this);
    loopBtn->setCheckable(true);
    loopBtn->setFixedHeight(22);
    connect(loopBtn, &QPushButton::toggled, this, &TimelineToolbar::loopToggleChanged);
    layout->addWidget(loopBtn);

    // Follow playhead
    followBtn = new QPushButton("Follow", this);
    followBtn->setCheckable(true);
    followBtn->setChecked(true);
    followBtn->setFixedHeight(22);
    connect(followBtn, &QPushButton::toggled, this, &TimelineToolbar::followPlayheadChanged);
    layout->addWidget(followBtn);

    layout->addSpacing(16);

    // Transport controls
    rewindBtn = new QPushButton("\xE2\x8F\xAE", this); // ⏮
    rewindBtn->setFixedSize(28, 22);
    connect(rewindBtn, &QPushButton::clicked, this, &TimelineToolbar::rewindClicked);
    layout->addWidget(rewindBtn);

    playBtn = new QPushButton("\xE2\x96\xB6", this); // ▶
    playBtn->setCheckable(true);
    playBtn->setFixedSize(28, 22);
    playBtn->setStyleSheet(
        "QPushButton:checked { color: #10b981; }");
    connect(playBtn, &QPushButton::clicked, this, &TimelineToolbar::playClicked);
    layout->addWidget(playBtn);

    stopBtn = new QPushButton("\xE2\x8F\xB9", this); // ⏹
    stopBtn->setFixedSize(28, 22);
    connect(stopBtn, &QPushButton::clicked, this, &TimelineToolbar::stopClicked);
    layout->addWidget(stopBtn);

    layout->addSpacing(4);

    // Record button
    recordBtn = new QPushButton("\xE2\x97\x8F Rec", this);
    recordBtn->setCheckable(true);
    recordBtn->setFixedHeight(22);
    recordBtn->setStyleSheet(
        "QPushButton { color: #a8a8b0; font-weight: bold; }"
        "QPushButton:checked { color: #ef4444; }");
    connect(recordBtn, &QPushButton::clicked, this, &TimelineToolbar::recordClicked);
    layout->addWidget(recordBtn);

    // CC Record button — captures MIDI controller movements during playback
    ccRecBtn = new QPushButton("CC Rec", this);
    ccRecBtn->setCheckable(true);
    ccRecBtn->setFixedHeight(22);
    ccRecBtn->setToolTip("Record incoming MIDI CC messages into the selected clip");
    ccRecBtn->setStyleSheet(
        "QPushButton { color: #a8a8b0; font-size: 8pt; padding: 2px 6px; }"
        "QPushButton:checked { color: #d97706; border: 1px solid #d97706; }");
    connect(ccRecBtn, &QPushButton::toggled, this, &TimelineToolbar::ccRecordToggled);
    layout->addWidget(ccRecBtn);

    // Count-in toggle
    countInBtn = new QPushButton("1Bar", this);
    countInBtn->setCheckable(true);
    countInBtn->setFixedHeight(22);
    countInBtn->setToolTip("Count-in: play one bar before recording starts");
    countInBtn->setStyleSheet(
        "QPushButton { color: #a8a8b0; font-size: 7pt; padding: 2px 4px; }"
        "QPushButton:checked { color: #d97706; }");
    connect(countInBtn, &QPushButton::toggled, this, &TimelineToolbar::countInToggled);
    layout->addWidget(countInBtn);

    // Timecode display
    timecodeLabel = new QLabel("00:00:000", this);
    timecodeLabel->setStyleSheet(
        "QLabel { color: #e8e8ec; font-family: monospace; font-size: 10pt; "
        "background: #121214; padding: 2px 6px; border: 1px solid #3a3a3e; border-radius: 2px; }");
    layout->addWidget(timecodeLabel);

    layout->addStretch();
}

TimelineToolbar::~TimelineToolbar() = default;

void TimelineToolbar::setPlaying(bool playing)
{
    const QSignalBlocker blocker(playBtn);
    playBtn->setChecked(playing);
    playBtn->setText(playing ? "\xE2\x8F\xB8" : "\xE2\x96\xB6"); // ⏸ or ▶
}

void TimelineToolbar::setTimecode(const QString& text)
{
    timecodeLabel->setText(text);
}

void TimelineToolbar::setBPM(double bpm)
{
    bpmSpinBox->blockSignals(true);
    bpmSpinBox->setValue(bpm);
    bpmSpinBox->blockSignals(false);
}

void TimelineToolbar::setMetronomeEnabled(bool enabled)
{
    metronomeBtn->blockSignals(true);
    metronomeBtn->setChecked(enabled);
    metronomeBtn->blockSignals(false);
}

void TimelineToolbar::setCountInEnabled(bool enabled)
{
    countInBtn->blockSignals(true);
    countInBtn->setChecked(enabled);
    countInBtn->blockSignals(false);
}

void TimelineToolbar::setTimeSig(int numerator, int denominator)
{
    timeSigCombo->blockSignals(true);
    timeSigCombo->setCurrentText(QString("%1/%2").arg(numerator).arg(denominator));
    timeSigCombo->blockSignals(false);
}

void TimelineToolbar::onTimeSigChanged(int index)
{
    Q_UNUSED(index);
    QString text = timeSigCombo->currentText();
    auto parts = text.split('/');
    if (parts.size() == 2)
        emit timeSigChanged(parts[0].toInt(), parts[1].toInt());
}

void TimelineToolbar::onMidiDeviceChanged(int index)
{
    if (index == 0)
        emit midiDeviceChanged({});
    else
        emit midiDeviceChanged(midiDeviceCombo->currentData().toString());
}

void TimelineToolbar::populateMidiDevices(const QStringList& devices)
{
    midiDeviceCombo->blockSignals(true);
    midiDeviceCombo->clear();
    midiDeviceCombo->addItem("None");
    for (const auto& dev : devices)
        midiDeviceCombo->addItem(dev);
    midiDeviceCombo->blockSignals(false);
}

void TimelineToolbar::setDefaultClipLen(double beats)
{
    defaultClipLenSpinBox->blockSignals(true);
    defaultClipLenSpinBox->setValue(beats);
    defaultClipLenSpinBox->blockSignals(false);
}

void TimelineToolbar::setSnap(bool enabled)
{
    snapBtn->blockSignals(true);
    snapBtn->setChecked(enabled);
    snapBtn->blockSignals(false);
}

void TimelineToolbar::setLoopEnabled(bool enabled)
{
    loopBtn->blockSignals(true);
    loopBtn->setChecked(enabled);
    loopBtn->blockSignals(false);
}

void TimelineToolbar::setSnapDivision(int index)
{
    const QSignalBlocker blocker(snapCombo);
    snapCombo->setCurrentIndex(index);
}

void TimelineToolbar::setCcRecordArmed(bool armed)
{
    const QSignalBlocker blocker(ccRecBtn);
    ccRecBtn->setChecked(armed);
}

void TimelineToolbar::addTrackPluginMenu(QMenu* parentMenu, HDAW::PluginManager& pluginManager)
{
    if (parentMenu == nullptr)
        parentMenu = trackMenu;

    const auto& plugins = pluginManager.getPlugins();
    if (plugins.empty()) return;

    auto* pluginMenu = parentMenu->addMenu("Track with Plugin");
    for (const auto& desc : plugins)
    {
        if (pluginManager.isBlacklisted(desc.fileOrIdentifier))
            continue;

        QString label = QString("[%1] %2")
            .arg(QString::fromUtf8(desc.pluginFormatName.toRawUTF8()))
            .arg(QString::fromUtf8(desc.name.toRawUTF8()));
        auto* act = pluginMenu->addAction(label);
        juce::String id = desc.fileOrIdentifier;
        juce::String fmt = desc.pluginFormatName;
        connect(act, &QAction::triggered, this,
            [this, id, fmt]() {
                emit addTrackWithPlugin(id, fmt);
            });
    }
}
