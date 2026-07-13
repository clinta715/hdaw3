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

    void switchToSourceFile(const juce::String& path)
    {
        sourceFile = path;
        preloadedData[0].free();
        preloadedData[1].free();
        preloadedChannels = 0;
        preloadedLength = 0;

        if (sourceFile.isNotEmpty() && sr > 0)
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

    // Identifies this clip for StretchCache lookups. Set by RoutingManager
    // when the processor is built/updated from the ValueTree.
    void setClipID(int id) { clipID = id; }
    int getClipID() const { return clipID; }

    // Stretch ratio as resolved from the ValueTree by RoutingManager
    // (1.0 = no stretch). Used by RoutingManager to key StretchCache.
    void setStretchRatio(double r) { stretchRatio = r; }
    double getStretchRatio() const { return stretchRatio; }

    // Adopts a stretched buffer produced by StretchCache. Called on the
    // message thread during rebuildRoutingGraph's prepareToPlay. After
    // this call, processBlock reads from stretchedData instead of
    // preloadedData; the original preloadedData is retained so a later
    // cache miss (e.g. ratio reverted to 1.0) can fall back to it.
    // `length` is per-channel sample count; `channels` is 1 or 2.
    void adoptStretchedBuffer(const int* ch0, const int* ch1,
                              int64_t length, int channels)
    {
        if (length <= 0)
        {
            clearStretchedBuffer();
            return;
        }
        stretchedData[0].free();
        stretchedData[1].free();
        stretchedData[0].malloc(static_cast<size_t>(length));
        stretchedData[1].malloc(static_cast<size_t>(length));
        std::copy_n(ch0, static_cast<size_t>(length), stretchedData[0].get());
        if (channels > 1 && ch1 != nullptr)
            std::copy_n(ch1, static_cast<size_t>(length), stretchedData[1].get());
        else
            std::copy_n(ch0, static_cast<size_t>(length), stretchedData[1].get());
        stretchedChannels = juce::jmax(1, channels);
        stretchedLength = length;
        activeBuffer.store(1, std::memory_order_release);
    }

    void clearStretchedBuffer()
    {
        activeBuffer.store(0, std::memory_order_release);
        stretchedData[0].free();
        stretchedData[1].free();
        stretchedChannels = 0;
        stretchedLength = 0;
    }

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
        stretchedData[0].free();
        stretchedData[1].free();
        stretchedChannels = 0;
        stretchedLength = 0;
        activeBuffer.store(0, std::memory_order_release);
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

        // Select the active source buffer. The audio thread reads exactly one
        // pointer per block; swap-in happens during rebuildRoutingGraph on the
        // message thread, so this load is lock-free and allocation-free.
        const int buf = activeBuffer.load(std::memory_order_acquire);
        const int* srcPtrs[2];
        int srcChannels;
        int64_t srcLength;
        if (buf == 1 && stretchedLength > 0)
        {
            srcPtrs[0] = stretchedData[0];
            srcPtrs[1] = stretchedData[1];
            srcChannels = stretchedChannels;
            srcLength = stretchedLength;
        }
        else
        {
            srcPtrs[0] = preloadedData[0];
            srcPtrs[1] = preloadedData[1];
            srcChannels = preloadedChannels;
            srcLength = preloadedLength;
        }

        // Read from in-memory buffer (RT-safe: no disk I/O on audio thread)
        int numToRead = 0;
        if (srcLength > 0 && sourceSample < srcLength)
        {
            numToRead = (std::min)(numSamples, static_cast<int>(srcLength - sourceSample));

            buffer.clear();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                int srcCh = (srcChannels > 1) ? (std::min)(ch, srcChannels - 1) : 0;
                const int* src = srcPtrs[srcCh];
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

    // Stretched buffer produced off-thread by StretchCache and adopted
    // during rebuildRoutingGraph. processBlock selects between this and
    // preloadedData via `activeBuffer` (one atomic load per block).
    juce::HeapBlock<int> stretchedData[2];
    int stretchedChannels = 0;
    int64_t stretchedLength = 0;
    std::atomic<int> activeBuffer{ 0 }; // 0 = preloaded, 1 = stretched

    int clipID = -1;
    double stretchRatio = 1.0; // resolved at rebuild; not RT-parametric

    juce::LinearSmoothedValue<float> gainSmooth;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipSourceProcessor)
};

} // namespace HDAW
