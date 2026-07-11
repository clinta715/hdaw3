#pragma once
#include <QWidget>
#include <QScrollBar>
#include <QPixmap>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../engine/ProjectPool.h"
#include "../model/ProjectModel.h"

class AudioWaveformWidget : public QWidget
{
    Q_OBJECT
public:
    AudioWaveformWidget(HDAW::ProjectPool& pool, QWidget* parent = nullptr);
    ~AudioWaveformWidget() override;

    void setClip(juce::ValueTree clip);
    void reloadThumbnail();
    void setPlayheadPosition(double seconds) { playheadSeconds = seconds; update(); }
    void setPixelsPerSecond(double pps);
    double getPixelsPerSecond() const { return pixelsPerSecond; }
    void setScrollX(int sx) { scrollX = sx; update(); }
    int getScrollX() const { return scrollX; }
    void zoomIn() { setPixelsPerSecond(pixelsPerSecond * 1.3); }
    void zoomOut() { setPixelsPerSecond(pixelsPerSecond / 1.3); }

signals:
    void fadeInChanged(double seconds);
    void fadeOutChanged(double seconds);
    void regionSelected(double startBeat, double endBeat);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class DragMode { None, FadeIn, FadeOut, SelectRegion };
    double beatAtPos(int x) const;
    bool isOverFadeIn(const QPoint& pos) const;
    bool isOverFadeOut(const QPoint& pos) const;
    QRectF fadeInRect() const;
    QRectF fadeOutRect() const;
    void invalidateWaveformCache();

    HDAW::ProjectPool& projectPool;
    juce::ValueTree currentClip;
    std::unique_ptr<juce::AudioThumbnail> thumbnail;

    double pixelsPerSecond = 20.0;
    int scrollX = 0;
    double playheadSeconds = -1.0;

    DragMode dragMode = DragMode::None;
    QPoint dragStart;
    double dragStartFade = 0.0;
    double dragStartBeat = 0.0;

    double selStart = -1.0;
    double selEnd = -1.0;

    QPixmap cachedWaveform;
    int cacheWidth = 0;
    int cacheHeight = 0;
    double cacheStartTime = -1.0;
    double cacheEndTime = -1.0;

    static constexpr double fadeHandleWidth = 20.0;
};
