#pragma once
#include <QWidget>
#include <QTimer>
#include <QLineEdit>
#include <QFont>
#include "../engine/AudioEngine.h"
#include <vector>

class TrackHeaderWidget : public QWidget
{
    Q_OBJECT
public:
    TrackHeaderWidget(AudioEngine& engine, QWidget* parent = nullptr);
    ~TrackHeaderWidget() override;

    void rebuild();
    void setScrollOffset(double yOffset);
    void setTrackHeight(int index, double height);
    double getTrackHeight(int index) const;
    int trackCount() const { return static_cast<int>(tracks.size()); }

signals:
    void trackSelectionChanged(int trackIndex);
    void automationToggled(int trackIndex);
    void addTrackRequested();
    void addTrackWithFX(const juce::String& fxType);
    void addTrackWithPlugin(const juce::String& pluginID, const juce::String& pluginFormat);
    void fxSlotAdded(int trackIndex);

public slots:
    void setSelectedTrack(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private slots:
    void updateVU();

private:
    struct TrackHeader {
        int trackIndex;
        QRect bounds;
        QRect muteRect;
        QRect soloRect;
        QRect armRect;
        QRect autoRect;
        QRect volRect;
        QRect panRect;
        QRect vuRect;
        QRect nameRect;
        float volValue = 1.0f;
        float panValue = 0.0f;
        bool isMuted = false;
        bool isSoloed = false;
        bool draggingVol = false;
        bool draggingPan = false;
        float vuLeft = 0.0f;
        float vuRight = 0.0f;
        QLineEdit* nameEdit = nullptr;
    };

    int hitTest(const QPoint& pos, int& outTrackIndex) const;
    TrackHeader& headerFor(int trackIndex);
    void commitVolume(int trackIndex, float vol);
    void commitPan(int trackIndex, float pan);
    void addFXToTrack(int trackIndex, const juce::String& type);
    void addPluginToTrack(int trackIndex, const juce::String& pluginID, const juce::String& pluginFormat);
    void layoutRects();

    AudioEngine& engine;
    QTimer vuTimer;
    std::vector<TrackHeader> tracks;
    int dragTrack = -1;
    QPoint dragStart;
    float dragStartValue = 0.0f;
    int resizeTrack = -1;
    int selectedTrack = -1;

    double scrollOffset = 0.0;

    QFont nameFont;
    QFont toggleFont;
    QFont smallFont;

    static constexpr double headerWidth = 120.0;
    static constexpr double defaultTrackHeight = 80.0;
    static constexpr double vuUpdateInterval = 16;
    static constexpr double rulerHeight = 30.0;
};
