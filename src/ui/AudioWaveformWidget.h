#pragma once
#include <QWidget>
#include <QScrollBar>
#include <QPixmap>
#include <QContextMenuEvent>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>
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
    double getSelectionStart() const { return selStart; }
    double getSelectionEnd() const { return selEnd; }
    bool hasSelection() const { return selStart >= 0.0 && selEnd > selStart; }
    void clearSelection() { selStart = -1.0; selEnd = -1.0; update(); }
    void selectAll() { selStart = 0.0; selEnd = currentClip.isValid() ? static_cast<double>(currentClip.getProperty(IDs::duration, 0.0)) : 0.0; update(); }
    void zoomIn() { setPixelsPerSecond(pixelsPerSecond * 1.3); }
    void zoomOut() { setPixelsPerSecond(pixelsPerSecond / 1.3); }

    // Late-bound UndoManager for fade-handle drags. The widget is constructed
    // before the engine finishes initializing, so the UM is wired up by the
    // owning editor after construction. Without this, fade drags wrote the
    // ValueTree with nullptr (not undoable), inconsistent with the spinbox
    // path which goes through ProjectCommands. Each drag begins a new
    // transaction on press so the whole drag is one undo step.
    void setUndoManager(juce::UndoManager* um) { undoManager = um; }

signals:
    void fadeInChanged(double seconds);
    void fadeOutChanged(double seconds);
    // NOTE: parameter names say "beat" for historical reasons but the values
    // are SECONDS within the clip's local timeline (0 = clip start). See H7.
    void regionSelected(double startTime, double endTime);
    void copyRequested();
    void cutRequested();
    void pasteRequested();
    void selectAllRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    enum class DragMode { None, FadeIn, FadeOut, SelectRegion };
    double timeAtPos(int x) const;
    bool isOverFadeIn(const QPoint& pos) const;
    bool isOverFadeOut(const QPoint& pos) const;
    QRectF fadeInRect() const;
    QRectF fadeOutRect() const;
    void invalidateWaveformCache();

    HDAW::ProjectPool& projectPool;
    juce::UndoManager* undoManager = nullptr;
    juce::ValueTree currentClip;
    std::unique_ptr<juce::AudioThumbnail> thumbnail;
    bool destroyed_ = false;

    struct ThumbnailListener : public juce::ChangeListener
    {
        AudioWaveformWidget* widget;
        std::atomic<bool> alive{ true };
        void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    };
    std::shared_ptr<ThumbnailListener> thumbListener;

    double pixelsPerSecond = 20.0;
    int scrollX = 0;
    double playheadSeconds = -1.0;

    DragMode dragMode = DragMode::None;
    QPoint dragStart;
    double dragStartFade = 0.0;

    double selStart = -1.0;
    double selEnd = -1.0;

    QPixmap cachedWaveform;
    int cacheWidth = 0;
    int cacheHeight = 0;
    double cacheStartTime = -1.0;
    double cacheEndTime = -1.0;

    static constexpr double fadeHandleWidth = 20.0;
};
