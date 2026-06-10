#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace HDAW {

/**
 * Manages audio assets and their visual representations (thumbnails).
 */
class ProjectPool
{
public:
    ProjectPool()
        : thumbnailCache(50) // Cache up to 50 thumbnails in memory
    {
        // Register standard audio formats
        formatManager.registerBasicFormats();
    }

    ~ProjectPool() = default;

    juce::AudioFormatManager& getFormatManager() { return formatManager; }
    juce::AudioThumbnailCache& getThumbnailCache() { return thumbnailCache; }

    /**
     * Helper to create a new thumbnail for a specific file.
     * Note: The caller usually owns the AudioThumbnail instance.
     */
    std::unique_ptr<juce::AudioThumbnail> createThumbnail(int sourceSamplesPerThumbnailSample,
                                                         juce::AudioThumbnailCache& cache)
    {
        return std::make_unique<juce::AudioThumbnail>(sourceSamplesPerThumbnailSample, 
                                                      formatManager, 
                                                      cache);
    }

private:
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectPool)
};

} // namespace HDAW
