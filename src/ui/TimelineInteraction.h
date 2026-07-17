#pragma once
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QMap>
#include <QGraphicsSceneMouseEvent>
#include <juce_data_structures/juce_data_structures.h>
#include "../common/ProjectCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class TimelineScene;
class ClipItem;
class QGraphicsRectItem;
class AudioEngine;

class TimelineInteraction : public QObject
{
    Q_OBJECT
public:
    TimelineInteraction(TimelineScene* scene, AudioEngine& engine, QObject* parent = nullptr);
    ~TimelineInteraction() override;

    void setSnapEnabled(bool enabled) { snapEnabled = enabled; }
    bool isSnapEnabled() const { return snapEnabled; }

    enum SnapDivision { Bar, Beat, Eighth, Sixteenth, Off };
    void setSnapDivision(SnapDivision div) { snapDivision = div; }
    SnapDivision getSnapDivision() const { return snapDivision; }

    double snapToGrid(double timeSeconds) const;

    void setDefaultClipDuration(double dur) { defaultClipDuration = dur; }
    double getDefaultClipDuration() const { return defaultClipDuration; }

    bool handleMousePress(QGraphicsSceneMouseEvent* e);
    bool handleMouseMove(QGraphicsSceneMouseEvent* e);
    bool handleMouseRelease(QGraphicsSceneMouseEvent* e);
    bool handleMouseDoubleClick(QGraphicsSceneMouseEvent* e);

    // Multi-selection helpers (called from TimelineScene or elsewhere)
    void clearSelection();
    void selectClip(ClipItem* clip, bool additive);
    void selectRange(ClipItem* fromClip, ClipItem* toClip);
    QList<ClipItem*> getSelectedClips() const;
    void deleteSelectedClips();

private:
    enum DragMode { None, Move, TrimLeft, TrimRight, FadeIn, FadeOut, RubberBand, Duplicate };

    void startRubberBand(const QPointF& scenePos, bool additive);
    void updateRubberBand(const QPointF& scenePos);
    void endRubberBand();

public:
    void setUndoManager(juce::UndoManager* um) { undoManager = um; }

private:
    AudioEngine& engine;
    TimelineScene* scene;
    bool snapEnabled = true;
    SnapDivision snapDivision = Beat;

    ProjectCommands* projectCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    juce::UndoManager* undoManager = nullptr;

    DragMode dragMode = None;
    ClipItem* dragItem = nullptr;
    QPointF dragStartPos;
    double dragStartValue = 0.0;
    double dragPPS = 10.0;
    double lastClipDuration = 4.0;
    double defaultClipDuration = 4.0;

    // Multi-selection state
    ClipItem* lastClickedClip = nullptr;
    bool rubberBandAdditive = false;
    QPointF rubberBandStartPos;
    QGraphicsRectItem* rubberBandRectItem = nullptr;

    // Duplicate-drag state
    QList<ClipItem*> duplicateItems;
    bool duplicatesCreated = false;

    // Cross-track drag state
    int dragStartTrackIndex = -1;
    int dragCurrentTrackIndex = -1;
    double dragStartYOffset = 0.0;  // Y offset within the track at drag start
    QMap<ClipItem*, double> dragStartClipY;  // per-clip Y at drag start for continuous tracking
};
