#include "PianoKeysWidget.h"
#include "Theme.h"
#include <QPainter>
#include <cmath>

PianoKeysWidget::PianoKeysWidget(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(keyWidth);
}

bool PianoKeysWidget::isBlackKey(int note)
{
    int inOctave = note % 12;
    return inOctave == 1 || inOctave == 3 || inOctave == 6 || inOctave == 8 || inOctave == 10;
}

int PianoKeysWidget::noteNumberAtPos(int y) const
{
    int idx = (y + scrollOffset) / static_cast<int>(keyHeight);
    return maxNote - 1 - idx;
}

QRect PianoKeysWidget::keyRect(int noteNumber) const
{
    int idx = maxNote - 1 - noteNumber;
    int y = static_cast<int>(idx * keyHeight) - scrollOffset;
    return QRect(0, y, keyWidth, static_cast<int>(std::ceil(keyHeight)));
}

void PianoKeysWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int h = height();

    for (int n = minNote; n < maxNote; ++n)
    {
        QRect r = keyRect(n);
        if (r.bottom() < 0 || r.top() > h) continue;

        bool black = isBlackKey(n);
        int octave = n / 12 - 1;
        int inOctave = n % 12;

        if (black)
        {
            // Black keys drawn later on top
            continue;
        }

        // White key
        painter.setPen(QPen(ThemeColors::border(), 1));
        painter.setBrush((octave % 2 == 0) ? QColor(220, 220, 220) : QColor(200, 200, 200));
        painter.drawRect(r.adjusted(0, 0, -1, 0));

        // C key label
        if (inOctave == 0)
        {
            painter.setPen(QColor(80, 80, 80));
            QFont f = painter.font();
            f.setPointSize(7);
            painter.setFont(f);
            painter.drawText(r.adjusted(3, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                             QString("C%1").arg(octave));
        }
    }

    // Draw black keys on top
    for (int n = minNote; n < maxNote; ++n)
    {
        if (!isBlackKey(n)) continue;

        QRect r = keyRect(n);
        if (r.bottom() < 0 || r.top() > h) continue;

        painter.setPen(QPen(QColor(20, 20, 20), 1));
        painter.setBrush(QColor(40, 40, 40));
        painter.drawRect(r.adjusted(0, 0, 0, -1));
        painter.setPen(QColor(20, 20, 20));
        painter.drawLine(r.topLeft(), r.topRight());
    }

    // Border
    painter.setPen(QPen(ThemeColors::border(), 1));
    painter.drawLine(width() - 1, 0, width() - 1, h);
}
