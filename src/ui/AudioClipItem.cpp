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
        // Toggle cache mode to force a QGraphicsItem cache rebuild now that
        // the waveform content has changed asynchronously. Calling update()
        // alone does not invalidate DeviceCoordinateCache for content-only
        // changes (no geometry change). This toggle is the proven fix from
        // v0.8.0 (9882c40); the base ClipItem now uses NoCache, but this
        // guard protects against any future subclass re-enabling a cache.
        item->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
        item->setCacheMode(QGraphicsItem::NoCache);
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
        // Cache the sample rate for vertical zoom calculation in paintContent
        if (auto* reader = projectPool.getFormatManager().createReaderFor(file))
        {
            cachedSampleRate = reader->sampleRate;
            delete reader;
        }

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
        auto sourceFile = clipTree.getProperty(IDs::sourceFile).toString();
        if (!sourceFile.isEmpty() && !juce::File(sourceFile).existsAsFile())
        {
            painter.fillRect(contentRect, QColor(100, 20, 20, 80));
            painter.setPen(QColor(255, 100, 100, 180));
            QFont f = painter.font();
            f.setPointSize(8);
            painter.setFont(f);
            painter.drawText(contentRect, Qt::AlignCenter, "Missing:\n" + QString::fromUtf8(sourceFile.toRawUTF8()));
        }
        else
        {
            painter.fillRect(contentRect, QColor(255, 255, 255, 40));
        }
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

        // Scale the vertical zoom factor inversely with how many source
        // samples each pixel represents.  For a short clip every pixel
        // covers few samples and the thumbnail data is already detailed,
        // so 1× is fine.  For a long clip each pixel spans thousands of
        // samples and drawChannels averages them into tiny min/max pairs,
        // making the waveform nearly invisible.  Boosting the zoom factor
        // compensates: a 10-minute clip at 20 px/s gets ~3× zoom.
        double samplesPerPixel = (duration * cachedSampleRate) / (double)w;
        float vZoom = static_cast<float>(
            (std::max)(1.0, std::sqrt(samplesPerPixel / 500.0)));
        vZoom = (std::min)(vZoom, 6.0f);

        thumbnail->drawChannels(g,
                                juce::Rectangle<int>(0, 0, w, h),
                                offset, offset + duration,
                                vZoom);
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
