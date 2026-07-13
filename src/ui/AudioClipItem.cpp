#include "AudioClipItem.h"
#include <QPainter>
#include <QImage>
#include <cstring>
#include "DebugLog.h"

AudioClipItem::ThumbnailListener::ThumbnailListener(AudioClipItem& owner)
    : item(&owner) {}

AudioClipItem::ThumbnailListener::~ThumbnailListener()
{
    alive.store(false, std::memory_order_release);
}

void AudioClipItem::ThumbnailListener::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (!alive.load(std::memory_order_acquire))
        return;
    if (source == item->thumbnail.get())
    {
        double len = item->thumbnail->getTotalLength();
        HDAW_LOG("AUCChg", QString("thumbnail loaded length=%1").arg(len));
        item->invalidateCache();
        item->update();
    }
}

AudioClipItem::AudioClipItem(juce::ValueTree tree, double pps, HDAW::ProjectPool& pool)
    : ClipItem(tree, pps), projectPool(pool)
{
    setCacheMode(QGraphicsItem::NoCache);
    thumbListener = std::make_shared<ThumbnailListener>(*this);
    loadThumbnail();
}

AudioClipItem::~AudioClipItem()
{
    thumbListener->alive.store(false, std::memory_order_release);
    if (thumbnail)
        thumbnail->removeChangeListener(thumbListener.get());
}

void AudioClipItem::loadThumbnail()
{
    auto sourceFile = clipTree.getProperty(IDs::sourceFile).toString();
    HDAW_LOG("AUCLoad", QString("sourceFile=%1 empty=%2").arg(QString::fromUtf8(sourceFile.toRawUTF8())).arg(sourceFile.isEmpty()));
    if (sourceFile.isEmpty())
        return;

    thumbnail = projectPool.createThumbnail(1000, projectPool.getThumbnailCache());
    if (thumbnail == nullptr)
    {
        HDAW_LOG("AUCLoad", "FAILED: createThumbnail returned null");
        return;
    }

    auto file = juce::File(sourceFile);
    HDAW_LOG("AUCLoad", QString("file.exists=%1 isDir=%2").arg(file.existsAsFile()).arg(file.isDirectory()));
    if (file.existsAsFile())
    {
        thumbnail->setSource(new juce::FileInputSource(file));
        thumbnail->addChangeListener(thumbListener.get());
        thumbnailLoaded = true;
    }
}

void AudioClipItem::invalidateCache()
{
    cacheWidth = 0;
    cacheHeight = 0;
    cachedWaveform = QPixmap();
}

void AudioClipItem::paintContent(QPainter& painter, const QRectF& contentRect)
{
    if (!thumbnail || thumbnail->getTotalLength() <= 0)
    {
        painter.fillRect(contentRect, QColor(255, 255, 255, 10));
        return;
    }

    int w = static_cast<int>(contentRect.width());
    int h = static_cast<int>(contentRect.height());
    if (w <= 0 || h <= 0) return;

    double offset = clipTree.getProperty(IDs::offset);
    double duration = getDuration();

    if (w == cacheWidth && h == cacheHeight
        && offset == cacheOffset && duration == cacheDuration
        && !cachedWaveform.isNull())
    {
        painter.drawPixmap(contentRect.topLeft(), cachedWaveform);
        return;
    }

    HDAW_LOG("AUCPaint", QString("RENDERING waveform w=%1 h=%2 offset=%3 dur=%4").arg(w).arg(h).arg(offset).arg(duration));

    juce::Image juceImg(juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g(juceImg);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        thumbnail->drawChannels(g,
                                juce::Rectangle<int>(0, 0, w, h),
                                offset, offset + duration,
                                1.0f);
    }

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
    cacheOffset = offset;
    cacheDuration = duration;

    if (!cachedWaveform.isNull())
    {
        painter.drawPixmap(contentRect.topLeft(), cachedWaveform);
        HDAW_LOG("AUCPaint", "waveform drawn done");
    }
    else
    {
        HDAW_LOG("AUCPaint", "ERROR: cachedWaveform is null");
        painter.fillRect(contentRect, QColor(255, 0, 0, 50));
    }
}
