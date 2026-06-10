#pragma once
#include "ClipItem.h"
#include "../engine/ProjectPool.h"
#include <juce_audio_utils/juce_audio_utils.h>

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

    HDAW::ProjectPool& projectPool;
    std::unique_ptr<juce::AudioThumbnail> thumbnail;
    bool thumbnailLoaded = false;
};
