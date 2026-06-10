#pragma once
#include <QWidget>
#include <QScrollBar>
#include "PianoRollModel.h"
#include "PianoKeysWidget.h"
#include "NoteGridWidget.h"
#include "VelocityLaneWidget.h"
#include "PianoRollRuler.h"
#include "../engine/AudioEngine.h"

class PianoRollWidget : public QWidget
{
    Q_OBJECT
public:
    PianoRollWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~PianoRollWidget() override;

    void loadClip(juce::ValueTree clipTree);
    void clear();

signals:
    void clipClosed();

private:
    void setupUI();
    void connectSignals();
    void syncScrollBars();
    void updateZoom(double factor);

    AudioEngine& engine;
    PianoRollModel model;

    PianoRollRuler* ruler;
    PianoKeysWidget* keys;
    NoteGridWidget* noteGrid;
    VelocityLaneWidget* velocityLane;
    QScrollBar* hScrollBar;
    QScrollBar* vScrollBar;

    bool isLoaded = false;
};
