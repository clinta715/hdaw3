#pragma once
#include <QWidget>
#include <QScrollBar>
#include <juce_core/juce_core.h>
#include "PianoRollModel.h"
#include "PianoKeysWidget.h"
#include "NoteGridWidget.h"
#include "VelocityLaneWidget.h"
#include "CCLaneWidget.h"
#include "PianoRollRuler.h"
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;

class QPushButton;
class QComboBox;
class QCheckBox;

class PianoRollWidget : public QWidget
{
    Q_OBJECT
public:
    PianoRollWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~PianoRollWidget() override;

    void loadClip(juce::ValueTree clipTree);
    void clear();
    void setPlayheadPosition(double seconds, double bpm);

signals:
    void clipClosed();

private:
    void setupUI();
    void connectSignals();
    void syncScrollBars();
    void updateZoom(double factor);

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    PianoRollModel model;

    PianoRollRuler* ruler;
    PianoKeysWidget* keys;
    NoteGridWidget* noteGrid;
    VelocityLaneWidget* velocityLane;
    QScrollBar* hScrollBar;
    QScrollBar* vScrollBar;

    QPushButton* snapBtn;
    QComboBox* snapCombo;

    // Chord stamp controls
    QCheckBox* chordStampChk;
    QComboBox* chordStampCombo;
    QComboBox* chordVoicingCombo;

    CCLaneWidget* ccLane;
    QComboBox* ccCombo;

    bool isLoaded = false;
};
