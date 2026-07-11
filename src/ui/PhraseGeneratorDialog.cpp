#include "PhraseGeneratorDialog.h"
#include "../engine/PhraseGenerator.h"
#include "../model/ProjectModel.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>

PhraseGeneratorDialog::PhraseGeneratorDialog(AudioEngine& ae, int targetTrackIndex, QWidget* parent)
    : QDialog(parent), engine(ae), trackIndex(targetTrackIndex)
{
    projectCmds = &engine.getProjectCommands();
    audioGraphCmds = &engine.getAudioGraphCommands();
    readModel = &engine.getReadModel();
    setWindowTitle("Generator");
    setFixedSize(440, 580);
    setModal(true);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    auto* title = new QLabel("Generator");
    QFont tf = title->font();
    tf.setPointSize(14);
    tf.setBold(true);
    title->setFont(tf);
    title->setStyleSheet(QString("color: %1;").arg(ThemeColors::accent().name()));
    mainLayout->addWidget(title);

    // ── Mode selector ──
    auto* modeRow = new QHBoxLayout();
    modeRow->addWidget(new QLabel("Mode:"));
    modeTypeCombo = new QComboBox(this);
    modeTypeCombo->addItem("Phrase");
    modeTypeCombo->addItem("Single Chord");
    modeTypeCombo->addItem("Chord Progression");
    modeRow->addWidget(modeTypeCombo, 1);
    mainLayout->addLayout(modeRow);

    // ── Scale group (shared) ──
    auto* scaleGroup = new QGroupBox("Scale / Mode");
    auto* scaleForm = new QFormLayout(scaleGroup);

    rootCombo = new QComboBox(this);
    const char* roots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < 12; ++i)
        rootCombo->addItem(roots[i], i);
    int projRoot = engine.getProjectModel().getScaleRoot();
    rootCombo->setCurrentIndex(std::clamp(projRoot, 0, 11));
    scaleForm->addRow("Root:", rootCombo);

    modeCombo = new QComboBox(this);
    const auto& modes = PhraseGenerator::getScaleModes();
    int projMode = engine.getProjectModel().getScaleMode();
    int modeIdx = 0;
    for (const auto& m : modes)
    {
        modeCombo->addItem(m.name, m.index);
        if (m.index == projMode)
            modeIdx = modeCombo->count() - 1;
    }
    modeCombo->setCurrentIndex(modeIdx);
    scaleForm->addRow("Mode:", modeCombo);

    mainLayout->addWidget(scaleGroup);

    // ── Param stack ──
    paramStack = new QStackedWidget(this);

    createPhraseControls(paramStack);
    createChordControls(paramStack);
    createProgressionControls(paramStack);

    paramStack->setCurrentIndex(0);
    mainLayout->addWidget(paramStack);

    // ── Note range (shared) ──
    auto* rangeGroup = new QGroupBox("Note Range");
    auto* rangeForm = new QFormLayout(rangeGroup);

    lowNoteSpin = new QSpinBox(this);
    lowNoteSpin->setRange(24, 108);
    lowNoteSpin->setValue(48);
    auto updateLowPrefix = [this](int v) {
        lowNoteSpin->setPrefix(QString::fromUtf8(PhraseGenerator::noteName(v)));
    };
    // 4-arg connect form: pass `this` as the receiver context so Qt
    // auto-disconnects if the dialog is destroyed (per AGENTS.md rule).
    connect(lowNoteSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, updateLowPrefix);
    updateLowPrefix(48);
    rangeForm->addRow("Low:", lowNoteSpin);

    highNoteSpin = new QSpinBox(this);
    highNoteSpin->setRange(24, 108);
    highNoteSpin->setValue(84);
    auto updateHighPrefix = [this](int v) {
        highNoteSpin->setPrefix(QString::fromUtf8(PhraseGenerator::noteName(v)));
    };
    // 4-arg connect form (see lowNoteSpin note above).
    connect(highNoteSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, updateHighPrefix);
    updateHighPrefix(84);
    rangeForm->addRow("High:", highNoteSpin);

    mainLayout->addWidget(rangeGroup);

    // ── Velocity (shared) ──
    auto* velGroup = new QGroupBox("Velocity");
    auto* velLayout = new QVBoxLayout(velGroup);

    velocitySlider = new QSlider(Qt::Horizontal, this);
    velocitySlider->setRange(30, 127);
    velocitySlider->setValue(90);
    auto* velLabel = new QLabel("Intensity: 90");
    connect(velocitySlider, &QSlider::valueChanged, this, [velLabel](int v) {
        velLabel->setText(QString("Intensity: %1").arg(v));
    });
    velLayout->addWidget(velocitySlider);
    velLayout->addWidget(velLabel);

    mainLayout->addWidget(velGroup);

    // ── Preview ──
    previewLabel = new QLabel("");
    previewLabel->setStyleSheet(QString("color: %1; font-size: 10px;").arg(ThemeColors::textMuted().name()));
    previewLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(previewLabel);

    // ── Buttons ──
    auto* btnLayout = new QHBoxLayout();
    auto* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setFixedHeight(30);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto* generateBtn = new QPushButton("Generate");
    generateBtn->setFixedHeight(30);
    generateBtn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: white; border: none; "
        "border-radius: 4px; padding: 0 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: %2; }")
        .arg(ThemeColors::accent().name(), ThemeColors::accentBright().name()));
    connect(generateBtn, &QPushButton::clicked, this, &PhraseGeneratorDialog::onGenerate);

    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(generateBtn);
    mainLayout->addLayout(btnLayout);

    // ── Connections ──
    connect(modeTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PhraseGeneratorDialog::onModeChanged);
    connect(styleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PhraseGeneratorDialog::onStyleChanged);
}

PhraseGeneratorDialog::~PhraseGeneratorDialog() = default;

// ── Mode switching ──

void PhraseGeneratorDialog::onModeChanged(int index)
{
    paramStack->setCurrentIndex(index);
}

void PhraseGeneratorDialog::onStyleChanged(int)
{
    // Pre-fill params based on style preset
    auto style = static_cast<PhraseGenerator::Style>(styleCombo->currentData().toInt());
    switch (style)
    {
    case PhraseGenerator::Arpeggio:
        lengthSpin->setValue(4);
        densitySpin->setValue(16);
        break;
    case PhraseGenerator::BassLine:
        lengthSpin->setValue(8);
        densitySpin->setValue(16);
        lowNoteSpin->setValue(24);
        highNoteSpin->setValue(60);
        break;
    case PhraseGenerator::ChordStab:
        lengthSpin->setValue(2);
        densitySpin->setValue(3);
        break;
    case PhraseGenerator::Pad:
        lengthSpin->setValue(8);
        densitySpin->setValue(6);
        break;
    case PhraseGenerator::Lead:
        lengthSpin->setValue(4);
        densitySpin->setValue(16);
        lowNoteSpin->setValue(60);
        highNoteSpin->setValue(96);
        break;
    case PhraseGenerator::RandomWalk:
        lengthSpin->setValue(4);
        densitySpin->setValue(12);
        break;
    case PhraseGenerator::Buildup:
        lengthSpin->setValue(8);
        densitySpin->setValue(32);
        break;
    default:
        lengthSpin->setValue(4);
        densitySpin->setValue(8);
        break;
    }
}

// ── Phrase controls page ──

void PhraseGeneratorDialog::createPhraseControls(QWidget* parent)
{
    phrasePage = new QWidget(parent);

    auto* layout = new QVBoxLayout(phrasePage);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* styleGroup = new QGroupBox("Style");
    auto* styleLayout = new QVBoxLayout(styleGroup);

    styleCombo = new QComboBox(phrasePage);
    for (int s = PhraseGenerator::Standard; s <= PhraseGenerator::Buildup; ++s)
    {
        auto style = static_cast<PhraseGenerator::Style>(s);
        styleCombo->addItem(PhraseGenerator::styleName(style), s);
    }
    styleCombo->setCurrentIndex(0);
    styleLayout->addWidget(styleCombo);
    layout->addWidget(styleGroup);

    auto* rhythmGroup = new QGroupBox("Rhythm");
    auto* rhythmForm = new QFormLayout(rhythmGroup);

    lengthSpin = new QSpinBox(phrasePage);
    lengthSpin->setRange(1, 64);
    lengthSpin->setValue(4);
    lengthSpin->setSuffix(" beats");
    rhythmForm->addRow("Length:", lengthSpin);

    densitySpin = new QSpinBox(phrasePage);
    densitySpin->setRange(1, 128);
    densitySpin->setValue(8);
    densitySpin->setSuffix(" notes");
    rhythmForm->addRow("Density:", densitySpin);

    layout->addWidget(rhythmGroup);

    paramStack->addWidget(phrasePage);
}

// ── Chord controls page ──

void PhraseGeneratorDialog::createChordControls(QWidget* parent)
{
    chordPage = new QWidget(parent);

    auto* layout = new QVBoxLayout(chordPage);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* typeGroup = new QGroupBox("Chord Type");
    auto* typeForm = new QFormLayout(typeGroup);

    chordTypeCombo = new QComboBox(chordPage);
    const auto& types = PhraseGenerator::getChordTypes();
    for (const auto& t : types)
        chordTypeCombo->addItem(t.name, t.index);
    chordTypeCombo->setCurrentIndex(0);
    typeForm->addRow("Chord:", chordTypeCombo);

    voicingCombo = new QComboBox(chordPage);
    voicingCombo->addItem("Close", 0);
    voicingCombo->addItem("Open", 1);
    voicingCombo->addItem("Spread", 2);
    typeForm->addRow("Voicing:", voicingCombo);

    inversionCombo = new QComboBox(chordPage);
    inversionCombo->addItem("Root", 0);
    inversionCombo->addItem("1st Inv", 1);
    inversionCombo->addItem("2nd Inv", 2);
    inversionCombo->addItem("3rd Inv", 3);
    typeForm->addRow("Inversion:", inversionCombo);

    layout->addWidget(typeGroup);

    auto* arpGroup = new QGroupBox("Playback");
    auto* arpForm = new QFormLayout(arpGroup);

    arpeggiateChk = new QCheckBox("Arpeggiate", chordPage);
    arpForm->addRow("", arpeggiateChk);

    arpRateSpin = new QDoubleSpinBox(chordPage);
    arpRateSpin->setRange(0.03125, 2.0);
    arpRateSpin->setValue(0.125);
    arpRateSpin->setSingleStep(0.03125);
    arpRateSpin->setSuffix(" beat");
    arpForm->addRow("Arp Rate:", arpRateSpin);

    chordDurationSpin = new QDoubleSpinBox(chordPage);
    chordDurationSpin->setRange(0.25, 16.0);
    chordDurationSpin->setValue(2.0);
    chordDurationSpin->setSingleStep(0.25);
    chordDurationSpin->setSuffix(" beats");
    arpForm->addRow("Note Length:", chordDurationSpin);

    layout->addWidget(arpGroup);

    paramStack->addWidget(chordPage);
}

// ── Progression controls page ──

void PhraseGeneratorDialog::createProgressionControls(QWidget* parent)
{
    progPage = new QWidget(parent);

    auto* layout = new QVBoxLayout(progPage);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* patGroup = new QGroupBox("Progression");
    auto* patForm = new QFormLayout(patGroup);

    patternCombo = new QComboBox(progPage);
    const auto& patterns = PhraseGenerator::getProgressionPatterns();
    for (const auto& p : patterns)
        patternCombo->addItem(p.name, p.index);
    patternCombo->setCurrentIndex(1); // Pop progression
    patForm->addRow("Pattern:", patternCombo);

    chordOverrideCombo = new QComboBox(progPage);
    chordOverrideCombo->addItem("Default per degree", -1);
    const auto& types = PhraseGenerator::getChordTypes();
    for (const auto& t : types)
        chordOverrideCombo->addItem(t.name, t.index);
    chordOverrideCombo->setCurrentIndex(0);
    patForm->addRow("Override chords:", chordOverrideCombo);

    beatsPerChordSpin = new QDoubleSpinBox(progPage);
    beatsPerChordSpin->setRange(0.5, 32.0);
    beatsPerChordSpin->setValue(4.0);
    beatsPerChordSpin->setSingleStep(1.0);
    beatsPerChordSpin->setSuffix(" beats");
    patForm->addRow("Per chord:", beatsPerChordSpin);

    layout->addWidget(patGroup);

    auto* arpGroup = new QGroupBox("Playback");
    auto* arpForm = new QFormLayout(arpGroup);

    progArpChk = new QCheckBox("Arpeggiate", progPage);
    arpForm->addRow("", progArpChk);

    progArpRateSpin = new QDoubleSpinBox(progPage);
    progArpRateSpin->setRange(0.03125, 2.0);
    progArpRateSpin->setValue(0.125);
    progArpRateSpin->setSingleStep(0.03125);
    progArpRateSpin->setSuffix(" beat");
    arpForm->addRow("Arp Rate:", progArpRateSpin);

    progDurSpin = new QDoubleSpinBox(progPage);
    progDurSpin->setRange(0.25, 16.0);
    progDurSpin->setValue(2.0);
    progDurSpin->setSingleStep(0.25);
    progDurSpin->setSuffix(" beats");
    arpForm->addRow("Note Length:", progDurSpin);

    layout->addWidget(arpGroup);

    paramStack->addWidget(progPage);
}

// ── Generate ──

void PhraseGeneratorDialog::onGenerate()
{
    int rootNote = rootCombo->currentData().toInt();
    int modeIdx = modeCombo->currentData().toInt();
    int lowNote = lowNoteSpin->value();
    int highNote = (std::max)(lowNote + 1, highNoteSpin->value());
    int vel = velocitySlider->value();
    int modeType = modeTypeCombo->currentIndex();

    std::vector<PhraseGenerator::GeneratedNote> notes;
    double lengthBeats = 4.0;

    if (modeType == 0) // Phrase
    {
        auto style = static_cast<PhraseGenerator::Style>(styleCombo->currentData().toInt());

        PhraseGenerator::PhraseParams params;
        params.style = style;
        params.lengthBeats = static_cast<double>(lengthSpin->value());
        params.density = densitySpin->value();
        params.scaleRoot = rootNote;
        params.scaleMode = modeIdx;
        params.lowNote = lowNote;
        params.highNote = highNote;
        params.minVelocity = (std::max)(30, vel - 20);
        params.maxVelocity = (std::min)(127, vel + 10);

        notes = PhraseGenerator::generatePhrase(params);
        lengthBeats = params.lengthBeats;
    }
    else if (modeType == 1) // Single Chord
    {
        PhraseGenerator::ChordParams params;
        params.scaleRoot = rootNote;
        params.scaleMode = modeIdx;
        params.lowNote = lowNote;
        params.highNote = highNote;
        params.minVelocity = (std::max)(30, vel - 20);
        params.maxVelocity = (std::min)(127, vel + 10);
        params.chordType = chordTypeCombo->currentData().toInt();
        params.voicing = voicingCombo->currentData().toInt();
        params.inversion = inversionCombo->currentData().toInt();
        params.arpeggiate = arpeggiateChk->isChecked();
        params.arpeggioRate = arpRateSpin->value();
        params.durationBeats = chordDurationSpin->value();

        // Use the first matching scale pitch as the root
        auto pitches = PhraseGenerator::buildScalePitches(rootNote, modeIdx, lowNote, highNote);
        int chordRoot = pitches.empty() ? 60 : pitches[0];
        notes = PhraseGenerator::generateChord(chordRoot, params);
        lengthBeats = params.arpeggiate
            ? params.arpeggioRate * static_cast<double>(notes.size())
            : params.durationBeats;
    }
    else // Chord Progression
    {
        PhraseGenerator::ProgressionParams params;
        params.scaleRoot = rootNote;
        params.scaleMode = modeIdx;
        params.lowNote = lowNote;
        params.highNote = highNote;
        params.minVelocity = (std::max)(30, vel - 20);
        params.maxVelocity = (std::min)(127, vel + 10);
        params.patternIndex = patternCombo->currentData().toInt();
        params.chordTypeOverride = chordOverrideCombo->currentData().toInt();
        params.arpeggiate = progArpChk->isChecked();
        params.arpeggioRate = progArpRateSpin->value();
        params.durationBeats = progDurSpin->value();
        params.beatsPerChord = beatsPerChordSpin->value();

        notes = PhraseGenerator::generateProgression(params);

        // Estimate length
        const auto& patterns = PhraseGenerator::getProgressionPatterns();
        const auto* pat = &patterns[0];
        for (const auto& p : patterns)
        {
            if (p.index == params.patternIndex)
            {
                pat = &p;
                break;
            }
        }
        lengthBeats = params.beatsPerChord * static_cast<double>(pat->chords.size());
    }

    if (notes.empty())
    {
        previewLabel->setText("No notes generated — check range and scale.");
        return;
    }

    // Create the MIDI clip on the target track
    auto trackList = engine.getProjectModel().getTrackListTree();
    if (trackIndex < 0 || trackIndex >= trackList.getNumChildren())
    {
        previewLabel->setText("Invalid track.");
        return;
    }

    auto trackTree = trackList.getChild(trackIndex);
    auto clipList = trackTree.getChildWithName(IDs::CLIP_LIST);
    if (!clipList.isValid())
    {
        clipList = juce::ValueTree(IDs::CLIP_LIST);
        trackTree.addChild(clipList, -1, &engine.getProjectModel().getUndoManager());
    }

    auto& um = engine.getProjectModel().getUndoManager();

    auto clipTree = ProjectModel::createMidiClipEmpty("Generated", 0.0, lengthBeats);
    clipTree.setProperty(IDs::color, static_cast<int>(0xFF88CC44), nullptr);
    auto midiNotes = clipTree.getChildWithName(IDs::MIDI_NOTE_LIST);
    for (const auto& n : notes)
        midiNotes.addChild(ProjectModel::createMidiNote(n.noteNumber, n.velocity, n.startBeat, n.durationBeats), -1, nullptr);

    clipList.addChild(clipTree, -1, &um);

    previewLabel->setText(QString("Generated %1 notes in '%2'.")
        .arg(notes.size())
        .arg(QString::fromUtf8(trackTree.getProperty(IDs::name).toString().toRawUTF8())));

    engine.getMainProcessor()->rebuildRoutingGraph();

    accept();
}
