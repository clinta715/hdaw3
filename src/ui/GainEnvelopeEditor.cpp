#include "GainEnvelopeEditor.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QColor>
#include <algorithm>

GainEnvelopeEditor::GainEnvelopeEditor(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(80);
}

void GainEnvelopeEditor::setPoints(const QVector<Point>& pts)
{
    points = pts;
    std::sort(points.begin(), points.end(),
              [](const Point& a, const Point& b) { return a.time < b.time; });
    update();
}

QVector<GainEnvelopeEditor::Point> GainEnvelopeEditor::getPoints() const
{
    return points;
}

void GainEnvelopeEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), QColor(30, 30, 35));

    // Grid lines
    p.setPen(QColor(60, 60, 70));
    for (int i = 1; i < 4; ++i)
        p.drawLine(0, height() * i / 4, width(), height() * i / 4);

    // Envelope curve
    if (points.size() >= 2)
    {
        QPainterPath path;
        path.moveTo(timeToX(points[0].time), gainToY(points[0].gain));
        for (size_t i = 1; i < points.size(); ++i)
            path.lineTo(timeToX(points[i].time), gainToY(points[i].gain));
        p.setPen(QPen(QColor(0x06, 0xb6, 0xd4), 2));
        p.drawPath(path);
    }

    // Points
    p.setBrush(QColor(0x06, 0xb6, 0xd4));
    for (const auto& pt : points)
    {
        int x = timeToX(pt.time);
        int y = gainToY(pt.gain);
        p.drawEllipse(QPoint(x, y), 5, 5);
    }

    // Drag preview
    if (dragIndex >= 0 && dragIndex < points.size())
    {
        int x = timeToX(points[dragIndex].time);
        int y = gainToY(points[dragIndex].gain);
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(QPoint(x, y), 8, 8);
    }
}

int GainEnvelopeEditor::hitTest(const QPoint& pos) const
{
    for (int i = 0; i < points.size(); ++i)
    {
        int x = timeToX(points[i].time);
        int y = gainToY(points[i].gain);
        if (QPoint(x, y).manhattanLength() < 10)
            return i;
    }
    return -1;
}

void GainEnvelopeEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;

    int idx = hitTest(e->pos());
    if (idx >= 0)
    {
        dragIndex = idx;
    }
    else
    {
        // Add new point
        double t = std::clamp(xToTime(e->x()), 0.0, duration);
        double g = std::clamp(yToGain(e->y()), 0.0, 1.0);
        points.append({t, g});
        std::sort(points.begin(), points.end(),
                  [](const Point& a, const Point& b) { return a.time < b.time; });
        dragIndex = points.indexOf({t, g});
        adding = true;
        emit pointAdded(t, g);
    }
    update();
}

void GainEnvelopeEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (dragIndex < 0)
        return;

    double t = std::clamp(xToTime(e->x()), 0.0, duration);
    double g = std::clamp(yToGain(e->y()), 0.0, 1.0);
    points[dragIndex] = {t, g};
    std::sort(points.begin(), points.end(),
              [](const Point& a, const Point& b) { return a.time < b.time; });
    dragIndex = points.indexOf({t, g});
    emit pointMoved(dragIndex, t, g);
    emit pointsChanged(points);
    update();
}

void GainEnvelopeEditor::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && dragIndex >= 0)
    {
        if (adding)
            adding = false;
        dragIndex = -1;
    }
    else if (e->button() == Qt::RightButton)
    {
        int idx = hitTest(e->pos());
        if (idx >= 0 && points.size() > 2)
        {
            points.remove(idx);
            emit pointRemoved(idx);
            emit pointsChanged(points);
            update();
        }
    }
}