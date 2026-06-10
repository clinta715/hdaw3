#include "TimeRuler.h"
#include "Theme.h"
#include <QPainter>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QAction>
#include <QInputDialog>
#include <QApplication>
#include <cmath>

TimeRuler::TimeRuler(AudioEngine& ae, double h)
    : engine(ae), height(h)
{
    setCursor(Qt::PointingHandCursor);
    setAcceptHoverEvents(true);
}

TimeRuler::~TimeRuler() = default;

void TimeRuler::setPixelsPerSecond(double pps)
{
    if (pixelsPerSecond != pps)
    {
        pixelsPerSecond = pps;
        prepareGeometryChange();
    }
}

void TimeRuler::setLoopBounds(double start, double end)
{
    loopStart = std::min(start, end);
    loopEnd = std::max(start, end);
    update();
}

QRectF TimeRuler::boundingRect() const
{
    double projectDuration = 300.0;
    return QRectF(0, 0, std::max(projectDuration * pixelsPerSecond + 200, 10000.0), height);
}

double TimeRuler::timeFromX(double x) const
{
    return x / pixelsPerSecond;
}

double TimeRuler::xFromTime(double t) const
{
    return t * pixelsPerSecond;
}

void TimeRuler::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing, false);

    // Background
    painter->fillRect(boundingRect(), ThemeColors::rulerBg());

    // Bottom border
    painter->setPen(QPen(ThemeColors::border(), 1));
    painter->drawLine(QPointF(0, height - 1), QPointF(boundingRect().width(), height - 1));

    // Loop region highlight
    double lx = xFromTime(loopStart);
    double rx = xFromTime(loopEnd);
    if (rx > lx)
    {
        painter->fillRect(QRectF(lx, 0, rx - lx, height), QColor(ThemeColors::accent().red(), ThemeColors::accent().green(), ThemeColors::accent().blue(), 40));
        painter->setPen(QPen(ThemeColors::accent(), 1));
        painter->drawLine(QPointF(lx, 0), QPointF(lx, height));
        painter->drawLine(QPointF(rx, 0), QPointF(rx, height));
        painter->setBrush(ThemeColors::success());
        painter->setPen(Qt::NoPen);
        QPolygonF flagL;
        flagL << QPointF(lx, 0) << QPointF(lx + 8, 4) << QPointF(lx, 8);
        painter->drawPolygon(flagL);
        painter->setBrush(ThemeColors::warning());
        QPolygonF flagR;
        flagR << QPointF(rx, 0) << QPointF(rx - 8, 4) << QPointF(rx, 8);
        painter->drawPolygon(flagR);
    }

    // Draw tempo change points
    auto tempoPoints = engine.getProjectModel().getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (tempoPoints.isValid())
    {
        double currentBpm = engine.getTransportManager().getBPM();
        for (int i = 0; i < tempoPoints.getNumChildren(); ++i)
        {
            auto pt = tempoPoints.getChild(i);
            double ptTime = pt.getProperty(IDs::startTime);
            double ptBpm = pt.getProperty(IDs::tempo);
            double ptX = xFromTime(ptTime);
            painter->setPen(QPen(ThemeColors::accentBright(), 2));
            painter->drawLine(QPointF(ptX, height - 12), QPointF(ptX, height - 3));
            painter->setPen(ThemeColors::accentBright());
            QFont f = painter->font();
            f.setPointSize(6);
            painter->setFont(f);
            painter->drawText(QPointF(ptX + 2, height - 5),
                QString("%1").arg(static_cast<int>(ptBpm)));
        }
    }

    if (!showBeats)
    {
        double visibleDuration = boundingRect().width() / pixelsPerSecond;
        double step = 1.0;
        if (pixelsPerSecond < 5) step = 5.0;
        if (pixelsPerSecond < 1) step = 10.0;

        painter->setPen(ThemeColors::textSecondary());
        QFont f = painter->font();
        f.setPointSize(8);
        painter->setFont(f);

        for (double t = 0; t < visibleDuration; t += step)
        {
            double x = xFromTime(t);
            int mins = static_cast<int>(t) / 60;
            int secs = static_cast<int>(t) % 60;
            QString label = QString("%1:%2").arg(mins).arg(secs, 2, 10, QLatin1Char('0'));
            painter->drawLine(QPointF(x, height - 8), QPointF(x, height));
            painter->drawText(QPointF(x + 2, height - 10), label);
        }
        return;
    }

    // Bars:Beats display
    double bpm = engine.getTransportManager().getBPM();
    double secondsPerBeat = 60.0 / std::max(1.0, bpm);
    double visibleDuration = boundingRect().width() / pixelsPerSecond;

    double beatsPerDivision;
    double pxPerBeat = pixelsPerSecond * secondsPerBeat;

    if (pxPerBeat < 4)
        beatsPerDivision = 4.0;
    else if (pxPerBeat < 16)
        beatsPerDivision = 1.0;
    else
        beatsPerDivision = 0.25;

    double totalBeats = (visibleDuration / secondsPerBeat);

    QFont f = painter->font();
    f.setPointSize(8);
    painter->setFont(f);

    for (double beat = 0; beat < totalBeats; beat += beatsPerDivision)
    {
        double t = beat * secondsPerBeat;
        double x = xFromTime(t);
        bool isBar = (static_cast<int>(beat) % 4 == 0);

        painter->setPen(isBar ? QColor(255, 255, 255, 20) : QColor(255, 255, 255, 10));
        painter->drawLine(QPointF(x, height - (isBar ? 12 : 6)), QPointF(x, height));

        if (isBar)
        {
            int barNum = static_cast<int>(beat / 4) + 1;
            painter->drawText(QPointF(x + 2, height - 14), QString::number(barNum));
            for (int s = 1; s < 4; ++s)
            {
                double subX = x + s * secondsPerBeat * pixelsPerSecond;
                painter->setPen(QColor(255, 255, 255, 5));
                painter->drawLine(QPointF(subX, height - 4), QPointF(subX, height));
            }
        }
    }
}

void TimeRuler::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::RightButton)
    {
        event->ignore();
        return;
    }

    double x = event->pos().x();
    double t = timeFromX(x);

    double lx = xFromTime(loopStart);
    double rx = xFromTime(loopEnd);
    double threshold = 5.0;

    if (std::abs(x - lx) < threshold)
        dragMode = DragLoopStart;
    else if (std::abs(x - rx) < threshold)
        dragMode = DragLoopEnd;
    else
    {
        dragMode = Seek;
        auto& tm = engine.getTransportManager();
        tm.setCurrentSample(static_cast<int64_t>(t * tm.getSampleRate()));
        auto transportTree = engine.getProjectModel().getTransportTree();
        transportTree.setProperty(IDs::position, t, &engine.getProjectModel().getUndoManager());
        emit seekRequested(t);
    }
}

void TimeRuler::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    double x = event->pos().x();
    double t = timeFromX(std::max(0.0, x));

    if (dragMode == DragLoopStart)
    {
        loopStart = t;
        if (loopStart > loopEnd) loopStart = loopEnd;
        update();
        emit loopBoundsChanged(loopStart, loopEnd);
    }
    else if (dragMode == DragLoopEnd)
    {
        loopEnd = t;
        if (loopEnd < loopStart) loopEnd = loopStart;
        update();
        emit loopBoundsChanged(loopStart, loopEnd);
    }
}

void TimeRuler::mouseReleaseEvent(QGraphicsSceneMouseEvent*)
{
    if (dragMode != Seek)
    {
        auto transportTree = engine.getProjectModel().getTransportTree();
        transportTree.setProperty(IDs::loopStart, loopStart, &engine.getProjectModel().getUndoManager());
        transportTree.setProperty(IDs::loopEnd, loopEnd, &engine.getProjectModel().getUndoManager());
    }
    dragMode = None;
}
