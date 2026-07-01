#pragma once
#include <QWidget>
#include <QGraphicsView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include "../engine/AudioEngine.h"
#include "TimelineScene.h"
#include "TimelineToolbar.h"
#include "TrackHeaderWidget.h"
#include "TimeRuler.h"
#include "PlayheadCursor.h"
#include "LoopMarker.h"
#include "TimelineInteraction.h"

class TimelineView : public QWidget
{
    Q_OBJECT
public:
    TimelineView(AudioEngine& engine, QWidget* parent = nullptr);
    ~TimelineView() override;

    TimelineScene* getScene() const { return timelineScene; }
    TimelineToolbar* getToolbar() const { return toolbar; }
    TimelineInteraction* getInteraction() const { return interaction; }

signals:
    void trackSelectionChanged(int trackIndex);
    void addTrackClicked();
    void addTrackWithFX(const juce::String& fxType);
    void addTrackWithPlugin(const juce::String& pluginID, const juce::String& pluginFormat);
    void automationToggled(int trackIndex);
    void inputMonitoringChanged(int trackIndex, bool enabled);
    void recordToggled();
    void playToggled();
    void stopRequested();
    void rewindRequested();
    void bpmChanged(double bpm);
    void metronomeToggled(bool enabled);
    void countInToggled(bool enabled);
    void timeSigChanged(int numerator, int denominator);
    void defaultClipLenChanged(double beats);

public slots:
    void setZoom(double factor);
    void zoomIn();
    void zoomOut();
    void setFollowPlayhead(bool follow);
    void scrollToPlayhead();
    void selectTrack(int index);

private:
    void setupUI();
    void connectSignals();
    void syncRulerWithScene();

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

    double pixelsPerSecond = 10.0;
    static constexpr double zoomFactor = 1.5;
    static constexpr double minPPS = 1.0;
    static constexpr double maxPPS = 200.0;
};
