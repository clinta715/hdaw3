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
    // Assign fresh unique ids to externally-supplied points so they can be
    // tracked across the sort below and through subsequent drags. External
    // callers (e.g. loading from the ValueTree) don't know about ids.
    for (auto& p : points)
        p.id = nextPointId++;
    std::sort(points.begin(), points.end(),
              [](const Point& a, const Point& b) { return a.time < b.time; });
    update();
}

QVector<GainEnvelopeEditor::Point> GainEnvelopeEditor::getPoints() const
{
    return points;
}

int GainEnvelopeEditor::indexOfId(long long id) const
{
    for (int i = 0; i < points.size(); ++i)
        if (points[i].id == id)
            return i;
    return -1;
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
    // Measure distance from `pos` to each point, not from the origin.
    // (QPoint(x,y).manhattanLength() is |x|+|y| — distance from (0,0) — which
    // is always >= 10 and therefore never matched, making every point
    // undraggable. Compute (point - pos) first.)
    for (int i = 0; i < points.size(); ++i)
    {
        int x = timeToX(points[i].time);
        int y = gainToY(points[i].gain);
        if ((QPoint(x, y) - pos).manhattanLength() < 10)
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
        double t = std::clamp(xToTime(e->x()), 0.0, duration);
        double g = std::clamp(yToGain(e->y()), 0.0, 1.0);
        Point p{ t, g, nextPointId++ };
        points.append(p);
        std::sort(points.begin(), points.end(),
                  [](const Point& a, const Point& b) { return a.time < b.time; });
        dragIndex = indexOfId(p.id);
        adding = true;
        emit pointAdded(t, g);
    }
    emit dragStarted();
    update();
}

void GainEnvelopeEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (dragIndex < 0)
        return;

    double t = std::clamp(xToTime(e->x()), 0.0, duration);
    double g = std::clamp(yToGain(e->y()), 0.0, 1.0);
    // Preserve the dragged point's id across the in-place update + sort so
    // we can find it again by identity (indexOf({t,g}) would match a
    // duplicate neighbor and rebind the drag to the wrong point).
    long long draggedId = points[dragIndex].id;
    points[dragIndex] = { t, g, draggedId };
    std::sort(points.begin(), points.end(),
              [](const Point& a, const Point& b) { return a.time < b.time; });
    dragIndex = indexOfId(draggedId);
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
        emit dragFinished();
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