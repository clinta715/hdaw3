#pragma once
#include <QWidget>
#include <QLabel>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;

class StatusBar : public QWidget
{
    Q_OBJECT
public:
    explicit StatusBar(AudioEngine& engine, QWidget* parent = nullptr);

public slots:
    void setBPM(double bpm);
    void setTimeSignature(int numerator, int denominator);
    void setSampleRate(double sampleRate);
    void setSelectedTrack(int trackIndex, const QString& trackName);
    void setMidiDevice(const QString& deviceName);
    void setRecording(bool recording);
    void setSelectionCount(int count);

private:
    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;

    QLabel* bpmLabel;
    QLabel* timeSigLabel;
    QLabel* sampleRateLabel;
    QLabel* trackLabel;
    QLabel* midiLabel;
    QLabel* recLabel;
    QLabel* selLabel;
};
