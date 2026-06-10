#include "AudioClipItem.h"
#include <QPainter>
#include <QImage>

AudioClipItem::AudioClipItem(juce::ValueTree tree, double pps, HDAW::ProjectPool& pool)
    : ClipItem(tree, pps), projectPool(pool)
{
    loadThumbnail();
}

AudioClipItem::~AudioClipItem()
{
    if (thumbnail)
        thumbnail->removeChangeListener(this);
}

void AudioClipItem::loadThumbnail()
{
    auto sourceFile = clipTree.getProperty(IDs::sourceFile).toString();
    if (sourceFile.isEmpty())
        return;

    thumbnail = projectPool.createThumbnail(1000, projectPool.getThumbnailCache());
    if (thumbnail == nullptr)
        return;

    auto file = juce::File(sourceFile);
    if (file.existsAsFile())
    {
        thumbnail->setSource(new juce::FileInputSource(file));
        thumbnail->addChangeListener(this);
        thumbnailLoaded = true;
    }
}

void AudioClipItem::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == thumbnail.get())
        update();
}

void AudioClipItem::paintContent(QPainter& painter, const QRectF& contentRect)
{
    if (thumbnail && thumbnail->getTotalLength() > 0)
    {
        int w = static_cast<int>(contentRect.width());
        int h = static_cast<int>(contentRect.height());
        if (w <= 0 || h <= 0) return;

        double offset = clipTree.getProperty(IDs::offset);
        double duration = getDuration();

        juce::Image juceImg(juce::Image::ARGB, w, h, true);
        juce::Graphics g(juceImg);
        g.setColour(juce::Colours::transparentBlack);
        g.fillAll();
        thumbnail->drawChannels(g,
                                juce::Rectangle<int>(0, 0, w, h),
                                offset, offset + duration,
                                1.0f);

        juce::Image::BitmapData bitmapData(juceImg, juce::Image::BitmapData::readOnly);
        QImage qimg(reinterpret_cast<const uchar*>(bitmapData.data),
                    w, h,
                    static_cast<int>(bitmapData.lineStride),
                    QImage::Format_ARGB32_Premultiplied);
        painter.drawImage(contentRect.topLeft(), qimg);
        return;
    }

    // Fallback: empty region fill
    painter.fillRect(contentRect, QColor(255, 255, 255, 10));
}
