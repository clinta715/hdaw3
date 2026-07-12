#include "AudioClipEditorWidget.h"
#include "../engine/AudioEngine.h"
#include "Theme.h"
#include "DebugLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>

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
}

void AudioClipEditorWidget::loadClip(juce::ValueTree clipTree)
{
    currentClip = clipTree;
    isLoaded = currentClip.isValid();

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

    settingUi = false;
}
