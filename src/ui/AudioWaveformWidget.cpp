#include "AudioWaveformWidget.h"
#include "Theme.h"
#include "DebugLog.h"
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QWheelEvent>
#include <QMenu>
#include <QAction>
#include <cmath>
#include <cstring>

AudioWaveformWidget::AudioWaveformWidget(HDAW::ProjectPool& pool, QWidget* parent)
    : QWidget(parent), projectPool(pool)
{
    setMouseTracking(true);
    setMinimumHeight(80);
    // Note: no global qApp event filter. Fade/region drags use grabMouse() on
    // press so all mouse events route here until release — this is the Qt
    // idiom for "capture the drag" and means leaving the widget no longer
    // abandons an in-progress drag (the previous leaveEvent cancel left the
    // selection/fade stuck if the cursor crossed the widget edge mid-drag).
}

void AudioWaveformWidget::ThumbnailListener::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (!alive.load(std::memory_order_acquire))
        return;
    if (source == widget->thumbnail.get())
    {
        widget->invalidateWaveformCache();
        widget->update();
    }
}

AudioWaveformWidget::~AudioWaveformWidget()
{
    destroyed_ = true;
    if (thumbListener)
        thumbListener->alive.store(false, std::memory_order_release);
    if (thumbnail && thumbListener)
        thumbnail->removeChangeListener(thumbListener.get());
}

void AudioWaveformWidget::setClip(juce::ValueTree clip)
{
    currentClip = clip;
    playheadSeconds = -1.0;
    scrollX = 0;
    selStart = -1.0;
    selEnd = -1.0;
    invalidateWaveformCache();
    reloadThumbnail();
    update();
}

void AudioWaveformWidget::reloadThumbnail()
{
    auto sourceFile = currentClip.isValid()
        ? currentClip.getProperty(IDs::sourceFile).toString()
        : juce::String{};
    if (sourceFile.isEmpty())
    {
        if (thumbnail && thumbListener)
            thumbnail->removeChangeListener(thumbListener.get());
        thumbListener.reset();
        thumbnail.reset();
        invalidateWaveformCache();
        return;
    }

    // Create the new thumbnail before tearing down the old one, so paintEvent
    // never sees a null thumbnail between destroy and re-create — it would
    // paint an invisible 4% white rect in that gap.
    auto newThumb = projectPool.createThumbnail(1000, projectPool.getThumbnailCache());
    if (newThumb == nullptr)
    {
        if (thumbnail && thumbListener)
            thumbnail->removeChangeListener(thumbListener.get());
        thumbListener.reset();
        thumbnail.reset();
        invalidateWaveformCache();
        return;
    }

    auto newListener = std::make_shared<ThumbnailListener>();
    newListener->widget = this;
    newThumb->addChangeListener(newListener.get());

    // Now swap: tear down the old, install the new, start loading.
    if (thumbnail && thumbListener)
        thumbnail->removeChangeListener(thumbListener.get());
    thumbListener = std::move(newListener);
    thumbnail = std::move(newThumb);
    invalidateWaveformCache();

    auto file = juce::File(sourceFile);
    if (file.existsAsFile())
        thumbnail->setSource(new juce::FileInputSource(file));
}

void AudioWaveformWidget::setPixelsPerSecond(double pps)
{
    pixelsPerSecond = (std::max)(4.0, (std::min)(200.0, pps));
    invalidateWaveformCache();
    update();
}

void AudioWaveformWidget::invalidateWaveformCache()
{
    cacheWidth = 0;
    cacheHeight = 0;
    cacheStartTime = -1.0;
    cacheEndTime = -1.0;
    cachedWaveform = QPixmap();
}

double AudioWaveformWidget::timeAtPos(int x) const
{
    // Returns seconds within the clip's local timeline (0 = clip start),
    // NOT beats despite the historical "beat" naming elsewhere. The selection
    // is in clip-local seconds and consumed as such by the region commands.
    return (static_cast<double>(x) + scrollX) / pixelsPerSecond;
}

bool AudioWaveformWidget::isOverFadeIn(const QPoint& pos) const
{
    return fadeInRect().contains(pos);
}

bool AudioWaveformWidget::isOverFadeOut(const QPoint& pos) const
{
    return fadeOutRect().contains(pos);
}

QRectF AudioWaveformWidget::fadeInRect() const
{
    double fade = currentClip.getProperty(IDs::fadeIn);
    double fadePx = fade * pixelsPerSecond;
    return QRectF(0, 0, (std::max)(fadePx, fadeHandleWidth), height());
}

QRectF AudioWaveformWidget::fadeOutRect() const
{
    double dur = currentClip.getProperty(IDs::duration);
    double fade = currentClip.getProperty(IDs::fadeOut);
    double durPx = dur * pixelsPerSecond;
    double fadePx = fade * pixelsPerSecond;
    return QRectF(durPx - (std::max)(fadePx, fadeHandleWidth), 0,
                  (std::max)(fadePx, fadeHandleWidth), height());
}

void AudioWaveformWidget::paintEvent(QPaintEvent*)
{
    if (destroyed_ || !updatesEnabled()) return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Background
    painter.fillRect(rect(), ThemeColors::bgWidget());

    if (!currentClip.isValid())
    {
        painter.fillRect(rect(), ThemeColors::bgWidget());
        painter.setPen(ThemeColors::placeholderText());
        QFont f = painter.font();
        f.setPointSize(11);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter, "No audio clip selected");
        return;
    }

    double offset = currentClip.getProperty(IDs::offset);
    double duration = currentClip.getProperty(IDs::duration);

    // Draw waveform
    if (thumbnail && thumbnail->getTotalLength() > 0)
    {
        double thumbTotal = thumbnail->getTotalLength();
        double startTime = (std::min)(offset, thumbTotal);
        double endTime = (std::min)(offset + duration, thumbTotal);

        if (endTime > startTime && w > 0 && h > 0)
        {
            if (w == cacheWidth && h == cacheHeight
                && startTime == cacheStartTime && endTime == cacheEndTime
                && !cachedWaveform.isNull())
            {
                painter.drawPixmap(0, 0, cachedWaveform);
            }
            else
            {
                juce::Image juceImg(juce::Image::ARGB, w, h, true);
                {
                    juce::Graphics g(juceImg);
                    // drawChannels paints with the Graphics context's current
                    // colour (it does not set its own), so set a visible one
                    // first — the JUCE image starts cleared, and without this
                    // the waveform would render in the default (black) colour
                    // and be invisible on the dark bgWidget() background. Same
                    // fix applied to AudioClipItem::paintContent in v0.8.0.
                    g.setColour(juce::Colours::white.withAlpha(0.85f));
                    if (thumbnail->getNumChannels() > 0)
                    {
                        thumbnail->drawChannels(g,
                            juce::Rectangle<int>(0, 0, w, h),
                            startTime, endTime,
                            1.0f);
                    }
                }

                // Copy the JUCE image into an independently-allocated QImage
                // (row-by-row memcpy). The previous code wrapped JUCE's buffer
                // in place via the QImage(data,stride,...) constructor: that
                // shares memory with the local juceImg, and once juceImg goes
                // out of scope the buffer is freed — leaving the QPixmap/
                // drawPixmap reading freed memory and crashing inside Qt's
                // raster paint engine. Mirrors the working AudioClipItem path.
                QImage qimg(w, h, QImage::Format_ARGB32_Premultiplied);
                qimg.fill(Qt::transparent);
                {
                    juce::Image::BitmapData bitmapData(juceImg, juce::Image::BitmapData::readOnly);
                    for (int y = 0; y < h; ++y)
                        std::memcpy(qimg.scanLine(y),
                                    bitmapData.data + y * bitmapData.lineStride,
                                    static_cast<size_t>(w) * 4);
                }

                cachedWaveform = QPixmap::fromImage(qimg);
                cacheWidth = w;
                cacheHeight = h;
                cacheStartTime = startTime;
                cacheEndTime = endTime;

                painter.drawPixmap(0, 0, cachedWaveform);
            }
        }
        else
        {
            painter.fillRect(rect(), QColor(40, 40, 45));
            painter.setPen(QColor(255, 255, 255, 60));
            painter.drawText(rect(), Qt::AlignCenter, "Waveform loading...");
        }
    }
    else
    {
        auto sourceFile = currentClip.getProperty(IDs::sourceFile).toString();
        if (!sourceFile.isEmpty() && !juce::File(sourceFile).existsAsFile())
        {
            painter.fillRect(rect(), QColor(50, 15, 15));
            painter.setPen(QColor(255, 100, 100));
            painter.drawText(rect(), Qt::AlignCenter,
                             "Missing file:\n" + QString::fromUtf8(sourceFile.toRawUTF8()));
        }
        else
        {
            painter.fillRect(rect(), QColor(40, 40, 45));
            painter.setPen(QColor(255, 255, 255, 60));
            painter.drawText(rect(), Qt::AlignCenter, "Waveform loading...");
        }
    }

    // Selection highlight
    if (selStart >= 0 && selEnd >= 0 && selEnd > selStart)
    {
        double sx = (selStart) * pixelsPerSecond - scrollX;
        double ex = (selEnd) * pixelsPerSecond - scrollX;
        QRectF selRect(sx, 0, ex - sx, h);
        painter.fillRect(selRect, QColor(0x06, 0xb6, 0xd4, 30));
        painter.setPen(QPen(QColor(0x06, 0xb6, 0xd4, 120), 1));
        painter.drawRect(selRect);
    }

    // Fade overlays
    double fadeIn = currentClip.getProperty(IDs::fadeIn);
    double fadeOut = currentClip.getProperty(IDs::fadeOut);

    if (fadeIn > 0 || fadeOut > 0)
    {
        // Draw fade curves
        painter.setPen(QPen(ThemeColors::accent(), 1));

        // Fade-in triangle
        if (fadeIn > 0)
        {
            double fiPx = fadeIn * pixelsPerSecond;
            QPainterPath fadeInPath;
            fadeInPath.moveTo(0, h);
            fadeInPath.lineTo(fiPx, 0);
            fadeInPath.lineTo(0, 0);
            fadeInPath.closeSubpath();
            painter.fillPath(fadeInPath, QColor(0x06, 0xb6, 0xd4, 50));
            painter.drawPath(fadeInPath);
        }

        // Fade-out triangle
        if (fadeOut > 0)
        {
            double durPx = duration * pixelsPerSecond;
            double foPx = fadeOut * pixelsPerSecond;
            QPainterPath fadeOutPath;
            fadeOutPath.moveTo(durPx, 0);
            fadeOutPath.lineTo(durPx - foPx, h);
            fadeOutPath.lineTo(durPx, h);
            fadeOutPath.closeSubpath();
            painter.fillPath(fadeOutPath, QColor(0x06, 0xb6, 0xd4, 50));
            painter.drawPath(fadeOutPath);
        }
    }

    // Playhead
    if (playheadSeconds >= 0 && playheadSeconds >= offset && playheadSeconds <= offset + duration)
    {
        double phx = (playheadSeconds - offset) * pixelsPerSecond - scrollX;
        painter.setPen(QPen(ThemeColors::accentBright(), 1));
        painter.drawLine(QPointF(phx, 0), QPointF(phx, h));
    }
}

void AudioWaveformWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !currentClip.isValid())
    {
        QWidget::mousePressEvent(event);
        return;
    }

    QPoint pos = event->pos();

    if (isOverFadeIn(pos))
    {
        dragMode = DragMode::FadeIn;
        dragStart = pos;
        dragStartFade = currentClip.getProperty(IDs::fadeIn);
        // Begin a new undo transaction on press so the entire fade drag is a
        // single undo step (AGENTS.md: "each drag should be one undo step").
        if (undoManager) undoManager->beginNewTransaction();
        grabMouse(); // capture all mouse events until release (H6)
    }
    else if (isOverFadeOut(pos))
    {
        dragMode = DragMode::FadeOut;
        dragStart = pos;
        dragStartFade = currentClip.getProperty(IDs::fadeOut);
        if (undoManager) undoManager->beginNewTransaction();
        grabMouse();
    }
    else
    {
        dragMode = DragMode::SelectRegion;
        dragStart = pos;
        selStart = timeAtPos(pos.x());
        selEnd = selStart;
        grabMouse();
        update();
    }
}

void AudioWaveformWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!currentClip.isValid() || dragMode == DragMode::None)
    {
        QWidget::mouseMoveEvent(event);
        return;
    }

    QPoint pos = event->pos();
    double duration = currentClip.getProperty(IDs::duration);

    if (dragMode == DragMode::FadeIn)
    {
        double dx = (pos.x() - dragStart.x()) / pixelsPerSecond;
        double newFade = (std::max)(0.0, (std::min)(duration * 0.5, dragStartFade + dx));
        // Write through the UndoManager so the drag is undoable. Previously
        // this used nullptr, making the drag non-undoable and inconsistent
        // with the spinbox path (projectCmds->setClipFadeIn). The transaction
        // was begun on press above.
        currentClip.setProperty(IDs::fadeIn, newFade, undoManager);
        emit fadeInChanged(newFade);
        update();
    }
    else if (dragMode == DragMode::FadeOut)
    {
        double dx = (dragStart.x() - pos.x()) / pixelsPerSecond;
        double newFade = (std::max)(0.0, (std::min)(duration * 0.5, dragStartFade + dx));
        currentClip.setProperty(IDs::fadeOut, newFade, undoManager);
        emit fadeOutChanged(newFade);
        update();
    }
    else if (dragMode == DragMode::SelectRegion)
    {
        selEnd = timeAtPos(pos.x());
        update();
    }
}

void AudioWaveformWidget::mouseReleaseEvent(QMouseEvent*)
{
    // Always release the mouse grab acquired on press (H6). With grabMouse the
    // release always lands here even if the cursor left the widget, so a drag
    // can no longer be abandoned mid-way by crossing the widget edge.
    if (dragMode != DragMode::None)
        releaseMouse();

    bool hasSel = currentClip.isValid() && selStart >= 0.0 && selEnd >= 0.0 && selEnd > selStart;

    if (hasSel)
    {
        if (selStart > selEnd)
            std::swap(selStart, selEnd);

        if (selEnd - selStart > 0.01)
            emit regionSelected(selStart, selEnd);
        else
            selStart = selEnd = -1.0;
    }
    else
    {
        // No valid selection (e.g. a click without drag): clear stale state so
        // a subsequent Copy doesn't read a leftover single-point selection.
        selStart = -1.0;
        selEnd = -1.0;
    }

    dragMode = DragMode::None;
    update();
}

void AudioWaveformWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        double factor = (event->angleDelta().y() > 0) ? 1.3 : 1.0 / 1.3;
        setPixelsPerSecond(pixelsPerSecond * factor);
    }
    else
    {
        int delta = event->angleDelta().x();
        if (delta == 0)
            delta = event->angleDelta().y();
        scrollX = (std::max)(0, scrollX - delta);
        update();
    }
    // Accept in both branches so the horizontal scroll doesn't also bubble up
    // to an ancestor scroll area. Matches NoteGridWidget/AutomationLaneWidget.
    event->accept();
}

void AudioWaveformWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    // If a drag is in progress when focus is lost (e.g. Alt-Tab), cancel it
    // cleanly and release the mouse grab so the widget isn't left in a
    // captured-mouse state. Note: we intentionally do NOT cancel on
    // leaveEvent — grabMouse() keeps the drag alive across widget-edge
    // crossings, which is the whole point (the old leaveEvent cancel abandoned
    // mid-drag region/fade selections).
    if (dragMode != DragMode::None)
    {
        releaseMouse();
        dragMode = DragMode::None;
        update();
    }
}

void AudioWaveformWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (!currentClip.isValid())
    {
        QWidget::contextMenuEvent(event);
        return;
    }

    QMenu menu;

    auto* copyAction = menu.addAction("Copy\tCtrl+C");
    copyAction->setEnabled(hasSelection());
    connect(copyAction, &QAction::triggered, this, &AudioWaveformWidget::copyRequested);

    auto* cutAction = menu.addAction("Cut\tCtrl+X");
    cutAction->setEnabled(hasSelection());
    connect(cutAction, &QAction::triggered, this, &AudioWaveformWidget::cutRequested);

    auto* pasteAction = menu.addAction("Paste\tCtrl+V");
    connect(pasteAction, &QAction::triggered, this, &AudioWaveformWidget::pasteRequested);

    menu.addSeparator();

    auto* selectAllAction = menu.addAction("Select All\tCtrl+A");
    connect(selectAllAction, &QAction::triggered, this, &AudioWaveformWidget::selectAllRequested);

    menu.exec(event->globalPos());
    event->accept();
}
