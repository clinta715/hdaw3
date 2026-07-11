#pragma once
#include <QWidget>
#include "PianoRollModel.h"

class CCLaneWidget : public QWidget
{
    Q_OBJECT
public:
    CCLaneWidget(PianoRollModel& model, QWidget* parent = nullptr);

    void setPixelsPerBeat(double ppb) { pixelsPerBeat = ppb; update(); }
    void setScrollOffset(int x) { scrollX = x; update(); }
    void setControllerNumber(int cc) { controllerNumber = cc; update(); }
    int getControllerNumber() const { return controllerNumber; }
    void setPlayheadPosition(double seconds, double bpm) { playheadSeconds = seconds; playheadBpm = bpm; update(); }

    static constexpr int laneHeight = 60;

signals:
    void ccChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int pointIndexAtBeat(double beat) const;
    int pointIndexAtPos(const QPoint& pos) const;

    PianoRollModel& model;
    double pixelsPerBeat = 40.0;
    int scrollX = 0;
    int controllerNumber = 1;
    bool dragging = false;
    int dragPointIndex = -1;
    double playheadSeconds = -1.0;
    double playheadBpm = 120.0;
};
