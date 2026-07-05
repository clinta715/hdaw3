#pragma once
#include <QWidget>
#include <QTimer>
#include "../engine/LevelMeter.h"

namespace HDAW {

class VUMeter : public QWidget
{
    Q_OBJECT
public:
    explicit VUMeter(LevelMeter& meterToPoll, QWidget* parent = nullptr);
    ~VUMeter() override;

    void setMeter(LevelMeter* newMeter) { meter = newMeter; currentLeft = 0.0f; currentRight = 0.0f; }

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void updateLevels();

private:
    LevelMeter* meter;
    QTimer timer;
    float currentLeft = 0.0f;
    float currentRight = 0.0f;

    // Decay/Smoothing
    const float decayFactor = 0.85f; 
};

} // namespace HDAW
