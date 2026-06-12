#pragma once
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>

class TimeRuler;

class LoopMarker : public QGraphicsItem
{
public:
    enum Side { Left, Right };

    LoopMarker(TimeRuler& ruler, Side side);
    ~LoopMarker() override;

    Side getSide() const { return side; }
    void setTime(double timeSeconds) { time = timeSeconds; update(); }
    double getTime() const { return time; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    static constexpr double markerWidth = 14.0;
    static constexpr double markerHeight = 22.0;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    TimeRuler& ruler;
    Side side;
    double time = 0.0;
    bool dragging = false;
};
