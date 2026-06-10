#include "VelocityLaneWidget.h"
#include "Theme.h"
#include <QPainter>
#include <QMouseEvent>
#include <cmath>

VelocityLaneWidget::VelocityLaneWidget(PianoRollModel& m, QWidget* parent)
    : QWidget(parent), model(m)
{
    setFixedHeight(laneHeight);
    setMouseTracking(true);
}

int VelocityLaneWidget::noteIndexAtBeat(double beat) const
{
    for (int i = 0; i < model.getNumNotes(); ++i)
    {
        auto note = model.getNote(i);
        double start = note.getProperty(IDs::startBeat);
        double end = start + static_cast<double>(note.getProperty(IDs::durationBeats));
        if (beat >= start && beat < end)
            return i;
    }
    return -1;
}

void VelocityLaneWidget::setVelocityAtPos(const QPoint& pos, float vel)
{
    double beat = (pos.x() + scrollX) / pixelsPerBeat;
    int idx = noteIndexAtBeat(beat);
    if (idx >= 0)
    {
        auto note = model.getNote(idx);
        note.setProperty(IDs::velocity, std::max(0.0f, std::min(127.0f, vel)), model.getUndoManager());
        emit velocityChanged();
        update();
    }
}

void VelocityLaneWidget::paintEvent(QPaintEvent*)
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

    // Grid lines (matching beat lines from note grid)
    double totalBeats = static_cast<double>(w + scrollX) / pixelsPerBeat + 1;
    for (int b = 0; b <= static_cast<int>(totalBeats); ++b)
    {
        int x = static_cast<int>(b * pixelsPerBeat - scrollX);
        if (x < -5 || x > w + 5) continue;
        painter.setPen(QPen(QColor(255, 255, 255, 6), 1));
        painter.drawLine(x, 0, x, h);
    }

    // Labels
    painter.setPen(ThemeColors::textSecondary());
    QFont f = painter.font();
    f.setPointSize(6);
    painter.setFont(f);
    painter.drawText(2, 2, 30, 12, Qt::AlignLeft | Qt::AlignTop, "Vel");
    painter.drawText(2, h - 12, 30, 10, Qt::AlignLeft | Qt::AlignTop, "0");
    painter.drawText(2, 2, 30, h, Qt::AlignLeft | Qt::AlignVCenter, "127");

    // Velocity bars
    for (int i = 0; i < model.getNumNotes(); ++i)
    {
        auto note = model.getNote(i);
        double startBeat = note.getProperty(IDs::startBeat);
        double durBeats = note.getProperty(IDs::durationBeats);
        float vel = note.getProperty(IDs::velocity);

        double x = startBeat * pixelsPerBeat - scrollX;
        double barW = std::max(durBeats * pixelsPerBeat, 2.0);
        double barH = (vel / 127.0) * (h - 8);
        if (barH < 1.0) barH = 1.0;

        if (x < -barW || x > w) continue;

        QColor barColor(ThemeColors::accent().red(), ThemeColors::accent().green(), ThemeColors::accent().blue(), static_cast<int>(vel / 127.0f * 200.0f + 55));

        painter.setPen(Qt::NoPen);
        painter.setBrush(barColor);
        painter.drawRect(QRectF(x, h - 4 - barH, barW, barH));

        // Velocity value label for selected notes
        if (model.getSelectedNotes().contains(note))
        {
            painter.setPen(ThemeColors::warning());
            f.setPointSize(6);
            painter.setFont(f);
            painter.drawText(QPointF(x, h - 6 - barH - 2),
                             QString::number(static_cast<int>(vel)));
        }
    }
}

void VelocityLaneWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        dragging = true;
        double beat = (event->pos().x() + scrollX) / pixelsPerBeat;
        int idx = noteIndexAtBeat(beat);
        if (idx >= 0)
        {
            float vel = 127.0f * (1.0f - static_cast<float>(event->pos().y()) / laneHeight);
            vel = std::max(1.0f, std::min(127.0f, vel));
            auto note = model.getNote(idx);
            note.setProperty(IDs::velocity, vel, model.getUndoManager());
            model.deselectAll();
            model.selectNote(note);
            emit velocityChanged();
            update();
        }
    }
}

void VelocityLaneWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging)
    {
        double beat = (event->pos().x() + scrollX) / pixelsPerBeat;
        int idx = noteIndexAtBeat(beat);
        if (idx >= 0)
        {
            float vel = 127.0f * (1.0f - static_cast<float>(event->pos().y()) / laneHeight);
            vel = std::max(1.0f, std::min(127.0f, vel));
            auto note = model.getNote(idx);
            note.setProperty(IDs::velocity, vel, model.getUndoManager());
            model.deselectAll();
            model.selectNote(note);
            emit velocityChanged();
            update();
        }
    }
}

void VelocityLaneWidget::mouseReleaseEvent(QMouseEvent*)
{
    dragging = false;
}
