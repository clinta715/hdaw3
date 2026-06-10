#pragma once
#include <QWidget>
#include "PianoRollModel.h"

class VelocityLaneWidget : public QWidget
{
    Q_OBJECT
public:
    VelocityLaneWidget(PianoRollModel& model, QWidget* parent = nullptr);

    void setPixelsPerBeat(double ppb) { pixelsPerBeat = ppb; update(); }
    void setScrollOffset(int x) { scrollX = x; update(); }

    static constexpr int laneHeight = 60;

signals:
    void velocityChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int noteIndexAtBeat(double beat) const;
    void setVelocityAtPos(const QPoint& pos, float vel);

    PianoRollModel& model;
    double pixelsPerBeat = 40.0;
    int scrollX = 0;
    bool dragging = false;
};
