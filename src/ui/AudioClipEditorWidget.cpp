#include "AudioClipEditorWidget.h"
#include "../engine/AudioEngine.h"
#include "Theme.h"
#include "DebugLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileInfo>
#include <QDir>
#include <QFrame>
#include <QShortcut>
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

    playBtn = new QPushButton("\u25B6", header);
    playBtn->setFixedSize(24, 20);
    playBtn->setToolTip("Play the clip from the playhead position");
    playBtn->setStyleSheet("QPushButton { font-size: 10pt; }");
    headerLayout->addWidget(playBtn);

    stopBtn = new QPushButton("\u25A0", header);
    stopBtn->setFixedSize(24, 20);
    stopBtn->setToolTip("Stop playback");
    stopBtn->setStyleSheet("QPushButton { font-size: 10pt; }");
    stopBtn->setEnabled(false);
    headerLayout->addWidget(stopBtn);

    zoomInBtn = new QPushButton("+", header);
    zoomInBtn->setFixedSize(20, 20);
    headerLayout->addWidget(zoomInBtn);

    zoomOutBtn = new QPushButton("-", header);
    zoomOutBtn->setFixedSize(20, 20);
    headerLayout->addWidget(zoomOutBtn);

    mainLayout->addWidget(header);

    // Waveform
    waveform = new AudioWaveformWidget(engine.getProjectPool(), this);
    // Wire the UndoManager so fade-handle drags are undoable (one undo step
    // per drag). See AudioWaveformWidget::setUndoManager.
    waveform->setUndoManager(&engine.getProjectModel().getUndoManager());
    mainLayout->addWidget(waveform, 1);

    // Two-row control bar. A single QHBoxLayout with ~30 fixed-width children
    // clips on narrow windows when the size policy is Ignored, and forces the
    // window wider when it is Preferred/Minimum. A QGridLayout with two rows
    // splits the children evenly (~15 per row) so the total natural width
    // roughly halves, eliminating both the clipping and the forced-widen bugs.
    auto* controlBar = new QWidget(this);
    controlBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* controlLayout = new QGridLayout(controlBar);
    controlLayout->setContentsMargins(8, 2, 8, 2);
    controlLayout->setSpacing(6);

    // --- Row 0: source info, gain, fades, loop, offset, duration ---
    int c0 = 0;

    sourceLabel = new QLabel("No file", controlBar);
    sourceLabel->setStyleSheet("color: #787880; font-size: 7pt;");
    controlLayout->addWidget(sourceLabel, 0, c0++);

    relinkBtn = new QPushButton("...", controlBar);
    relinkBtn->setFixedSize(20, 16);
    relinkBtn->setToolTip("Search for missing source file in project directory");
    relinkBtn->setStyleSheet("QPushButton { color: #a8a8b0; font-size: 7pt; border: 1px solid #444; background: #2a2a30; } QPushButton:hover { background: #3a3a40; }");
    relinkBtn->setVisible(false);
    controlLayout->addWidget(relinkBtn, 0, c0++);

    infoLabel = new QLabel("", controlBar);
    infoLabel->setStyleSheet("color: #787880; font-size: 7pt;");
    controlLayout->addWidget(infoLabel, 0, c0++);

    auto* gainLbl = new QLabel("Gain:", controlBar);
    gainLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(gainLbl, 0, c0++);

    gainSlider = new QSlider(Qt::Horizontal, controlBar);
    gainSlider->setRange(-3600, 1200);
    gainSlider->setValue(0);
    gainSlider->setFixedWidth(80);
    gainSlider->setFixedHeight(16);
    controlLayout->addWidget(gainSlider, 0, c0++);

    gainLabel = new QLabel("0.0 dB", controlBar);
    gainLabel->setFixedWidth(40);
    gainLabel->setStyleSheet("color: #e8e8ec; font-size: 7pt;");
    controlLayout->addWidget(gainLabel, 0, c0++);

    auto* fadeInLbl = new QLabel("Fade In:", controlBar);
    fadeInLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(fadeInLbl, 0, c0++);

    fadeInSpin = new QDoubleSpinBox(controlBar);
    fadeInSpin->setRange(0, 60);
    fadeInSpin->setDecimals(3);
    fadeInSpin->setSingleStep(0.01);
    fadeInSpin->setValue(0);
    fadeInSpin->setFixedWidth(60);
    fadeInSpin->setFixedHeight(20);
    controlLayout->addWidget(fadeInSpin, 0, c0++);

    auto* fadeOutLbl = new QLabel("Fade Out:", controlBar);
    fadeOutLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(fadeOutLbl, 0, c0++);

    fadeOutSpin = new QDoubleSpinBox(controlBar);
    fadeOutSpin->setRange(0, 60);
    fadeOutSpin->setDecimals(3);
    fadeOutSpin->setSingleStep(0.01);
    fadeOutSpin->setValue(0);
    fadeOutSpin->setFixedWidth(60);
    fadeOutSpin->setFixedHeight(20);
    controlLayout->addWidget(fadeOutSpin, 0, c0++);

    loopCheck = new QCheckBox("Loop", controlBar);
    loopCheck->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(loopCheck, 0, c0++);

    auto* offsetLbl = new QLabel("Offset:", controlBar);
    offsetLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(offsetLbl, 0, c0++);

    offsetSpin = new QDoubleSpinBox(controlBar);
    offsetSpin->setRange(0, 99999);
    offsetSpin->setDecimals(3);
    offsetSpin->setSingleStep(0.1);
    offsetSpin->setValue(0);
    offsetSpin->setFixedWidth(60);
    offsetSpin->setFixedHeight(20);
    controlLayout->addWidget(offsetSpin, 0, c0++);

    auto* durLbl = new QLabel("Dur:", controlBar);
    durLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(durLbl, 0, c0++);

    durationSpin = new QDoubleSpinBox(controlBar);
    durationSpin->setRange(0.001, 99999);
    durationSpin->setDecimals(3);
    durationSpin->setSingleStep(0.1);
    durationSpin->setValue(1);
    durationSpin->setFixedWidth(60);
    durationSpin->setFixedHeight(20);
    controlLayout->addWidget(durationSpin, 0, c0++);

    fitToLoopBtn = new QPushButton("Fit to Loop", controlBar);
    fitToLoopBtn->setFixedHeight(20);
    fitToLoopBtn->setToolTip("Stretch the entire source to span the current loop region.");
    controlLayout->addWidget(fitToLoopBtn, 0, c0++);

    playheadLabel = new QLabel("PH: --s", controlBar);
    playheadLabel->setStyleSheet("color: #e8e8ec; font-size: 7pt;");
    playheadLabel->setToolTip("Current playhead position \u2014 paste inserts here");
    controlLayout->addWidget(playheadLabel, 0, c0++);

    // --- Row 1: timestretch, slice, region clipboard ---
    int c1 = 0;

    auto* bpmLbl = new QLabel("Src BPM:", controlBar);
    bpmLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(bpmLbl, 1, c1++);

    sourceBpmSpin = new QDoubleSpinBox(controlBar);
    sourceBpmSpin->setRange(0.0, 400.0);
    sourceBpmSpin->setDecimals(1);
    sourceBpmSpin->setSingleStep(0.5);
    sourceBpmSpin->setValue(0.0);
    sourceBpmSpin->setSuffix("");
    sourceBpmSpin->setFixedWidth(56);
    sourceBpmSpin->setFixedHeight(20);
    sourceBpmSpin->setToolTip("Musical tempo of the source file (0 = unknown).");
    controlLayout->addWidget(sourceBpmSpin, 1, c1++);

    auto* modeLbl = new QLabel("Stretch:", controlBar);
    modeLbl->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(modeLbl, 1, c1++);

    stretchModeCombo = new QComboBox(controlBar);
    stretchModeCombo->addItem("Off", 0);
    stretchModeCombo->addItem("Tempo Match", 1);
    stretchModeCombo->addItem("Manual", 2);
    stretchModeCombo->setFixedHeight(20);
    stretchModeCombo->setToolTip("Off = no stretch. Tempo Match = follow project BPM. "
                                  "Manual = use the ratio below.");
    controlLayout->addWidget(stretchModeCombo, 1, c1++);

    stretchRatioSpin = new QDoubleSpinBox(controlBar);
    stretchRatioSpin->setRange(0.25, 4.0);
    stretchRatioSpin->setDecimals(3);
    stretchRatioSpin->setSingleStep(0.01);
    stretchRatioSpin->setValue(1.0);
    stretchRatioSpin->setFixedWidth(56);
    stretchRatioSpin->setFixedHeight(20);
    stretchRatioSpin->setToolTip("Time stretch ratio vs. original source (>1 longer, <1 shorter).");
    controlLayout->addWidget(stretchRatioSpin, 1, c1++);

    auto* sep2 = new QFrame(controlBar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFixedHeight(20);
    controlLayout->addWidget(sep2, 1, c1++);

    sliceAtPlayheadBtn = new QPushButton("Slice at Playhead", controlBar);
    sliceAtPlayheadBtn->setFixedHeight(20);
    sliceAtPlayheadBtn->setToolTip("Split clip at current playhead position.");
    controlLayout->addWidget(sliceAtPlayheadBtn, 1, c1++);

    sliceAtTransientsBtn = new QPushButton("Slice at Transients", controlBar);
    sliceAtTransientsBtn->setFixedHeight(20);
    sliceAtTransientsBtn->setToolTip("Auto-slice clip at detected transients.");
    controlLayout->addWidget(sliceAtTransientsBtn, 1, c1++);

    sliceAtSelectionBtn = new QPushButton("Slice at Selection", controlBar);
    sliceAtSelectionBtn->setFixedHeight(20);
    sliceAtSelectionBtn->setToolTip("Split clip at selected region boundaries.");
    sliceAtSelectionBtn->setEnabled(false);
    controlLayout->addWidget(sliceAtSelectionBtn, 1, c1++);

    selectionLabel = new QLabel("No selection", controlBar);
    selectionLabel->setStyleSheet("color: #a8a8b0; font-size: 7pt;");
    controlLayout->addWidget(selectionLabel, 1, c1++);

    auto* sep4 = new QFrame(controlBar);
    sep4->setFrameShape(QFrame::VLine);
    sep4->setFixedHeight(20);
    controlLayout->addWidget(sep4, 1, c1++);

    copyRegionBtn = new QPushButton("Copy Region", controlBar);
    copyRegionBtn->setFixedHeight(20);
    copyRegionBtn->setEnabled(false);
    copyRegionBtn->setToolTip("Copy selected region");
    controlLayout->addWidget(copyRegionBtn, 1, c1++);

    cutRegionBtn = new QPushButton("Cut Region", controlBar);
    cutRegionBtn->setFixedHeight(20);
    cutRegionBtn->setEnabled(false);
    cutRegionBtn->setToolTip("Cut selected region");
    controlLayout->addWidget(cutRegionBtn, 1, c1++);

    pasteRegionBtn = new QPushButton("Paste", controlBar);
    pasteRegionBtn->setFixedHeight(20);
    pasteRegionBtn->setEnabled(false);
    pasteRegionBtn->setToolTip("Paste the copied region at the playhead position "
                                "(click on the waveform or timeline to position the playhead)");
    controlLayout->addWidget(pasteRegionBtn, 1, c1++);

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

    // Local playback buttons
    connect(playBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onPlayClicked);
    connect(stopBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onStopClicked);

    // Playback timer — updates the waveform playhead during local playback
    playbackTimer = new QTimer(this);
    playbackTimer->setInterval(16); // ~60 fps
    connect(playbackTimer, &QTimer::timeout, this, &AudioClipEditorWidget::onPlaybackTimer);

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

    // Gain envelope connections. The drag lifecycle (press→move→release) is
    // split into two phases:
    //   • During drag: the editor updates its internal points for visual
    //     preview. The widget stores the pending points but does NOT write to
    //     the model — zero undo entries per mouse-move.
    //   • On release: the widget commits the final envelope to the model in
    //     one undoable transaction (one undo step per drag).
    // Non-drag operations (pointAdded, pointRemoved) still go through the
    // model immediately — they are single-click events, not continuous drags.
    connect(gainEnvelopeEditor, &GainEnvelopeEditor::dragStarted, this, &AudioClipEditorWidget::onEnvelopeDragStarted);
    connect(gainEnvelopeEditor, &GainEnvelopeEditor::dragFinished, this, &AudioClipEditorWidget::onEnvelopeDragFinished);
    connect(gainEnvelopeEditor, &GainEnvelopeEditor::pointAdded, this, [this](double t, double g) {
        if (settingUi || !currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->beginTransaction("add envelope point");
        projectCmds->addGainEnvelopePoint(clipId, t, g);
        reloadEnvelopeEditorFromModel();
    });
    connect(gainEnvelopeEditor, &GainEnvelopeEditor::pointRemoved, this, [this](int /*index*/) {
        if (settingUi || !currentClip.isValid()) return;
        projectCmds->beginTransaction("remove envelope point");
        replaceEnvelopeFromEditor();
    });
    connect(gainEnvelopeEditor, &GainEnvelopeEditor::pointMoved, this, [this](int index, double t, double g) {
        if (settingUi || !currentClip.isValid()) return;
        // During a drag, skip model writes entirely — the editor's internal
        // state is the live preview. Only store the pending points for commit
        // on release. Outside a drag (shouldn't happen, but defensive), fall
        // through to a direct write.
        if (envelopeDragging)
        {
            pendingEnvelopePoints = gainEnvelopeEditor->getPoints();
            juce::ignoreUnused(index, t, g);
            return;
        }
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        projectCmds->moveGainEnvelopePoint(clipId, index, t, g);
    });

    // Region selection tracking
    connect(waveform, &AudioWaveformWidget::regionSelected, this, [this](double startTime, double endTime) {
        if (isLoaded) {
            double dur = endTime - startTime;
            selectionLabel->setText(QString("Sel: %1s-%2s (%3s)")
                .arg(startTime, 0, 'f', 2).arg(endTime, 0, 'f', 2).arg(dur, 0, 'f', 2));
            copyRegionBtn->setEnabled(true);
            cutRegionBtn->setEnabled(true);
        }
    });

    connect(copyRegionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onCopyRegion);
    connect(cutRegionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onCutRegion);
    connect(pasteRegionBtn, &QPushButton::clicked, this, &AudioClipEditorWidget::onPasteRegion);

    // Waveform right-click context menu signals
    connect(waveform, &AudioWaveformWidget::copyRequested, this, &AudioClipEditorWidget::onCopyRegion);
    connect(waveform, &AudioWaveformWidget::cutRequested, this, &AudioClipEditorWidget::onCutRegion);
    connect(waveform, &AudioWaveformWidget::pasteRequested, this, &AudioClipEditorWidget::onPasteRegion);
    connect(waveform, &AudioWaveformWidget::selectAllRequested, this, &AudioClipEditorWidget::onSelectAllRegion);

    // Region keyboard shortcuts. Use WidgetWithChildrenShortcut context so
    // these only fire when the audio editor (or a child) has focus — the
    // default WindowShortcut would also fire while the Mixer/Piano-roll/etc.
    // tab is shown, hijacking Ctrl+C/X/V from text fields elsewhere.
    auto* copyShortcut = new QShortcut(QKeySequence::Copy, this);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated, this, &AudioClipEditorWidget::onCopyRegion);

    auto* cutShortcut = new QShortcut(QKeySequence::Cut, this);
    cutShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(cutShortcut, &QShortcut::activated, this, &AudioClipEditorWidget::onCutRegion);

    auto* pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    pasteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(pasteShortcut, &QShortcut::activated, this, &AudioClipEditorWidget::onPasteRegion);

    auto* selectAllShortcut = new QShortcut(QKeySequence::SelectAll, this);
    selectAllShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(selectAllShortcut, &QShortcut::activated, this, &AudioClipEditorWidget::onSelectAllRegion);

    // Relink missing source file
    connect(relinkBtn, &QPushButton::clicked, this, [this]() {
        if (!currentClip.isValid()) return;
        int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
        // Search the project file's directory — walk up to MainWindow for currentFile
        auto* mw = qobject_cast<QWidget*>(parent());
        while (mw && !mw->objectName().contains("MainWindow"))
            mw = mw->parentWidget();
        QString searchDir = QDir::currentPath();
        if (mw)
        {
            // Reaching into MainWindow for currentFile — the existing pattern
            // for editor widgets that need the project path (AGENTS.md decoupling
            // gap: ReadModel has no getProjectFilePath).
            searchDir = ""; // will be resolved in the command via engine
        }
        auto found = projectCmds->findMissingClipSourceFile(clipId, std::string{});
        if (!found.empty())
        {
            reloadClip();
            HDAW_LOG("AERelink", QString("Relinked clip %1 → %2")
                .arg(clipId).arg(QString::fromStdString(found)));
        }
    });
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
    if (!fi.exists())
    {
        sourceLabel->setText("!! Missing: " + fi.fileName());
        sourceLabel->setStyleSheet("color: #ff6b6b; font-size: 7pt;");
    }
    else
    {
        sourceLabel->setText(fi.fileName());
        sourceLabel->setStyleSheet("color: #787880; font-size: 7pt;");
    }

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
    // Stop any local playback
    if (isPlayingLocally)
        onStopClicked();

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
    if (playheadLabel)
        playheadLabel->setText(QString("PH: %1s").arg(seconds, 0, 'f', 2));
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
    auto envelope = ProjectModel::ensureGainEnvelope(currentClip, &engine.getProjectModel().getUndoManager());
    ProjectModel::clearGainEnvelope(envelope, &engine.getProjectModel().getUndoManager());
    for (const auto& pt : points)
    {
        ProjectModel::addGainEnvelopePoint(envelope, pt.time, pt.gain, &engine.getProjectModel().getUndoManager());
    }
    projectCmds->notifyClipGainEnvelopeChanged(clipId);
}

void AudioClipEditorWidget::onEnvelopeDragStarted()
{
    envelopeDragging = true;
    pendingEnvelopePoints.clear();
}

void AudioClipEditorWidget::onEnvelopeDragFinished()
{
    if (!envelopeDragging || !currentClip.isValid()) return;
    envelopeDragging = false;

    // Commit the final envelope state to the model in one undoable transaction.
    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    auto points = gainEnvelopeEditor->getPoints();

    projectCmds->beginTransaction("move envelope point");
    projectCmds->clearGainEnvelope(clipId);
    for (const auto& pt : points)
        projectCmds->addGainEnvelopePoint(clipId, pt.time, pt.gain);
    projectCmds->endTransaction();
    projectCmds->notifyClipGainEnvelopeChanged(clipId);
    pendingEnvelopePoints.clear();
}

void AudioClipEditorWidget::reloadEnvelopeEditorFromModel()
{
    if (!currentClip.isValid()) return;
    settingUi = true;
    loadGainEnvelope();
    settingUi = false;
}

void AudioClipEditorWidget::replaceEnvelopeFromEditor()
{
    if (!currentClip.isValid()) return;
    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));
    auto points = gainEnvelopeEditor->getPoints();
    projectCmds->clearGainEnvelope(clipId);
    for (const auto& pt : points)
        projectCmds->addGainEnvelopePoint(clipId, pt.time, pt.gain);
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
    copyRegionBtn->setEnabled(false);
    cutRegionBtn->setEnabled(false);
    selectionLabel->setText("No selection");
    reloadClip();
}

void AudioClipEditorWidget::onPasteRegion()
{
    if (!currentClip.isValid() || !HDAW::RegionClipboard::hasContent()) return;

    int clipId = static_cast<int>(currentClip.getProperty(IDs::clipID, 0));

    // Paste target time: use the playhead if it sits inside the source clip's
    // span, otherwise default to just past the source clip's end. Previously
    // this always used the absolute transport position, so a stopped transport
    // (playhead at 0) pasted every region at timeline 0, overlapping whatever
    // was there. The clip's own position is a predictable, in-track fallback.
    double clipStart = currentClip.getProperty(IDs::startTime, 0.0);
    double clipDur = currentClip.getProperty(IDs::duration, 0.0);
    double clipEnd = clipStart + clipDur;

    auto transport = readModel->getTransport();
    double pasteTime = transport.currentTimeSeconds;
    if (pasteTime < clipStart || pasteTime > clipEnd)
        pasteTime = clipEnd; // just after the source clip

    projectCmds->pasteAudioClipRegion(clipId, pasteTime);
    reloadClip();
}

void AudioClipEditorWidget::onSelectAllRegion()
{
    if (!currentClip.isValid() || !waveform) return;
    waveform->selectAll();
    double dur = currentClip.getProperty(IDs::duration, 0.0);
    selectionLabel->setText(QString("Sel: 0.00s-%1s (%1s)").arg(dur, 0, 'f', 2));
    copyRegionBtn->setEnabled(true);
    cutRegionBtn->setEnabled(true);
}

void AudioClipEditorWidget::onPlayClicked()
{
    if (!currentClip.isValid() || isPlayingLocally) return;

    // Position the transport at the clip's start time
    double clipStart = currentClip.getProperty(IDs::startTime, 0.0);
    transportCmds->seekToSeconds(clipStart);
    transportCmds->play();

    isPlayingLocally = true;
    playBtn->setEnabled(false);
    stopBtn->setEnabled(true);

    // Start the playback timer to update the waveform playhead
    playbackTimer->start();
}

void AudioClipEditorWidget::onStopClicked()
{
    if (!isPlayingLocally) return;

    transportCmds->stop();
    playbackTimer->stop();
    isPlayingLocally = false;
    playBtn->setEnabled(true);
    stopBtn->setEnabled(false);

    // Reset the waveform playhead
    waveform->setPlayheadPosition(-1.0);
    playheadLabel->setText("PH: --s");
}

void AudioClipEditorWidget::onPlaybackTimer()
{
    if (!currentClip.isValid() || !isPlayingLocally)
    {
        playbackTimer->stop();
        return;
    }

    // Check if playback has gone past the clip end
    auto transport = readModel->getTransport();
    double clipStart = currentClip.getProperty(IDs::startTime, 0.0);
    double clipDur = currentClip.getProperty(IDs::duration, 0.0);
    double clipEnd = clipStart + clipDur;

    if (transport.currentTimeSeconds >= clipEnd)
    {
        onStopClicked();
        return;
    }

    // Update the waveform playhead relative to the clip's local time
    double localTime = transport.currentTimeSeconds - clipStart;
    waveform->setPlayheadPosition(localTime);
    playheadLabel->setText(QString("PH: %1s").arg(localTime, 0, 'f', 2));
}


