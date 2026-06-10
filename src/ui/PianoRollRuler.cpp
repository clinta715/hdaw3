#include "PianoRollRuler.h"
#include "Theme.h"
#include <QPainter>

PianoRollRuler::PianoRollRuler(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(rulerHeight);
}

void PianoRollRuler::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    int w = width();
    int h = height();

    painter.fillRect(rect(), ThemeColors::bgWidget());

    // Bottom border
    painter.setPen(QPen(ThemeColors::border(), 1));
    painter.drawLine(0, h - 1, w, h - 1);

    // Beat markers
    double startBeat = std::max(0.0, static_cast<double>(scrollOffset) / pixelsPerBeat);
    double endBeat = startBeat + static_cast<double>(w) / pixelsPerBeat + 1;

    painter.setPen(ThemeColors::textSecondary());
    QFont f = painter.font();
    f.setPointSize(7);
    painter.setFont(f);

    for (int beat = static_cast<int>(startBeat); beat <= static_cast<int>(endBeat); ++beat)
    {
        int x = xFromBeat(static_cast<double>(beat));
        if (x < -10 || x > w + 10) continue;

        bool isBar = (beat % 4 == 0);

        painter.setPen(isBar ? QColor(255, 255, 255, 20) : QColor(255, 255, 255, 10));
        painter.drawLine(x, isBar ? h - 14 : h - 6, x, h);

        if (isBar)
        {
            int barNum = beat / 4 + 1;
            painter.setPen(ThemeColors::textPrimary());
            painter.drawText(x + 2, 4, w - x, h - 16, Qt::AlignLeft | Qt::AlignBottom,
                             QString::number(barNum));
        }
    }
}
