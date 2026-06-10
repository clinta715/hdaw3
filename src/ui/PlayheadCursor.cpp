#include "PlayheadCursor.h"
#include "Theme.h"
#include <QPainter>
#include <QtGlobal>

PlayheadCursor::PlayheadCursor(HDAW::TransportManager& tm, double pps, QGraphicsItem* parent)
    : QGraphicsObject(parent), transportManager(tm), pixelsPerSecond(pps)
{
    setZValue(999);
}

PlayheadCursor::~PlayheadCursor()
{
    stopSync();
}

void PlayheadCursor::setPixelsPerSecond(double pps)
{
    pixelsPerSecond = pps;
}

void PlayheadCursor::setViewRectHeight(double h)
{
    viewHeight = h;
    prepareGeometryChange();
    update();
}

void PlayheadCursor::startSync()
{
    connect(&syncTimer, &QTimer::timeout, this, &PlayheadCursor::syncPosition);
    syncTimer.start(33);
}

void PlayheadCursor::stopSync()
{
    syncTimer.stop();
    disconnect(&syncTimer, nullptr, this, nullptr);
}

void PlayheadCursor::syncPosition()
{
    if (!transportManager.isPlayingNow())
        return;

    int64_t sample = transportManager.getCurrentSample();
    double sr = transportManager.getSampleRate();
    double timeSeconds = sr > 0 ? static_cast<double>(sample) / sr : 0.0;
    double x = timeSeconds * pixelsPerSecond;

    setPos(x, y());
}

QRectF PlayheadCursor::boundingRect() const
{
    return QRectF(-5, 0, 6, viewHeight);
}

QPainterPath PlayheadCursor::shape() const
{
    return QPainterPath();
}

void PlayheadCursor::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing, false);

    // Playhead triangle at top
    QPolygonF tri;
    tri << QPointF(0, 0) << QPointF(-5, 8) << QPointF(5, 8);
    painter->setPen(Qt::NoPen);
    painter->setBrush(ThemeColors::accent());
    painter->drawPolygon(tri);

    // Vertical line
    painter->setPen(QPen(ThemeColors::accent(), 1));
    painter->drawLine(QPointF(0, 8), QPointF(0, viewHeight));
}
