#pragma once
#include <QGraphicsObject>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include "../common/ProjectCommands.h"
#include "../common/TransportCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;

class TimeRuler : public QGraphicsObject
{
    Q_OBJECT
public:
    TimeRuler(AudioEngine& engine, double height = 30.0);
    ~TimeRuler() override;

    void setPixelsPerSecond(double pps);
    double getPixelsPerSecond() const { return pixelsPerSecond; }

    void setShowBeats(bool show) { showBeats = show; }
    bool isShowingBeats() const { return showBeats; }

    void setLoopBounds(double start, double end);
    double getLoopStart() const { return loopStart; }
    double getLoopEnd() const { return loopEnd; }
    bool isInLoop(double time) const { return time >= loopStart && time <= loopEnd; }

    /// Write current loopStart/loopEnd to the ValueTree (called from LoopMarker on drag end).
    void commitLoopBounds();

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    double timeFromX(double x) const;
    double xFromTime(double t) const;

signals:
    void seekRequested(double timeSeconds);
    void loopBoundsChanged(double start, double end);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    TransportCommands* transportCmds = nullptr;
    ReadModel* readModel = nullptr;
    double height;
    double pixelsPerSecond = 10.0;
    bool showBeats = true;

    double loopStart = 0.0;
    double loopEnd = 8.0;

    enum DragMode { None, Seek, DragLoopStart, DragLoopEnd };
    DragMode dragMode = None;
};
