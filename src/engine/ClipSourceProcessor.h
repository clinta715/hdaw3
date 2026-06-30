#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <algorithm>
#include "TransportManager.h"

namespace HDAW {

class ClipSourceProcessor : public juce::AudioProcessor
{
public:
    ClipSourceProcessor(HDAW::TransportManager& tm, juce::AudioFormatManager& fm)
        : AudioProcessor(BusesProperties()
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          transportManager(tm), formatManager(fm)
    {
    }

    ~ClipSourceProcessor() override = default;

    void setSourceFile(const juce::String& path)
    {
        sourceFile = path;
    }

    juce::String getSourceFile() const { return sourceFile; }

    void setStartTime(double t) { startTime.store(t); }
    double getStartTime() const { return startTime.load(); }

    void setDuration(double d) { duration.store(d); }
    double getDuration() const { return duration.load(); }

    void setOffset(double o) { offset.store(o); }
    void setGain(float g) { gain.store(g); }
    void setFadeIn(float f) { fadeIn.store(f); }
    void setFadeOut(float f) { fadeOut.store(f); }
    void setLooping(bool l) { looping.store(l); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        (void) samplesPerBlock;
        sr = sampleRate;

        preloadedData[0].free();
        preloadedData[1].free();
        preloadedChannels = 0;
        preloadedLength = 0;

        if (sourceFile.isNotEmpty())
        {
            std::unique_ptr<juce::AudioFormatReader> r(
                formatManager.createReaderFor(juce::File(sourceFile)));
            if (r != nullptr)
            {
                preloadedChannels = juce::jmin(static_cast<int>(r->numChannels), 2);
                const int total = static_cast<int>(r->lengthInSamples);
                if (preloadedChannels > 0 && total > 0)
                {
                    preloadedData[0].malloc(total);
                    preloadedData[1].malloc(total);
                    int* const ptrs[2] = { preloadedData[0], preloadedData[1] };
                    r->read(ptrs, preloadedChannels, 0, total, true);
                    preloadedLength = static_cast<int64_t>(total);
                }
            }
        }

        gainSmooth.reset(sampleRate, 0.02);
        gainSmooth.setCurrentAndTargetValue(gain.load());
    }

    void releaseResources() override
    {
        preloadedData[0].free();
        preloadedData[1].free();
        preloadedChannels = 0;
        preloadedLength = 0;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = juce::jmin(2, buffer.getNumChannels());

        if (numSamples <= 0)
            return;

        // Calculate clip-local sample position
        int64_t transportSample = transportManager.getCurrentSample();
        double startSampleDouble = startTime.load() * sr;
        int64_t startSample = static_cast<int64_t>(startSampleDouble);
        double durSec = duration.load();
        int64_t durSamples = static_cast<int64_t>(durSec * sr);
        double offSec = offset.load();
        int64_t offsetSamples = static_cast<int64_t>(offSec * sr);
        bool isLooping = looping.load();

        // Read position relative to clip start
        int64_t clipLocalSample = transportSample - startSample;

        // Clip bounds check
        if (durSamples <= 0 || (!isLooping && (clipLocalSample < 0 || clipLocalSample >= durSamples)))
        {
            buffer.clear();
            return;
        }

        // Looping wrap
        if (isLooping && durSamples > 0)
        {
            clipLocalSample = clipLocalSample % durSamples;
            if (clipLocalSample < 0)
                clipLocalSample += durSamples;
        }

        int64_t sourceSample = offsetSamples + clipLocalSample;

        // Read from preloaded in-memory buffer (RT-safe: no disk I/O on audio thread)
        int numToRead = 0;
        if (preloadedLength > 0 && sourceSample < preloadedLength)
        {
            numToRead = (std::min)(numSamples, static_cast<int>(preloadedLength - sourceSample));

            buffer.clear();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                int srcCh = (preloadedChannels > 1) ? (std::min)(ch, preloadedChannels - 1) : 0;
                const int* src = preloadedData[srcCh];
                float* dest = buffer.getWritePointer(ch);
                for (int s = 0; s < numToRead; ++s)
                    dest[s] = static_cast<float>(src[sourceSample + s]) / 32768.0f;
                for (int s = numToRead; s < numSamples; ++s)
                    dest[s] = 0.0f;
            }
        }
        else
        {
            buffer.clear();
            return;
        }

        // Apply gain, fadeIn, fadeOut
        float currentFadeIn = fadeIn.load();
        float currentFadeOut = fadeOut.load();

        gainSmooth.setTargetValue(gain.load(std::memory_order_relaxed));

        int64_t fadeInSamples = static_cast<int64_t>(currentFadeIn * sr);
        int64_t fadeOutSamples = static_cast<int64_t>(currentFadeOut * sr);

        for (int s = 0; s < numToRead; ++s)
        {
            int64_t localPos = clipLocalSample + s;

            float envelope = 1.0f;

            // Fade in
            if (fadeInSamples > 0 && localPos < fadeInSamples)
                envelope *= static_cast<float>(localPos) / static_cast<float>(fadeInSamples);

            // Fade out
            if (fadeOutSamples > 0 && localPos > durSamples - fadeOutSamples)
                envelope *= static_cast<float>(durSamples - localPos) / static_cast<float>(fadeOutSamples);

            float g = gainSmooth.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* channelData = buffer.getWritePointer(ch);
                channelData[s] *= g * envelope;
            }
        }
    }

    // AudioProcessor boilerplate
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "ClipSource"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    HDAW::TransportManager& transportManager;
    juce::AudioFormatManager& formatManager;
    juce::String sourceFile;

    std::atomic<double> startTime{ 0.0 };
    std::atomic<double> duration{ 1.0 };
    std::atomic<double> offset{ 0.0 };
    std::atomic<float> gain{ 1.0f };
    std::atomic<float> fadeIn{ 0.0f };
    std::atomic<float> fadeOut{ 0.0f };
    std::atomic<bool> looping{ false };

    juce::HeapBlock<int> preloadedData[2];
    int preloadedChannels = 0;
    int64_t preloadedLength = 0;
    juce::LinearSmoothedValue<float> gainSmooth;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipSourceProcessor)
};

} // namespace HDAW
