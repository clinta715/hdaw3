#pragma once
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <juce_data_structures/juce_data_structures.h>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"
#include "../model/ProjectModel.h"
#include "ClipItem.h"
#include "AudioClipItem.h"
#include "MidiClipItem.h"
#include "TimelineInteraction.h"
#include <map>

class TimelineScene : public QGraphicsScene, private juce::ValueTree::Listener
{
    Q_OBJECT
public:
    TimelineScene(AudioEngine& engine, QObject* parent = nullptr);
    ~TimelineScene() override;

    void rebuildFromValueTree();

    double getPixelsPerSecond() const { return pixelsPerSecond; }
    void setPixelsPerSecond(double pps);

    int getTrackCount() const { return trackCount; }
    double getTrackY(int trackIndex) const;
    int trackIndexAtY(double y) const;

    AudioEngine& getEngine() { return engine; }
    double getRulerHeight() const { return rulerHeight; }
    double getTrackHeight(int trackIndex) const;

    void setInteraction(TimelineInteraction* i) { interaction = i; }

signals:
    void trackCountChanged(int count);
    void clipSelected(juce::ValueTree clipTree);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;

private:
    void attachListener(juce::ValueTree tree);
    void detachListener();

    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded) override;
    void valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override;

    ClipItem* createClipItem(juce::ValueTree clipTree);
    void removeClipItem(juce::ValueTree clipTree);
    void updateClipItem(juce::ValueTree clipTree);

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    double pixelsPerSecond = 10.0;
    int trackCount = 0;

    TimelineInteraction* interaction = nullptr;

    juce::ValueTree listenerRoot;

    static constexpr double trackHeaderWidth = 120.0;
    static constexpr double defaultTrackHeight = 80.0;
    static constexpr double rulerHeight = 30.0;

    std::map<int, ClipItem*> clipItemMap;
};
