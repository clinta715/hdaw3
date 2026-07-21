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
#include <juce_data_structures/juce_data_structures.h>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"
#include "../common/PluginService.h"
#include "../common/PluginParamService.h"

class AudioEngine;
#include "FXSlotRow.h"

class FXChainWidget : public QWidget, private juce::ValueTree::Listener
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
    void addPluginSlot(const QString& pluginID);
    int indexAtDropY(int y) const;

    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded) override;
    void valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int index) override;
    void valueTreeChildOrderChanged(juce::ValueTree& parentTree, int oldIndex, int newIndex) override;
    bool isCurrentTrackFxChain(const juce::ValueTree& tree) const;

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
