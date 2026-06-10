#include "TimelineToolbar.h"
#include <QHBoxLayout>
#include <QLabel>

TimelineToolbar::TimelineToolbar(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet(
        "TimelineToolbar { background: rgba(28, 28, 31, 220); border-bottom: 1px solid #3a3a3e; }");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    // Add Track button
    addTrackBtn = new QPushButton("+", this);
    addTrackBtn->setFixedSize(22, 22);
    addTrackBtn->setToolTip("Add Track");
    addTrackBtn->setStyleSheet(
        "QPushButton { font-weight: bold; font-size: 11pt; }"
        "QPushButton:hover { border-color: #06b6d4; }");
    connect(addTrackBtn, &QPushButton::clicked, this, &TimelineToolbar::addTrackClicked);
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
        "QDoubleSpinBox { background: #121214; color: #e4e4e7; border: 1px solid #3a3a3e; "
        "border-radius: 2px; padding: 1px 2px; }");
    connect(bpmSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TimelineToolbar::bpmChanged);
    layout->addWidget(bpmSpinBox);

    // Time signature label
    timeSigLabel = new QLabel("4/4", this);
    timeSigLabel->setStyleSheet(
        "QLabel { color: #a1a1aa; font-family: monospace; font-size: 8pt; "
        "padding: 2px 4px; }");
    layout->addWidget(timeSigLabel);

    layout->addSpacing(4);

    // Metronome toggle
    metronomeBtn = new QPushButton("Met", this);
    metronomeBtn->setCheckable(true);
    metronomeBtn->setFixedHeight(22);
    metronomeBtn->setStyleSheet(
        "QPushButton { color: #a1a1aa; }"
        "QPushButton:checked { color: #10b981; }");
    connect(metronomeBtn, &QPushButton::toggled, this, &TimelineToolbar::metronomeToggled);
    layout->addWidget(metronomeBtn);

    layout->addSpacing(4);

    // Default clip length
    auto* clipLenLabel = new QLabel("Clip:", this);
    clipLenLabel->setStyleSheet("QLabel { color: #a1a1aa; font-size: 7pt; }");
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
        "QDoubleSpinBox { background: #121214; color: #e4e4e7; border: 1px solid #3a3a3e; "
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
        "QPushButton { color: #a1a1aa; font-weight: bold; }"
        "QPushButton:checked { color: #ef4444; }");
    connect(recordBtn, &QPushButton::clicked, this, &TimelineToolbar::recordClicked);
    layout->addWidget(recordBtn);

    // Timecode display
    timecodeLabel = new QLabel("00:00:000", this);
    timecodeLabel->setStyleSheet(
        "QLabel { color: #e4e4e7; font-family: monospace; font-size: 10pt; "
        "background: #121214; padding: 2px 6px; border: 1px solid #3a3a3e; border-radius: 2px; }");
    layout->addWidget(timecodeLabel);

    layout->addStretch();
}

TimelineToolbar::~TimelineToolbar() = default;

void TimelineToolbar::setPlaying(bool playing)
{
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

void TimelineToolbar::setDefaultClipLen(double beats)
{
    defaultClipLenSpinBox->blockSignals(true);
    defaultClipLenSpinBox->setValue(beats);
    defaultClipLenSpinBox->blockSignals(false);
}
