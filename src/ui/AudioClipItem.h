#pragma once
#include "ClipItem.h"
#include "../engine/ProjectPool.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <QPixmap>

class AudioClipItem : public ClipItem,
                       private juce::ChangeListener
{
public:
    AudioClipItem(juce::ValueTree clipTree, double pixelsPerSecond, HDAW::ProjectPool& pool);
    ~AudioClipItem() override;

protected:
    void paintContent(QPainter& painter, const QRectF& contentRect) override;

private:
    void loadThumbnail();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void invalidateCache();

    HDAW::ProjectPool& projectPool;
    std::unique_ptr<juce::AudioThumbnail> thumbnail;
    bool thumbnailLoaded = false;

    QPixmap cachedWaveform;
    double cacheOffset = -1.0;
    double cacheDuration = -1.0;
    int cacheWidth = 0;
    int cacheHeight = 0;
};
