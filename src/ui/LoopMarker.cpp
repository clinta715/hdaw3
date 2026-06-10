#include "LoopMarker.h"
#include "Theme.h"
#include "TimeRuler.h"
#include <QPainter>
#include <QCursor>
#include <QGraphicsScene>

LoopMarker::LoopMarker(TimeRuler& r, Side s)
    : ruler(r), side(s)
{
    setCursor(Qt::SizeHorCursor);
    setAcceptHoverEvents(true);
    setZValue(50);
}

LoopMarker::~LoopMarker() = default;

QRectF LoopMarker::boundingRect() const
{
    return QRectF(-markerWidth / 2, -markerHeight, markerWidth, markerHeight + 4);
}

void LoopMarker::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);

    QColor color = (side == Left) ? ThemeColors::success() : ThemeColors::warning();

    // Flag triangle
    QPolygonF flag;
    if (side == Left)
    {
        flag << QPointF(0, -markerHeight)
             << QPointF(markerWidth, -markerHeight + 6)
             << QPointF(0, -markerHeight + 12);
    }
    else
    {
        flag << QPointF(0, -markerHeight)
             << QPointF(-markerWidth, -markerHeight + 6)
             << QPointF(0, -markerHeight + 12);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(flag);

    // Vertical line extending down
    painter->setPen(QPen(color, 1));
    painter->drawLine(QPointF(0, -markerHeight + 12), QPointF(0, 0));
}

void LoopMarker::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        dragging = true;
        event->accept();
    }
}

void LoopMarker::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (!dragging) return;

    double x = event->scenePos().x();
    double t = ruler.mapFromScene(QPointF(x, 0)).x();
    double seconds = t / ruler.getPixelsPerSecond();
    seconds = (std::max)(0.0, seconds);

    time = seconds;
    setPos(t, y());

    ruler.setLoopBounds(
        side == Left ? time : ruler.getLoopStart(),
        side == Right ? time : ruler.getLoopEnd()
    );

    event->accept();
}

void LoopMarker::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && dragging)
    {
        dragging = false;
        event->accept();
    }
}
