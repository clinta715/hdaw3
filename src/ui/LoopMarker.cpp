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
    setAcceptedMouseButtons(Qt::LeftButton);
    setZValue(50);
}

LoopMarker::~LoopMarker() = default;

QRectF LoopMarker::boundingRect() const
{
    return QRectF(-markerWidth - hitMargin, -markerHeight - hitMargin,
                  (markerWidth + hitMargin) * 2, markerHeight + 8 + hitMargin * 2);
}

void LoopMarker::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);

    QColor color = (side == Left) ? ThemeColors::success() : ThemeColors::warning();

    QPolygonF flag;
    if (side == Left)
    {
        flag << QPointF(-1, -markerHeight)
             << QPointF(markerWidth, -markerHeight + 8)
             << QPointF(-1, -markerHeight + 16);
    }
    else
    {
        flag << QPointF(1, -markerHeight)
             << QPointF(-markerWidth, -markerHeight + 8)
             << QPointF(1, -markerHeight + 16);
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawPolygon(flag);

    painter->setPen(QPen(color, 2));
    painter->drawLine(QPointF(0, -markerHeight + 16), QPointF(0, 4));
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

    double t = ruler.mapFromScene(event->scenePos()).x();
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
        ruler.commitLoopBounds();
        event->accept();
    }
}
