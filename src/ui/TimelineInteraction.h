#pragma once
#include <QObject>
#include <QPointF>
#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <juce_data_structures/juce_data_structures.h>

class TimelineScene;
class ClipItem;
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

private:
    enum DragMode { None, Move, TrimLeft, TrimRight, FadeIn, FadeOut, RubberBand };

public:
    void setUndoManager(juce::UndoManager* um) { undoManager = um; }

private:
    AudioEngine& engine;
    TimelineScene* scene;
    bool snapEnabled = true;
    SnapDivision snapDivision = Beat;

    juce::UndoManager* undoManager = nullptr;

    DragMode dragMode = None;
    ClipItem* dragItem = nullptr;
    QPointF dragStartPos;
    double dragStartValue = 0.0;
    double dragPPS = 10.0;
    QPointF rubberBandOrigin;
    QGraphicsRectItem* rubberBandRect = nullptr;

    double lastClipDuration = 4.0;
    double defaultClipDuration = 4.0;
};
