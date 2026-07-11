#pragma once
#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <juce_core/juce_core.h>
#include "../model/ProjectModel.h"
#include "../common/ProjectCommands.h"
#include "../common/ReadModel.h"
#include "AudioWaveformWidget.h"

class AudioEngine;

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

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
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

    bool settingUi = false;
    bool isLoaded = false;
};
