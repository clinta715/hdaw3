#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <juce_core/juce_core.h>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"
#include "../common/PluginService.h"
#include "../common/PluginParamService.h"

class AudioEngine;
#include "FXSlotRow.h"

class FXChainWidget : public QWidget
{
    Q_OBJECT
public:
    FXChainWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~FXChainWidget() override;

    void loadTrack(int trackIndex);
    void clear();

signals:
    void chainChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void rebuildUI();
    void addFXSlot(const juce::String& type = "eq");
    int indexAtDropY(int y) const;

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    PluginService* pluginService = nullptr;
    PluginParamService* paramService = nullptr;
    int currentTrack = -1;
    QVBoxLayout* slotLayout;
    QScrollArea* scrollArea;
    QWidget* slotContainer;
    QLabel* headerLabel;
};
