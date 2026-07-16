#pragma once
#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <juce_core/juce_core.h>
#include "../model/ProjectModel.h"
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;
class QKeyEvent;
#include "AudioWaveformWidget.h"
#include "GainEnvelopeEditor.h"

class AudioClipEditorWidget : public QWidget
{
    Q_OBJECT
public:
    AudioClipEditorWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~AudioClipEditorWidget() override;

    void loadClip(juce::ValueTree clipTree);
    void clear();
    void updatePlayhead(double seconds);

signals:
    void clipClosed();

private:
    void setupUI();
    void connectSignals();
    void updateControls();
    void loadGainEnvelope();
    void reloadClip();
    void onGainEnvelopeChanged(const QVector<GainEnvelopeEditor::Point>& points);

    // Slicing handlers
    void onSliceAtPlayhead();
    void onSliceAtTransients();
    void onSliceAtSelection();

    // Region clipboard handlers
    void keyPressEvent(QKeyEvent* event) override;
    void onCopyRegion();
    void onCutRegion();
    void onPasteRegion();

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    juce::ValueTree currentClip;

    AudioWaveformWidget* waveform;
    QLabel* titleLabel;
    QPushButton* closeBtn;
    QPushButton* zoomInBtn;
    QPushButton* zoomOutBtn;

    QSlider* gainSlider;
    QLabel* gainLabel;
    QDoubleSpinBox* fadeInSpin;
    QDoubleSpinBox* fadeOutSpin;
    QCheckBox* loopCheck;
    QDoubleSpinBox* offsetSpin;
    QDoubleSpinBox* durationSpin;
    QLabel* sourceLabel;
    QLabel* infoLabel;

    // Timestretch controls
    QDoubleSpinBox* sourceBpmSpin;
    QComboBox* stretchModeCombo;
    QDoubleSpinBox* stretchRatioSpin;
    QPushButton* fitToLoopBtn;

    // Slicing controls
    QPushButton* sliceAtPlayheadBtn = nullptr;
    QPushButton* sliceAtTransientsBtn = nullptr;
    QPushButton* sliceAtSelectionBtn = nullptr;

    // Region clipboard controls
    QPushButton* copyRegionBtn = nullptr;
    QPushButton* cutRegionBtn = nullptr;
    QPushButton* pasteRegionBtn = nullptr;
    QLabel* selectionLabel = nullptr;

    // Gain envelope editor
    GainEnvelopeEditor* gainEnvelopeEditor = nullptr;

    bool settingUi = false;
    bool isLoaded = false;
};
