#pragma once
#include <QWidget>
#include <QTimer>
#include <QComboBox>
#include "../engine/AutomationManager.h"
#include "../engine/AudioEngine.h"

class AutomationLaneWidget : public QWidget
{
    Q_OBJECT
public:
    AutomationLaneWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~AutomationLaneWidget() override;

    void loadTrack(int trackIndex);
    void clear();
    int currentTrackIndex() const { return currentTrack; }

signals:
    void automationChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void onParamChanged(int index);

private:
    int pointAtPos(const QPoint& pos) const;
    double timeFromX(int x) const;
    double valueFromY(int y) const;
    int xFromTime(double t) const;
    int yFromValue(double v) const;
    void refreshParamCombo();
    juce::ValueTree currentAutoTree() const;

    AudioEngine& engine;
    int currentTrack = -1;
    int currentParamIndex = 0;
    QComboBox* paramCombo;

    double pixelsPerSecond = 40.0;
    int scrollX = 0;
    int dragPoint = -1;
    int hoverPoint = -1;

    static constexpr int laneHeight = 120;
    static constexpr int pointRadius = 5;
};
