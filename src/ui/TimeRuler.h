#pragma once
#include <QGraphicsObject>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include "../engine/AudioEngine.h"

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

private:

    AudioEngine& engine;
    double height;
    double pixelsPerSecond = 10.0;
    bool showBeats = true;

    double loopStart = 0.0;
    double loopEnd = 8.0;

    enum DragMode { None, Seek, DragLoopStart, DragLoopEnd };
    DragMode dragMode = None;
};
