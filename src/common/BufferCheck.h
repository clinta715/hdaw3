#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>
#include <limits>

namespace HDAW {

// Debug-only realtime buffer sanity checks. On the audio thread we ONLY
// set atomics — never allocate, lock, log, or format. A message thread
// drains the flags and reports.
class BufferCheck
{
public:
    static void checkBuffer(const juce::AudioBuffer<float>& buffer,
                            double sampleRate, int contextId)
    {
#if JUCE_DEBUG
        const uint32_t blockStart = juce::Time::getMillisecondCounterHiRes();

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        for (int ch = 0; ch < numChannels && problemFlags_.load(std::memory_order_relaxed) == 0; ++ch)
        {
            const float* d = buffer.getReadPointer(ch);
            for (int s = 0; s < numSamples; ++s)
            {
                const float v = d[s];
                if (!std::isfinite(v)) // NaN or ±Inf
                {
                    problemFlags_.fetch_or(kProblemNonFinite, std::memory_order_relaxed);
                    lastContext_.store(contextId, std::memory_order_relaxed);
                    lastChannel_.store(ch, std::memory_order_relaxed);
                    lastSample_.store(s, std::memory_order_relaxed);
                    break;
                }
            }
        }

        // DC-offset drift: per-block mean of channel 0. A sustained offset
        // (mean > 0.25) trips; a tone has block mean ~0 so it never trips.
        if (problemFlags_.load(std::memory_order_relaxed) == 0
            && numSamples > 0 && sampleRate > 0.0)
        {
            double blockSum = 0.0;
            const float* d0 = buffer.getReadPointer(0);
            for (int s = 0; s < numSamples; ++s)
                blockSum += d0[s];
            const double blockMean = blockSum / numSamples;
            if (std::fabs(blockMean) > 0.25)
            {
                problemFlags_.fetch_or(kProblemDC, std::memory_order_relaxed);
                lastContext_.store(contextId, std::memory_order_relaxed);
            }
        }

        // Glitch detector: any single block taking more than 4x its nominal
        // duration indicates a priority inversion / overrun. The 4x threshold
        // is floored at 100 ms so scheduler-timeslice noise can't false-positive
        // while genuine overruns (typically hundreds of ms) are still caught.
        if (problemFlags_.load(std::memory_order_relaxed) == 0)
        {
            const uint32_t blockElapsed =
                juce::Time::getMillisecondCounterHiRes() - blockStart;
            const uint32_t nominalMs = static_cast<uint32_t>(
                1000.0 * static_cast<double>(numSamples) / (sampleRate > 0.0 ? sampleRate : 1.0));
            const uint32_t glitchThresholdMs = (nominalMs * 4) > 100 ? (nominalMs * 4) : 100;
            if (nominalMs > 0 && blockElapsed > glitchThresholdMs)
            {
                problemFlags_.fetch_or(kProblemGlitch, std::memory_order_relaxed);
                lastContext_.store(contextId, std::memory_order_relaxed);
            }
        }
#endif // JUCE_DEBUG
    }

    static bool anyProblemPending() noexcept
    {
        return problemFlags_.load(std::memory_order_acquire) != 0;
    }

    static juce::String drainProblem()
    {
        const int flags = problemFlags_.exchange(0, std::memory_order_acq_rel);
        if (flags == 0)
            return {};
        juce::String desc = "RT:";
        if (flags & kProblemNonFinite)
            desc += " non-finite sample (NaN/Inf)";
        if (flags & kProblemDC)
            desc += " DC-offset drift";
        if (flags & kProblemGlitch)
            desc += " block overrun (4x)";
        desc += " ctx=" + juce::String(lastContext_.load(std::memory_order_relaxed))
              + " ch=" + juce::String(lastChannel_.load(std::memory_order_relaxed))
              + " sample=" + juce::String(lastSample_.load(std::memory_order_relaxed));
        return desc;
    }

    static void resetForTest() noexcept
    {
        problemFlags_.store(0, std::memory_order_release);
        lastContext_.store(-1, std::memory_order_release);
        lastChannel_.store(-1, std::memory_order_release);
        lastSample_.store(-1, std::memory_order_release);
    }

private:
    enum : int
    {
        kProblemNonFinite = 1 << 0,
        kProblemDC        = 1 << 1,
        kProblemGlitch    = 1 << 2
    };
    inline static std::atomic<int> problemFlags_{ 0 };
    inline static std::atomic<int> lastContext_{ -1 };
    inline static std::atomic<int> lastChannel_{ -1 };
    inline static std::atomic<int> lastSample_{ -1 };
};

} // namespace HDAW
