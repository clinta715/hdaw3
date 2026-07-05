#include "VUMeter.h"
#include "Theme.h"
#include <QPainter>
#include <cmath>

namespace HDAW {

VUMeter::VUMeter(LevelMeter& meterToPoll, QWidget* parent)
    : QWidget(parent), meter(&meterToPoll)
{
    setMinimumWidth(20);
    setMinimumHeight(100);

    connect(&timer, &QTimer::timeout, this, &VUMeter::updateLevels);
    timer.start(16); // ~60 FPS
}

VUMeter::~VUMeter()
{
}

void VUMeter::updateLevels()
{
    if (meter == nullptr) return;

    float targetLeft = meter->getLeftLevel();
    float targetRight = meter->getRightLevel();

    // Fast rise, slow decay
    if (targetLeft > currentLeft) currentLeft = targetLeft;
    else currentLeft *= decayFactor;

    if (targetRight > currentRight) currentRight = targetRight;
    else currentRight *= decayFactor;

    update(); // Trigger repaint
}

void VUMeter::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    
    painter.fillRect(rect(), ThemeColors::bgWindow());

    int w = width();
    int h = height();
    int spacing = 2;
    int barW = (w - spacing * 3) / 2;

    auto drawBar = [&](int x, float level) {
        // Logarithmic scale roughly
        float db = 20.0f * std::log10((std::max)(level, 0.0001f));
        float normalized = (db + 60.0f) / 60.0f; // -60dB to 0dB range
        normalized = (std::max)(0.0f, (std::min)(1.0f, normalized));

        int barH = static_cast<int>(normalized * h);
        
        QRect barRect(x, h - barH, barW, barH);
        
        QColor color;
        if (db > -3.0f) color = ThemeColors::vuRed();
        else if (db > -12.0f) color = ThemeColors::vuYellow();
        else color = ThemeColors::vuGreen();

        painter.fillRect(barRect, color);
    };

    drawBar(spacing, currentLeft);
    drawBar(spacing * 2 + barW, currentRight);
}

} // namespace HDAW
