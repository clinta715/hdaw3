#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QTimer>
#include "../engine/AudioEngine.h"
#include "MixerStripWidget.h"
#include "VUMeter.h"

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
    QWidget* stripContainer;
    QHBoxLayout* stripLayout;
    QTimer vuTimer;
    HDAW::VUMeter* masterVU = nullptr;
};
