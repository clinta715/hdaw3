#pragma once
#include <QWidget>
#include <QTimer>
#include <QComboBox>
#include <QPushButton>
#include "../engine/AutomationManager.h"
#include "../common/ProjectCommands.h"
#include "../common/AudioGraphCommands.h"
#include "../common/ReadModel.h"

class AudioEngine;

class AutomationLaneWidget : public QWidget
{
    Q_OBJECT
public:
    AutomationLaneWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~AutomationLaneWidget() override;

    void loadTrack(int trackIndex);
    void clear();
    int currentTrackIndex() const { return currentTrack; }
    void setPlayheadPosition(double seconds) { playheadSeconds = seconds; update(); }
    void setPixelsPerSecond(double pps) { pixelsPerSecond = pps; update(); }
    void setScrollX(int sx) { scrollX = sx; update(); }

signals:
    void automationChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;

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

    void addAutomationLane(const QString& name, int paramID);
    void removeCurrentLane();
    void showAddLaneMenu();

    AudioEngine& engine;
    ProjectCommands* projectCmds = nullptr;
    AudioGraphCommands* audioGraphCmds = nullptr;
    ReadModel* readModel = nullptr;
    int currentTrack = -1;
    int currentParamIndex = 0;
    QComboBox* paramCombo;
    QPushButton* addLaneBtn;
    QPushButton* removeLaneBtn;

    double pixelsPerSecond = 40.0;
    int scrollX = 0;
    int dragPoint = -1;
    int hoverPoint = -1;
    double playheadSeconds = -1.0;

    static constexpr int laneHeight = 120;
    static constexpr int pointRadius = 5;
};
