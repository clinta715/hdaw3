#pragma once
#include "ClipItem.h"
#include "../engine/ProjectPool.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <QPixmap>
#include <atomic>
#include <memory>

class AudioClipItem : public ClipItem
{
public:
    AudioClipItem(juce::ValueTree clipTree, double pixelsPerSecond, HDAW::ProjectPool& pool);
    ~AudioClipItem() override;

protected:
    void paintContent(QPainter& painter, const QRectF& contentRect) override;

private:
    void loadThumbnail();
    void invalidateCache();

    HDAW::ProjectPool& projectPool;
    std::unique_ptr<juce::AudioThumbnail> thumbnail;
    bool thumbnailLoaded = false;
    double cachedSampleRate = 44100.0;

    QPixmap cachedWaveform;
    double cacheOffset = -1.0;
    double cacheDuration = -1.0;
    int cacheWidth = 0;
    int cacheHeight = 0;

    struct ThumbnailListener : public juce::ChangeListener
    {
        ThumbnailListener(AudioClipItem& owner);
        ~ThumbnailListener() override;
        void changeListenerCallback(juce::ChangeBroadcaster* source) override;

        AudioClipItem* item;
        std::atomic<bool> alive{ true };
    };

    std::shared_ptr<ThumbnailListener> thumbListener;
};
