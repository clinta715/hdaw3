#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <algorithm>
#include <vector>
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
                    float* const ptrs[2] = { preloadedData[0], preloadedData[1] };
                    r->read(ptrs, preloadedChannels, 0, total);
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
                    float* const ptrs[2] = { preloadedData[0], preloadedData[1] };
                    r->read(ptrs, preloadedChannels, 0, total);
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
        const float* preloadedPtrs[2];
        const int* stretchedPtrs[2];
        int srcChannels;
        int64_t srcLength;
        bool useFloatBuffer = false;
        if (buf == 1 && stretchedLength > 0)
        {
            stretchedPtrs[0] = stretchedData[0];
            stretchedPtrs[1] = stretchedData[1];
            srcChannels = stretchedChannels;
            srcLength = stretchedLength;
        }
        else
        {
            preloadedPtrs[0] = preloadedData[0];
            preloadedPtrs[1] = preloadedData[1];
            srcChannels = preloadedChannels;
            srcLength = preloadedLength;
            useFloatBuffer = true;
        }

        // Read from in-memory buffer (RT-safe: no disk I/O on audio thread).
        // The audible window is the number of samples remaining within the
        // clip's *duration* from the current read position. Clamping to this
        // (in addition to the source-file length) prevents the read loop from
        // pulling samples past the clip end when the source file is longer
        // than the clip (the normal "trimmed clip" case). Without this clamp
        // the fade-out envelope below goes negative past durSamples, inverting
        // the phase of a tail that should not be audible at all.
        int64_t audibleRemaining = durSamples - clipLocalSample;
        int numToRead = 0;
        if (srcLength > 0 && sourceSample < srcLength && audibleRemaining > 0)
        {
            int availFromSource = static_cast<int>(srcLength - sourceSample);
            numToRead = (std::min)((std::min)(numSamples, availFromSource),
                                   static_cast<int>(audibleRemaining));

            buffer.clear();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                int srcCh = (srcChannels > 1) ? (std::min)(ch, srcChannels - 1) : 0;
                float* dest = buffer.getWritePointer(ch);
                if (useFloatBuffer)
                {
                    const float* src = preloadedPtrs[srcCh];
                    for (int s = 0; s < numToRead; ++s)
                        dest[s] = src[sourceSample + s];
                }
                else
                {
                    const int* src = stretchedPtrs[srcCh];
                    for (int s = 0; s < numToRead; ++s)
                        dest[s] = static_cast<float>(src[sourceSample + s]) / 32768.0f;
                }
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

            // Fade out. Clamp to >= 0 so the envelope can never go negative
            // (which would invert phase of the tail). At localPos == durSamples
            // the envelope is 0; beyond it the clamp keeps it at 0 instead of
            // turning negative.
            if (fadeOutSamples > 0 && localPos > durSamples - fadeOutSamples)
            {
                float fadePos = static_cast<float>(durSamples - localPos)
                              / static_cast<float>(fadeOutSamples);
                envelope *= (std::max)(0.0f, fadePos);
            }

            // Gain envelope (per-clip automation)
            double clipLocalTime = static_cast<double>(localPos) / sr;
            double envGain = getGainAtTime(clipLocalTime);

            float g = gainSmooth.getNextValue();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* channelData = buffer.getWritePointer(ch);
                channelData[s] *= g * envelope * static_cast<float>(envGain);
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

    // Gain envelope support.
    //
    // RT contract: `getGainAtTime` is called once per sample on the audio
    // thread (see processBlock). It MUST NOT allocate or lock. We mirror the
    // AutomationManager pattern: a juce::SpinLock guards the point vector,
    // the audio-thread read uses `tryEnter()` and returns a safe default
    // (1.0 — i.e. "no envelope") if it can't acquire, and the message-thread
    // writer takes the lock with a blocking ScopedLockType. A blocked read is
    // extremely rare (writes happen only on clip rebuild/point edit) and the
    // fallback is the previous-sample gain, so the worst case is one block
    // of slightly stale envelope — never a dropout or a crash.
public:
    struct GainPoint { double time; double gain; };
    void setGainEnvelopePoints(const std::vector<GainPoint>& points)
    {
        // Sort by time so the read path can binary-search. The audio-thread
        // reader assumes strictly increasing point times.
        std::vector<GainPoint> sorted = points;
        std::sort(sorted.begin(), sorted.end(),
                  [](const GainPoint& a, const GainPoint& b) { return a.time < b.time; });

        const juce::SpinLock::ScopedLockType lock(envelopeLock);
        gainEnvelopePoints = std::move(sorted);
        hasEnvelope.store(!gainEnvelopePoints.empty(), std::memory_order_release);
    }

    double getGainAtTime(double time) const
    {
        // Fast path: no envelope → unity gain, no lock at all.
        // (Safe because hasEnvelope is written under envelopeLock before any
        // swap that could empty the vector, and we tolerate reading a stale
        // bool here — worst case we take the lock and re-check.)
        if (!hasEnvelope.load(std::memory_order_acquire))
            return 1.0;

        if (!envelopeLock.tryEnter())
            return 1.0; // blocked: skip this block's envelope, RT-safe

        double result = 1.0;
        if (!gainEnvelopePoints.empty())
        {
            // lower_bound gives O(log n) lookup; the vector is sorted by time.
            auto it = std::lower_bound(gainEnvelopePoints.begin(),
                                       gainEnvelopePoints.end(), time,
                                       [](const GainPoint& p, double t) { return p.time < t; });
            if (it == gainEnvelopePoints.end())
                result = gainEnvelopePoints.back().gain;
            else if (it == gainEnvelopePoints.begin())
                result = gainEnvelopePoints.front().gain;
            else
            {
                const auto& a = *(it - 1);
                const auto& b = *it;
                double denom = b.time - a.time;
                // Guard against duplicate-time points (0/0 NaN). Treat a
                // zero-width segment as a step: hold the earlier point's gain.
                if (denom <= 0.0)
                    result = a.gain;
                else
                {
                    double alpha = (time - a.time) / denom;
                    result = a.gain + alpha * (b.gain - a.gain);
                }
            }
        }
        envelopeLock.exit();
        return result;
    }

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

    juce::HeapBlock<float> preloadedData[2];
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

    // Gain envelope support. See setGainEnvelopePoints for the RT contract.
    // gainEnvelopePoints is mutated only under envelopeLock; the audio-thread
    // reader acquires it with tryEnter(). hasEnvelope is an atomic fast-path
    // gate so the common "no envelope" case takes no lock at all.
    std::vector<GainPoint> gainEnvelopePoints;
    mutable juce::SpinLock envelopeLock;
    std::atomic<bool> hasEnvelope{ false };

    juce::LinearSmoothedValue<float> gainSmooth;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipSourceProcessor)
};

} // namespace HDAW