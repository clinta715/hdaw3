#pragma once
#include <QWidget>
#include <QGraphicsView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <vector>
#include "../engine/AudioEngine.h"
#include "../engine/ClipClipboard.h"
#include "TimelineScene.h"
#include "TimelineToolbar.h"
#include "TrackHeaderWidget.h"
#include "TimeRuler.h"
#include "PlayheadCursor.h"
#include "LoopMarker.h"
#include "MarkerItem.h"
#include "TimelineInteraction.h"

class TimelineView : public QWidget, private juce::ValueTree::Listener
{
    Q_OBJECT
public:
    TimelineView(AudioEngine& engine, QWidget* parent = nullptr);
    ~TimelineView() override;

    TimelineScene* getScene() const { return timelineScene; }
    TimelineToolbar* getToolbar() const { return toolbar; }
    TimelineInteraction* getInteraction() const { return interaction; }

    // Clipboard operations — operate on currently selected clips.
    // pasteClips uses the current playhead position as the origin and
    // places each pasted clip on its source track (or selected track if
    // the source track no longer exists).
    void cutSelectedClips();
    void copySelectedClips();
    void pasteClips();
    void duplicateSelectedClips();

    static constexpr double duplicateOffsetSeconds = 0.25;

signals:
    void trackSelectionChanged(int trackIndex);
    void addTrackClicked();
    void addTrackWithFX(const juce::String& fxType);
    void addTrackWithPlugin(const juce::String& pluginID, const juce::String& pluginFormat);
    void automationToggled(int trackIndex);
    void inputMonitoringChanged(int trackIndex, bool enabled);
    void recordToggled();
    void ccRecordToggled(bool armed);
    void playToggled();
    void stopRequested();
    void rewindRequested();
    void bpmChanged(double bpm);
    void metronomeToggled(bool enabled);
    void countInToggled(bool enabled);
    void timeSigChanged(int numerator, int denominator);
    void midiDeviceChanged(const QString& deviceIdentifier);
    void defaultClipLenChanged(double beats);

public slots:
    void setZoom(double factor);
    void zoomIn();
    void zoomOut();
    void setFollowPlayhead(bool follow);
    void scrollToPlayhead();
    void selectTrack(int index);
    void setSelectedTrack(int index) { selectedTrack = index; }

    // Zoom-to-fit: adjust pixelsPerSecond so the visible viewport shows
    // either all clips (fitAll) or just the current selection
    // (fitSelection). Both clamp to [minPPS, maxPPS] and recentre the
    // horizontal scroll on the resulting range.
    void zoomToFitAll();
    void zoomToFitSelection();

private:
    void setupUI();
    void connectSignals();
    void syncRulerWithScene();

    // ValueTree listener — fires when the project tree changes (markers, etc.)
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded) override;
    void valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichItWasRemoved) override;

    bool eventFilter(QObject* obj, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void handleFileDrop(const QString& filePath, QPointF scenePos);
    void handleContextMenu(QContextMenuEvent* event);
    void handleClipContextMenu(ClipItem* clip, const QPoint& globalPos);
    void handleEmptyAreaContextMenu(const QPointF& scenePos, const QPoint& globalPos);
    void handleKeyPress(QKeyEvent* event);
    void handleDrop(QDropEvent* event);

    AudioEngine& engine;

    TimelineToolbar* toolbar;
    TrackHeaderWidget* trackHeaders;
    QGraphicsView* graphicsView;
    TimelineScene* timelineScene;
    TimelineInteraction* interaction;

    TimeRuler* rulerItem;
    PlayheadCursor* playheadCursor;
    LoopMarker* loopStartMarker;
    LoopMarker* loopEndMarker;

    // Marker items, indexed by their underlying MARKER ValueTree. We
    // listen to the MARKER_LIST subtree of the project tree and create /
    // destroy MarkerItem objects as markers are added or removed.
    std::vector<MarkerItem*> markerItems;
    juce::ValueTree markerListTree;
    void syncMarkers();
    MarkerItem* findMarkerItem(const juce::ValueTree& markerTree);
    void onMarkerPropertyChanged(const juce::ValueTree& markerTree, const juce::Identifier& property);
    void onMarkerAdded(const juce::ValueTree& markerTree);
    void onMarkerRemoved(const juce::ValueTree& markerTree);

    double pixelsPerSecond = 10.0;
    static constexpr double zoomFactor = 1.5;
    static constexpr double minPPS = 1.0;
    static constexpr double maxPPS = 200.0;

    int selectedTrack = -1;
};
