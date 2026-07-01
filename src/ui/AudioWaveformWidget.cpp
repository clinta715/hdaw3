#include "AudioWaveformWidget.h"
#include "Theme.h"
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QWheelEvent>
#include <QApplication>
#include <cmath>

AudioWaveformWidget::AudioWaveformWidget(HDAW::ProjectPool& pool, QWidget* parent)
    : QWidget(parent), projectPool(pool)
{
    setMouseTracking(true);
    setMinimumHeight(80);
    qApp->installEventFilter(this);
}

AudioWaveformWidget::~AudioWaveformWidget() = default;

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
    thumbnail.reset();
    invalidateWaveformCache();
    if (!currentClip.isValid())
        return;

    auto sourceFile = currentClip.getProperty(IDs::sourceFile).toString();
    if (sourceFile.isEmpty())
        return;

    thumbnail = projectPool.createThumbnail(1000, projectPool.getThumbnailCache());
    if (thumbnail == nullptr)
        return;

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

double AudioWaveformWidget::beatAtPos(int x) const
{
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
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    // Background
    painter.fillRect(rect(), ThemeColors::bgWidget());

    if (!currentClip.isValid())
    {
        painter.setPen(ThemeColors::textMuted());
        painter.drawText(rect(), Qt::AlignCenter, "No clip loaded");
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
                juce::Graphics g(juceImg);
                g.setColour(juce::Colours::transparentBlack);
                g.fillAll();

                if (thumbnail->getNumChannels() > 0)
                {
                    thumbnail->drawChannels(g,
                        juce::Rectangle<int>(0, 0, w, h),
                        startTime, endTime,
                        1.0f);
                }

                juce::Image::BitmapData bitmapData(juceImg, juce::Image::BitmapData::readOnly);
                QImage qimg(reinterpret_cast<const uchar*>(bitmapData.data),
                    w, h,
                    static_cast<int>(bitmapData.lineStride),
                    QImage::Format_ARGB32_Premultiplied);

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
            painter.fillRect(rect(), QColor(255, 255, 255, 10));
        }
    }
    else
    {
        painter.fillRect(rect(), QColor(255, 255, 255, 10));
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
    }
    else if (isOverFadeOut(pos))
    {
        dragMode = DragMode::FadeOut;
        dragStart = pos;
        dragStartFade = currentClip.getProperty(IDs::fadeOut);
    }
    else
    {
        dragMode = DragMode::SelectRegion;
        dragStart = pos;
        selStart = beatAtPos(pos.x());
        selEnd = selStart;
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
        currentClip.setProperty(IDs::fadeIn, newFade, nullptr);
        emit fadeInChanged(newFade);
        update();
    }
    else if (dragMode == DragMode::FadeOut)
    {
        double dx = (dragStart.x() - pos.x()) / pixelsPerSecond;
        double newFade = (std::max)(0.0, (std::min)(duration * 0.5, dragStartFade + dx));
        currentClip.setProperty(IDs::fadeOut, newFade, nullptr);
        emit fadeOutChanged(newFade);
        update();
    }
    else if (dragMode == DragMode::SelectRegion)
    {
        selEnd = beatAtPos(pos.x());
        update();
    }
}

void AudioWaveformWidget::mouseReleaseEvent(QMouseEvent*)
{
    if (dragMode == DragMode::SelectRegion && currentClip.isValid())
    {
        if (selStart > selEnd)
            std::swap(selStart, selEnd);

        if (selEnd - selStart > 0.01)
            emit regionSelected(selStart, selEnd);
        else
            selStart = selEnd = -1.0;

        update();
    }

    dragMode = DragMode::None;
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
}

void AudioWaveformWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    if (dragMode != DragMode::None)
    {
        dragMode = DragMode::None;
        update();
    }
}

void AudioWaveformWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (dragMode != DragMode::None)
    {
        dragMode = DragMode::None;
        update();
    }
}

bool AudioWaveformWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease && dragMode != DragMode::None && obj != this)
    {
        dragMode = DragMode::None;
        update();
    }
    return QWidget::eventFilter(obj, event);
}
