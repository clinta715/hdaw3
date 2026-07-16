#include "AudioClipEditorWidget.h"
#include "../engine/AudioEngine.h"
#include "Theme.h"
#include "DebugLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QFrame>
#include <QKeyEvent>
#include "../engine/RegionClipboard.h"

AudioClipEditorWidget::AudioClipEditorWidget(AudioEngine& ae, QWidget* parent)
    : QWidget(parent), engine(ae)
{
    projectCmds = &engine.getProjectCommands();
    transportCmds = &engine.getTransportCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    HDAW_LOG("AECtor", "Enter AudioClipEditorWidget ctor");
    setupUI();
    HDAW_LOG("AECtor", "After setupUI");
    connectSignals();
    HDAW_LOG("AECtor", "After connectSignals");
    clear();
    HDAW_LOG("AECtor", "AudioClipEditorWidget ctor complete");
}

AudioClipEditorWidget::~AudioClipEditorWidget() = default;

void AudioClipEditorWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header
    auto* header = new QWidget(this);
    header->setFixedHeight(28);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(8, 2, 8, 2);

    closeBtn = new QPushButton("X", header);
    closeBtn->setFixedSize(20, 20);
    headerLayout->addWidget(closeBtn);

    titleLabel = new QLabel("Audio Editor", header);
    titleLabel->setObjectName("audioEditorTitle");
    headerLayout->addWidget(titleLabel);

    headerLayout->addStretch();

    zoomInBtn = new QPushButton("+", header);
    zoomInBtn->setFixedSize(20, 20);
    headerLayout->addWidget(zoomInBtn);

    zoomOutBtn = new QPushButton("-", header);
    zoomOutBtn->setFixedSize(20, 20);
    headerLayout->addWidget(zoomOutBtn);

    mainLayout->addWidget(header);

    // Waveform
    waveform = new AudioWaveformWidget(engine.getProjectPool(), this);
    mainLayout->addWidget(waveform, 1);

    // Control bar
    auto* controlBar = new QWidget(this);
    controlBar->setFixedHeight(36);
    auto* controlLayout = new QHBoxLayout(controlBar);
    controlLayout->setContentsMargins(8, 2, 8, 2);
    controlLayout->setSpacing(6);

    sourceLabel = new QLabel("No file", controlBar);
    sourceLabel->setStyleSheet("color: #787880; font-size: 7pt;");
    controlLayout->addWidget(sourceLabel);

    infoLabel = new QLabel("", controlBar);
    infoLabel->setStyleSheet("color: #787880; font-size: 7pt;");
    controlLayout->addWidget(infoLabel);

    controlLayout->addStretch();

    auto* gainLbl = new QLabel("Gain:", controlBar);
    gainLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(gainLbl);

    gainSlider = new QSlider(Qt::Horizontal, controlBar);
    gainSlider->setRange(-3600, 1200);
    gainSlider->setValue(0);
    gainSlider->setFixedWidth(80);
    gainSlider->setFixedHeight(16);
    controlLayout->addWidget(gainSlider);

    gainLabel = new QLabel("0.0 dB", controlBar);
    gainLabel->setFixedWidth(40);
    gainLabel->setStyleSheet("color: #e8e8ec; font-size: 7pt;");
    controlLayout->addWidget(gainLabel);

    auto* fadeInLbl = new QLabel("Fade In:", controlBar);
    fadeInLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(fadeInLbl);

    fadeInSpin = new QDoubleSpinBox(controlBar);
    fadeInSpin->setRange(0, 60);
    fadeInSpin->setDecimals(3);
    fadeInSpin->setSingleStep(0.01);
    fadeInSpin->setValue(0);
    fadeInSpin->setFixedWidth(60);
    fadeInSpin->setFixedHeight(20);
    controlLayout->addWidget(fadeInSpin);

    auto* fadeOutLbl = new QLabel("Fade Out:", controlBar);
    fadeOutLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(fadeOutLbl);

    fadeOutSpin = new QDoubleSpinBox(controlBar);
    fadeOutSpin->setRange(0, 60);
    fadeOutSpin->setDecimals(3);
    fadeOutSpin->setSingleStep(0.01);
    fadeOutSpin->setValue(0);
    fadeOutSpin->setFixedWidth(60);
    fadeOutSpin->setFixedHeight(20);
    controlLayout->addWidget(fadeOutSpin);

    loopCheck = new QCheckBox("Loop", controlBar);
    loopCheck->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(loopCheck);

    auto* offsetLbl = new QLabel("Offset:", controlBar);
    offsetLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(offsetLbl);

    offsetSpin = new QDoubleSpinBox(controlBar);
    offsetSpin->setRange(0, 99999);
    offsetSpin->setDecimals(3);
    offsetSpin->setSingleStep(0.1);
    offsetSpin->setValue(0);
    offsetSpin->setFixedWidth(60);
    offsetSpin->setFixedHeight(20);
    controlLayout->addWidget(offsetSpin);

    auto* durLbl = new QLabel("Dur:", controlBar);
    durLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(durLbl);

    durationSpin = new QDoubleSpinBox(controlBar);
    durationSpin->setRange(0.001, 99999);
    durationSpin->setDecimals(3);
    durationSpin->setSingleStep(0.1);
    durationSpin->setValue(1);
    durationSpin->setFixedWidth(60);
    durationSpin->setFixedHeight(20);
    controlLayout->addWidget(durationSpin);

    // --- Timestretch controls ---
    auto* sep2 = new QFrame(controlBar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFixedHeight(20);
    controlLayout->addWidget(sep2);

    auto* bpmLbl = new QLabel("Src BPM:", controlBar);
    bpmLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(bpmLbl);

    sourceBpmSpin = new QDoubleSpinBox(controlBar);
    sourceBpmSpin->setRange(0.0, 400.0);
    sourceBpmSpin->setDecimals(1);
    sourceBpmSpin->setSingleStep(0.5);
    sourceBpmSpin->setValue(0.0);
    sourceBpmSpin->setSuffix("");
    sourceBpmSpin->setFixedWidth(56);
    sourceBpmSpin->setFixedHeight(20);
    sourceBpmSpin->setToolTip("Musical tempo of the source file (0 = unknown).");
    controlLayout->addWidget(sourceBpmSpin);

    auto* modeLbl = new QLabel("Stretch:", controlBar);
    modeLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(modeLbl);

    stretchModeCombo = new QComboBox(controlBar);
    stretchModeCombo->addItem("Off", 0);
    stretchModeCombo->addItem("Tempo Match", 1);
    stretchModeCombo->addItem("Manual", 2);
    stretchModeCombo->setFixedHeight(20);
    stretchModeCombo->setToolTip("Off = no stretch. Tempo Match = follow project BPM. "
                                  "Manual = use the ratio below.");
    controlLayout->addWidget(stretchModeCombo);

    stretchRatioSpin = new QDoubleSpinBox(controlBar);
    stretchRatioSpin->setRange(0.25, 4.0);
    stretchRatioSpin->setDecimals(3);
    stretchRatioSpin->setSingleStep(0.01);
    stretchRatioSpin->setValue(1.0);
    stretchRatioSpin->setFixedWidth(56);
    stretchRatioSpin->setFixedHeight(20);
    stretchRatioSpin->setToolTip("Time stretch ratio vs. original source (>1 longer, <1 shorter).");
    controlLayout->addWidget(stretchRatioSpin);

    fitToLoopBtn = new QPushButton("Fit to Loop", controlBar);
    fitToLoopBtn->setFixedHeight(20);
    fitToLoopBtn->setToolTip("Stretch the entire source to span the current loop region.");
    controlLayout->addWidget(fitToLoopBtn);

    // Slicing controls
    auto* sep3 = new QFrame(controlBar);
    sep3->setFrameShape(QFrame::VLine);
    sep3->setFixedHeight(20);
    controlLayout->addWidget(sep3);

    sliceAtPlayheadBtn = new QPushButton("Slice at Playhead", controlBar);
    sliceAtPlayheadBtn->setFixedHeight(20);
    sliceAtPlayheadBtn->setToolTip("Split clip at current playhead position.");
    controlLayout->addWidget(sliceAtPlayheadBtn);

    sliceAtTransientsBtn = new QPushButton("Slice at Transients", controlBar);
    sliceAtTransientsBtn->setFixedHeight(20);
    sliceAtTransientsBtn->setToolTip("Auto-slice clip at detected transients.");
    controlLayout->addWidget(sliceAtTransientsBtn);

    sliceAtSelectionBtn = new QPushButton("Slice at Selection", controlBar);
    sliceAtSelectionBtn->setFixedHeight(20);
    sliceAtSelectionBtn->setToolTip("Split clip at selected region boundaries.");
    sliceAtSelectionBtn->setEnabled(false);
    controlLayout->addWidget(sliceAtSelectionBtn);

    // Region selection label
    selectionLabel = new QLabel("No selection", controlBar);
    selectionLabel->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(selectionLabel);

    // Region copy/paste buttons
    auto* sep4 = new QFrame(controlBar);
    sep4->setFrameShape(QFrame::VLine);
    sep4->setFixedHeight(20);
    controlLayout->addWidget(sep4);

    copyRegionBtn = new QPushButton("Copy Region", controlBar);
    copyRegionBtn->setFixedHeight(20);
    copyRegionBtn->setEnabled(false);
    copyRegionBtn->setToolTip("Copy selected region");
    controlLayout->addWidget(copyRegionBtn);

    cutRegionBtn = new QPushButton("Cut Region", controlBar);
    cutRegionBtn->setFixedHeight(20);
    cutRegionBtn->setEnabled(false);
    cutRegionBtn->setToolTip("Cut selected region");
    controlLayout->addWidget(cutRegionBtn);

    pasteRegionBtn = new QPushButton("Paste", controlBar);
    pasteRegionBtn->setFixedHeight(20);
    pasteRegionBtn->setEnabled(false);
    pasteRegionBtn->setToolTip("Paste region at playhead");
    controlLayout->addWidget(pasteRegionBtn);

    // Gain envelope editor (placed in a new section below control bar)
    gainEnvelopeEditor = new GainEnvelopeEditor(this);
    gainEnvelopeEditor->setFixedHeight(80);
    mainLayout->addWidget(gainEnvelopeEditor);

    mainLayout->addWidget(controlBar);
}

void AudioClipEditorWidget::connectSignals()
{
    connect(closeBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::clipClosed);

    connect(zoomInBtn, &QPushButton::clicked, waveform, &AudioWaveformWidget::zoomIn);
    connect(zoomOutBtn, &QPushButton::clicked, waveform, &AudioWaveformWidget::zoomOut);

    connect(gainSlider, &QSlider::valueChanged, this, [this](int val) {
        if (settingUi || !currentClip.isValid()) return;
        double dB = val / 100.0;
        double linear = std::pow(10.0, dB / 20.0);
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->setClipGain(clipId, static_cast<float>(linear));
        gainLabel->setText(QString("%1 dB").arg(dB, 0, 'f', 1));
    });

    connect(waveform, &AudioWaveformWidget::fadeInChanged, this, [this](double sec) {
        // Block signals while programmatically updating the spinbox: otherwise
        // setValue fires valueChanged, which re-writes the same fadeIn property
        // to the ValueTree and triggers a redundant waveform repaint on every
        // drag move (a feedback loop). The drag itself already committed the
        // property; this sync is purely visual.
        if (!settingUi)
        {
            QSignalBlocker blocker(fadeInSpin);
            fadeInSpin->setValue(sec);
        }
    });
    connect(waveform, &AudioWaveformWidget::fadeOutChanged, this, [this](double sec) {
        // See fadeInChanged above for why we block signals here.
        if (!settingUi)
        {
            QSignalBlocker blocker(fadeOutSpin);
            fadeOutSpin->setValue(sec);
        }
    });

    connect(fadeInSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->setClipFadeIn(clipId, val);
        waveform->update();
    });

    connect(fadeOutSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->setClipFadeOut(clipId, val);
        waveform->update();
    });

    connect(loopCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->setClipLooping(clipId, checked);
    });

    connect(offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->setClipOffset(clipId, std::max(0.0, val));
        waveform->update();
    });

    connect(durationSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->setClipDuration(clipId, std::max(0.001, val));
        waveform->update();
    });

    // --- Timestretch connections ---
    connect(sourceBpmSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->setClipSourceBpm(clipId, val);
        // If currently tempo-matching, re-derive the ratio.
        int mode = stretchModeCombo->currentData().toInt();
        if (mode == 1 && val > 0.0)
            projectCmds->tempoMatchClip(clipId);
    });

    connect(stretchModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        int mode = stretchModeCombo->currentData().toInt();
        projectCmds->setClipStretchMode(clipId, mode);
        // Ratio spin enabled only in Manual mode.
        stretchRatioSpin->setEnabled(mode == 2);
    });

    connect(stretchRatioSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        // Only apply when in Manual mode — TempoMatch derives the ratio itself.
        if (stretchModeCombo->currentData().toInt() == 2)
            projectCmds->setClipStretchRatio(clipId, val);
    });

    connect(fitToLoopBtn, &QPushButton::clicked, this, [this] {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->fitClipToLoop(clipId);
    });

    // Slicing connections
    connect(sliceAtPlayheadBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onSliceAtPlayhead);
    connect(sliceAtTransientsBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onSliceAtTransients);
    connect(sliceAtSelectionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onSliceAtSelection);

    // Gain envelope connections
    connect(gainEnvelopeEditor, &GainEnvelopeEditor::pointsChanged, this, &AudioClipEditorWidget::onGainEnvelopeChanged);

    // Region selection tracking
    connect(waveform, &AudioWaveformWidget::regionSelected, this, [this](double startBeat, double endBeat) {
        if (isLoaded) {
            double dur = endBeat - startBeat;
            selectionLabel->setText(QString("Sel: %1s-%2s (%3s)")
                .arg(startBeat, 0, 'f', 2).arg(endBeat, 0, 'f', 2).arg(dur, 0, 'f', 2));
            copyRegionBtn->setEnabled(true);
            cutRegionBtn->setEnabled(true);
        }
    });

    connect(copyRegionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onCopyRegion);
    connect(cutRegionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onCutRegion);
    connect(pasteRegionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onPasteRegion);
}

void AudioClipEditorWidget::loadClip(juce::ValueTree clipTree)
{
    currentClip = clipTree;
    isLoaded = currentClip.isValid();

    if (isLoaded)
        pasteRegionBtn->setEnabled(HDAW::RegionClipboard::hasContent());

    if (!isLoaded)
    {
        clear();
        return;
    }

    waveform->setClip(currentClip);

    // Title
    QString name = QString::fromUtf8(currentClip.getProperty(IDs::name).toString().toRawUTF8());
    titleLabel->setText("Audio Editor - " + name);

    // Source info
    QString src = QString::fromUtf8(currentClip.getProperty(IDs::sourceFile).toString().toRawUTF8());
    QFileInfo fi(src);
    sourceLabel->setText(fi.fileName());

    // Load metadata from reader
    juce::File jf(src.toUtf8().constData());
    if (jf.existsAsFile())
    {
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            engine.getProjectPool().getFormatManager().createReaderFor(jf));
        if (reader != nullptr)
        {
            double len = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            infoLabel->setText(QString("%1 Hz / %2 bit / %3s")
                .arg(reader->sampleRate)
                .arg(reader->bitsPerSample)
                .arg(len, 0, 'f', 2));
        }
    }

    updateControls();
    loadGainEnvelope();
    show();
}

void AudioClipEditorWidget::clear()
{
    currentClip = juce::ValueTree();
    isLoaded = false;
    waveform->setClip(juce::ValueTree());
    titleLabel->setText("Audio Editor - No clip selected");
    sourceLabel->setText("No file");
    infoLabel->setText("");
    gainEnvelopeEditor->setPoints({});
    copyRegionBtn->setEnabled(false);
    cutRegionBtn->setEnabled(false);
    pasteRegionBtn->setEnabled(false);
    selectionLabel->setText("No selection");
    updateControls();
}

void AudioClipEditorWidget::updatePlayhead(double seconds)
{
    waveform->setPlayheadPosition(seconds);
}

void AudioClipEditorWidget::updateControls()
{
    settingUi = true;

    if (!isLoaded)
    {
        gainSlider->setValue(0);
        gainLabel->setText("0.0 dB");
        fadeInSpin->setValue(0);
        fadeOutSpin->setValue(0);
        loopCheck->setChecked(false);
        offsetSpin->setValue(0);
        durationSpin->setValue(1);
        sourceBpmSpin->setValue(0.0);
        stretchModeCombo->setCurrentIndex(0);
        stretchRatioSpin->setValue(1.0);
        stretchRatioSpin->setEnabled(false);
        settingUi = false;
        return;
    }

    double gain = currentClip.getProperty(IDs::gain);
    double gainDb = 20.0 * std::log10((std::max)(gain, 0.001));
    gainSlider->setValue(static_cast<int>(gainDb * 100.0));
    gainLabel->setText(QString("%1 dB").arg(gainDb, 0, 'f', 1));

    fadeInSpin->setValue(static_cast<double>(currentClip.getProperty(IDs::fadeIn)));
    fadeOutSpin->setValue(static_cast<double>(currentClip.getProperty(IDs::fadeOut)));
    loopCheck->setChecked(currentClip.getProperty(IDs::looping));
    offsetSpin->setValue(static_cast<double>(currentClip.getProperty(IDs::offset)));
    durationSpin->setValue(static_cast<double>(currentClip.getProperty(IDs::duration)));

    double srcBpm = currentClip.getProperty(IDs::sourceBpm, 0.0);
    int mode = static_cast<int>(currentClip.getProperty(IDs::stretchMode, 0));
    double ratio = currentClip.getProperty(IDs::stretchRatio, 1.0);
    sourceBpmSpin->setValue(srcBpm);
    int comboIdx = stretchModeCombo->findData(mode);
    stretchModeCombo->setCurrentIndex(comboIdx >= 0 ? comboIdx : 0);
    stretchRatioSpin->setValue(ratio);
    stretchRatioSpin->setEnabled(mode == 2);

    settingUi = false;
}

void AudioClipEditorWidget::loadGainEnvelope()
{
    if (!isLoaded) return;
    auto envelope = currentClip.getChildWithName(IDs::GAIN_ENVELOPE);
    QVector<GainEnvelopeEditor::Point> points;
    for (int i = 0; i < envelope.getNumChildren(); ++i)
    {
        auto pt = envelope.getChild(i);
        points.append({ pt.getProperty(IDs::pointTime), pt.getProperty(IDs::pointGain) });
    }
    gainEnvelopeEditor->setPoints(points);
    gainEnvelopeEditor->setDuration(currentClip.getProperty(IDs::duration));
}

void AudioClipEditorWidget::reloadClip()
{
    if (currentClip.isValid())
        loadClip(currentClip);
}

void AudioClipEditorWidget::onGainEnvelopeChanged(const QVector<GainEnvelopeEditor::Point>& points)
{
    if (settingUi || !currentClip.isValid()) return;
    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    auto envelope = ProjectModel::ensureGainEnvelope(currentClip);
    ProjectModel::clearGainEnvelope(envelope, &engine.getProjectModel().getUndoManager());
    for (const auto& pt : points)
    {
        ProjectModel::addGainEnvelopePoint(envelope, pt.time, pt.gain, &engine.getProjectModel().getUndoManager());
    }
    projectCmds->notifyClipGainEnvelopeChanged(clipId);
}

void AudioClipEditorWidget::onSliceAtPlayhead()
{
    if (!currentClip.isValid()) return;
    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    projectCmds->sliceClipAtPlayhead(clipId);
    // Reload clip list or refresh
    reloadClip();
}

void AudioClipEditorWidget::onSliceAtTransients()
{
    if (!currentClip.isValid()) return;
    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    projectCmds->sliceClipAtTransients(clipId);
    reloadClip();
}

void AudioClipEditorWidget::onSliceAtSelection()
{
    if (!currentClip.isValid()) return;
    // Get selection from waveform
    double selStart = waveform->getSelectionStart();
    double selEnd = waveform->getSelectionEnd();
    if (selEnd <= selStart) return;
    
    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    projectCmds->sliceClipAtTimes(clipId, {selStart, selEnd});
    reloadClip();
}

void AudioClipEditorWidget::onCopyRegion()
{
    if (!currentClip.isValid() || !waveform->hasSelection()) return;

    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    double selStart = waveform->getSelectionStart();
    double selEnd = waveform->getSelectionEnd();
    if (selEnd <= selStart) return;

    projectCmds->copyAudioClipRegion(clipId, selStart, selEnd);
    pasteRegionBtn->setEnabled(true);
}

void AudioClipEditorWidget::onCutRegion()
{
    if (!currentClip.isValid() || !waveform->hasSelection()) return;

    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    double selStart = waveform->getSelectionStart();
    double selEnd = waveform->getSelectionEnd();
    if (selEnd <= selStart) return;

    projectCmds->cutAudioClipRegion(clipId, selStart, selEnd);
    pasteRegionBtn->setEnabled(true);
    waveform->clearSelection();
    reloadClip();
}

void AudioClipEditorWidget::onPasteRegion()
{
    if (!currentClip.isValid() || !HDAW::RegionClipboard::hasContent()) return;

    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    auto transport = readModel->getTransport();
    double pasteTime = transport.currentTimeSeconds;

    projectCmds->pasteAudioClipRegion(clipId, pasteTime);
    reloadClip();
}

void AudioClipEditorWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers() == Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_C:
            onCopyRegion();
            return;
        case Qt::Key_X:
            onCutRegion();
            return;
        case Qt::Key_V:
            onPasteRegion();
            return;
        }
    }
    QWidget::keyPressEvent(event);
}
