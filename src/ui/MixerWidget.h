#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QTimer>
#include "../common/ProjectCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"
#include "MixerStripWidget.h"
#include "VUMeter.h"

class AudioEngine;

class MixerWidget : public QWidget
{
    Q_OBJECT
public:
    MixerWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~MixerWidget() override;

    void rebuild();
    void updateMasterMeter();

signals:
    void fxButtonClicked(int trackIndex);

private:
    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    QWidget* stripContainer;
    QHBoxLayout* stripLayout;
    QTimer vuTimer;
    HDAW::VUMeter* masterVU = nullptr;
};
