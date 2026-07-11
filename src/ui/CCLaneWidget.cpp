#include "CCLaneWidget.h"
#include "Theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <cmath>

CCLaneWidget::CCLaneWidget(PianoRollModel& m, QWidget* parent)
    : QWidget(parent), model(m)
{
    setFixedHeight(laneHeight);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
}

int CCLaneWidget::pointIndexAtBeat(double beat) const
{
    int count = model.getCcPointCount(controllerNumber);
    for (int i = 0; i < count; ++i)
    {
        auto pt = model.getCcPoint(controllerNumber, i);
        double ptBeat = pt.getProperty(IDs::beat);
        if (std::abs(ptBeat - beat) < 0.05)
            return i;
    }
    return -1;
}

int CCLaneWidget::pointIndexAtPos(const QPoint& pos) const
{
    double beat = (pos.x() + scrollX) / pixelsPerBeat;
    return pointIndexAtBeat(beat);
}

void CCLaneWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Background
    painter.fillRect(rect(), ThemeColors::bgWindow());

    // Top border
    painter.setPen(QPen(ThemeColors::border(), 1));
    painter.drawLine(0, 0, w, 0);

    // Grid lines
    double totalBeats = static_cast<double>(w + scrollX) / pixelsPerBeat + 1;
    for (int b = 0; b <= static_cast<int>(totalBeats); ++b)
    {
        int x = static_cast<int>(b * pixelsPerBeat - scrollX);
        if (x < -5 || x > w + 5) continue;
        painter.setPen(QPen(ThemeColors::gridLineBeat(), 1));
        painter.drawLine(x, 0, x, h);
    }

    // Label — keep the three labels in disjoint corners so they don't overlap:
    // "CCn" top-left, "127" top-right, "0" bottom-left.
    painter.setPen(ThemeColors::textSecondary());
    QFont f = painter.font();
    f.setPointSize(6);
    painter.setFont(f);
    painter.drawText(2, 2, 30, 12, Qt::AlignLeft | Qt::AlignTop,
                     QString("CC%1").arg(controllerNumber));
    painter.drawText(w - 32, 2, 30, 12, Qt::AlignRight | Qt::AlignTop, "127");
    painter.drawText(2, h - 12, 30, 10, Qt::AlignLeft | Qt::AlignTop, "0");

    // Draw CC points as vertical bars
    int count = model.getCcPointCount(controllerNumber);
    for (int i = 0; i < count; ++i)
    {
        auto pt = model.getCcPoint(controllerNumber, i);
        double beat = pt.getProperty(IDs::beat);
        int val = pt.getProperty(IDs::value);

        double x = beat * pixelsPerBeat - scrollX + 0.5;
        if (x < 0 || x > w) continue;

        double barH = (val / 127.0) * (h - 8);
        if (barH < 1.0) barH = 1.0;

        QColor barColor(ThemeColors::accent().red(), ThemeColors::accent().green(),
                        ThemeColors::accent().blue(), static_cast<int>(val / 127.0f * 200.0f + 55));

        painter.setPen(Qt::NoPen);
        painter.setBrush(barColor);
        painter.drawRect(QRectF(x - 1.5, h - 4 - barH, 3.0, barH));
    }

    // Playhead
    if (playheadSeconds >= 0)
    {
        double beatPos = playheadSeconds * playheadBpm / 60.0;
        int phx = static_cast<int>(beatPos * pixelsPerBeat - scrollX);
        if (phx >= 0 && phx <= w)
        {
            painter.setPen(QPen(ThemeColors::accentBright(), 1));
            painter.drawLine(phx, 0, phx, h);
        }
    }
}

void CCLaneWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        double beat = (event->pos().x() + scrollX) / pixelsPerBeat;
        int idx = pointIndexAtBeat(beat);
        if (idx >= 0)
        {
            dragging = true;
            grabMouse();
            dragPointIndex = idx;
            auto pt = model.getCcPoint(controllerNumber, idx);
            int val = 127 - static_cast<int>(127.0 * event->pos().y() / laneHeight);
            val = (std::max)(0, (std::min)(127, val));
            pt.setProperty(IDs::value, val, model.getUndoManager());
            emit ccChanged();
            update();
        }
        else
        {
            int val = 127 - static_cast<int>(127.0 * event->pos().y() / laneHeight);
            val = (std::max)(0, (std::min)(127, val));
            model.addCcPoint(controllerNumber, beat, val);
            dragging = true;
            grabMouse();
            dragPointIndex = pointIndexAtBeat(beat);
            emit ccChanged();
            update();
        }
    }
}

void CCLaneWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging && dragPointIndex >= 0)
    {
        int count = model.getCcPointCount(controllerNumber);
        if (dragPointIndex >= count)
        {
            dragging = false;
            return;
        }

        auto pt = model.getCcPoint(controllerNumber, dragPointIndex);
        double newBeat = (event->pos().x() + scrollX) / pixelsPerBeat;
        newBeat = (std::max)(0.0, newBeat);
        int newVal = 127 - static_cast<int>(127.0 * event->pos().y() / laneHeight);
        newVal = (std::max)(0, (std::min)(127, newVal));
        pt.setProperty(IDs::beat, newBeat, model.getUndoManager());
        pt.setProperty(IDs::value, newVal, model.getUndoManager());
        emit ccChanged();
        update();
    }
}

void CCLaneWidget::mouseReleaseEvent(QMouseEvent*)
{
    if (dragging)
        releaseMouse();
    dragging = false;
    dragPointIndex = -1;
}

void CCLaneWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
}

void CCLaneWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
}
