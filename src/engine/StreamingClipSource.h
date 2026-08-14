#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace HDAW {

// Background double-buffer reader for long audio clips. Replaces whole-file
// preload: a short file (<= kPromoteToWholeFileMs) is read fully into memory
// (promoted); a long file streams through a fixed-size std::int16_t sliding window
// filled by a background std::thread. The audio thread never blocks, never
// allocates, and performs no I/O in realtime mode â€” if the requested window
// is not covered, it outputs silence and requests a swap.
class StreamingClipSource
{
public:
    static constexpr int kBufferSeconds = 4;
    static constexpr int kPromoteToWholeFileMs = 8000;

    StreamingClipSource() = default;
    ~StreamingClipSource() { stopPlayback(); }

    StreamingClipSource(const StreamingClipSource&) = delete;
    StreamingClipSource& operator=(const StreamingClipSource&) = delete;

    // Message thread. Opens the reader, reads metadata, and either promotes a
    // short file to whole-file residency or allocates the double-buffer sides.
    void prepare(const juce::File& file, juce::AudioFormatManager& fm,
                 double sampleRate, int samplesPerBlock)
    {
        stopPlayback();

        ok_ = false;
        promoted_ = false;
        numChannels_ = 0;
        sourceLength_ = 0;
        wholeFile_.data[0].reset();
        wholeFile_.data[1].reset();
        wholeFile_.channels = 0;
        wholeFile_.length = 0;
        bufA_.clear();
        bufB_.clear();
        bufLen_ = 0;
        baseA_.store(0);
        baseB_.store(0);
        fillA_.store(0);
        fillB_.store(0);
        targetPos_.store(0);
        activeBuffer_.store(0);
        swapRequest_.store(false);
        starvedCount_.store(0);

        sampleRate_ = sampleRate;
        blockSize_ = samplesPerBlock;

reader_ = std::unique_ptr<juce::AudioFormatReader>(
            fm.createReaderFor(file));
        if (reader_ == nullptr)
            return;

        numChannels_ = juce::jmin(static_cast<int>(reader_->numChannels), 2);
        sourceLength_ = reader_->lengthInSamples;
        const double durationSec = static_cast<double>(sourceLength_) / sampleRate_;

        if (durationSec * 1000.0 <= kPromoteToWholeFileMs)
        {
            // Promote: whole-file residency in std::int16_t.
            if (sourceLength_ > 0 && numChannels_ > 0)
            {
                wholeFile_.channels = numChannels_;
                wholeFile_.length = sourceLength_;
                wholeFile_.data[0].reset(new std::int16_t[sourceLength_]);
                wholeFile_.data[1].reset(new std::int16_t[sourceLength_]);
                fillWholeFile();
                promoted_ = true;
            }
        }
        else
        {
            // Stream: fixed std::int16_t double buffer.
            bufLen_ = static_cast<int64_t>(kBufferSeconds) * static_cast<int64_t>(sampleRate_);
            windowLen_ = bufLen_ + blockSize_;
            if (bufLen_ > 0)
            {
                const size_t sideSize = static_cast<size_t>(windowLen_) * static_cast<size_t>(numChannels_);
                bufA_.assign(sideSize, 0);
                bufB_.assign(sideSize, 0);
            }
        }

        ok_ = true;
    }

    bool isOk() const { return ok_; }
    bool isWholeFileResident() const { return promoted_; }
    int64_t sourceLength() const { return sourceLength_; }
    int numChannels() const { return numChannels_; }

    // Message thread. When non-realtime (export), the worker is stopped and
    // refills happen synchronously inside readNextBlock.
    void setNonRealtime(bool nr)
    {
        if (nr)
        {
            running_.store(false);
            if (worker_.joinable())
                worker_.join();
            nonRealtime_.store(true);
        }
        else
        {
            nonRealtime_.store(false);
        }
    }

    // Message thread. Fills the preload head synchronously, then (realtime
    // mode only) starts the background reader thread.
    void startPlayback()
    {
        if (!ok_ || promoted_ || bufLen_ <= 0)
            return;

        swapRequest_.store(false);
        targetPos_.store(0);
        fillBuffer(0, 0);
        activeBuffer_.store(0, std::memory_order_release);

        if (nonRealtime_.load())
            return;

        running_.store(true);
        worker_ = std::thread(&StreamingClipSource::readerLoop, this);
    }

    // Message thread. Stops and joins the background reader.
    void stopPlayback()
    {
        running_.store(false);
        if (worker_.joinable())
            worker_.join();
    }

    // AUDIO THREAD (realtime) / export thread (non-realtime). Never blocks,
    // never allocates, no I/O in realtime mode.
    void readNextBlock(juce::AudioBuffer<float>& out, int64_t sourcePos)
    {
        out.clear();

        if (!ok_)
            return;

        const int numSamples = out.getNumSamples();

        if (promoted_)
        {
            copyPromoted(out, sourcePos, numSamples);
            return;
        }

        if (numChannels_ <= 0 || bufLen_ <= 0)
            return;

        const int which = activeBuffer_.load(std::memory_order_acquire);
        int64_t base = (which == 0) ? baseA_.load(std::memory_order_acquire)
                                    : baseB_.load(std::memory_order_acquire);
        int64_t fill = (which == 0) ? fillA_.load(std::memory_order_acquire)
                                    : fillB_.load(std::memory_order_acquire);

        const bool covered = sourcePos >= base
                          && sourcePos + numSamples <= base + fill;

        if (!covered)
        {
            if (nonRealtime_.load())
            {
                // Synchronous refill for offline export. Reload base/fill:
                // the local copies above predate the refill.
                fillBuffer(which, sourcePos);
                activeBuffer_.store(which, std::memory_order_release);
                const int64_t newBase = (which == 0) ? baseA_.load(std::memory_order_acquire)
                                                     : baseB_.load(std::memory_order_acquire);
                const int64_t newFill = (which == 0) ? fillA_.load(std::memory_order_acquire)
                                                     : fillB_.load(std::memory_order_acquire);
                if (newBase != base || newFill != fill)
                {
                    base = newBase;
                    fill = newFill;
                }
            }
            else
            {
                targetPos_.store(sourcePos);
                swapRequest_.store(true, std::memory_order_release);
                starvedCount_.fetch_add(1, std::memory_order_relaxed);
                return; // silence this block
            }
        }

        const std::int16_t* active = (which == 0) ? bufA_.data() : bufB_.data();
        const int64_t off = sourcePos - base;
        const int numCh = numChannels_;
        const int channels = out.getNumChannels();

        for (int s = 0; s < numSamples; ++s)
        {
            if (off + s < fill)
            {
                for (int ch = 0; ch < channels; ++ch)
                {
                    const int srcCh = (numCh > 1) ? juce::jmin(ch, numCh - 1) : 0;
                    const std::int16_t v = active[static_cast<size_t>(off + s) * numCh + srcCh];
                    out.setSample(ch, s, static_cast<float>(v) / 32768.0f);
                }
            }
        }
        // Lookahead: request the next window once the audio thread has consumed
        // half the current window. This gives the reader ~half a window of real
        // time to fill + swap before the edge is crossed, so the boundary block
        // is never starved. The new window starts at the CURRENT audio position,
        // so it still covers the crossing point even if the swap lands late.
        if (!nonRealtime_.load() && sourcePos - base >= windowLen_ / 2)
        {
            targetPos_.store(sourcePos);
            swapRequest_.store(true, std::memory_order_release);
        }
    }

    float getDiskUsagePercent() const
    {
        if (promoted_)
            return 1.0f;
        if (bufLen_ <= 0 || windowLen_ <= 0)
            return 0.0f;
        const int which = activeBuffer_.load(std::memory_order_acquire);
        const int64_t fill = (which == 0) ? fillA_.load(std::memory_order_acquire)
                                          : fillB_.load(std::memory_order_acquire);
        return static_cast<float>(fill) / static_cast<float>(windowLen_);
    }

    uint64_t starvedCount() const { return starvedCount_.load(std::memory_order_relaxed); }

private:
    void fillWholeFile()
    {
        juce::AudioBuffer<float> scratch(wholeFile_.channels,
                                         static_cast<int>(wholeFile_.length));
        scratch.clear();
        float* ptrs[2] = { scratch.getWritePointer(0),
                           wholeFile_.channels > 1 ? scratch.getWritePointer(1) : nullptr };
        reader_->read(ptrs, wholeFile_.channels, 0,
                      static_cast<int>(wholeFile_.length));
        for (int64_t i = 0; i < wholeFile_.length; ++i)
        {
            for (int ch = 0; ch < wholeFile_.channels; ++ch)
            {
                const float f = scratch.getSample(ch, static_cast<int>(i));
                wholeFile_.data[ch][i] = static_cast<std::int16_t>(
                    juce::jlimit(-32768.0f, 32767.0f, f * 32768.0f));
            }
        }
    }

    void copyPromoted(juce::AudioBuffer<float>& out, int64_t sourcePos, int numSamples)
    {
        const int channels = out.getNumChannels();
        const int wholeCh = wholeFile_.channels;
        for (int s = 0; s < numSamples; ++s)
        {
            const int64_t idx = sourcePos + s;
            if (idx < 0 || idx >= wholeFile_.length)
                continue;
            for (int ch = 0; ch < channels; ++ch)
            {
                const int srcCh = (wholeCh > 1) ? juce::jmin(ch, wholeCh - 1) : 0;
                const std::int16_t v = wholeFile_.data[srcCh][idx];
                out.setSample(ch, s, static_cast<float>(v) / 32768.0f);
            }
        }
    }

    // Fills side `which` with a contiguous window [base, base+fill) in std::int16_t.
    // Publishes fill then base with release so the audio thread's acquire load
    // of base/fill sees the buffer contents.
    void fillBuffer(int which, int64_t base)
    {
        if (bufLen_ <= 0 || numChannels_ <= 0)
            return;

        std::int16_t* dest = (which == 0) ? bufA_.data() : bufB_.data();
        const int64_t available = (std::max)(static_cast<int64_t>(0),
            (std::min)(sourceLength_ - base, windowLen_));

        if (available > 0)
        {
            juce::AudioBuffer<float> scratch(numChannels_,
                                             static_cast<int>(available));
            scratch.clear();
            float* ptrs[2] = { scratch.getWritePointer(0),
                               numChannels_ > 1 ? scratch.getWritePointer(1) : nullptr };
            reader_->read(ptrs, numChannels_, base, static_cast<int>(available));
            for (int64_t i = 0; i < available; ++i)
            {
                for (int ch = 0; ch < numChannels_; ++ch)
                {
                    const float f = scratch.getSample(ch, static_cast<int>(i));
                    dest[static_cast<size_t>(i) * numChannels_ + ch] =
                        static_cast<std::int16_t>(juce::jlimit(-32768.0f, 32767.0f, f * 32768.0f));
                }
            }
        }

        // Zero the tail so the audio thread's off+s < fill guard sees silence.
        for (int64_t i = available; i < windowLen_; ++i)
        {
            for (int ch = 0; ch < numChannels_; ++ch)
                dest[static_cast<size_t>(i) * numChannels_ + ch] = 0;
        }

        if (which == 0)
        {
            fillA_.store(available, std::memory_order_release);
            baseA_.store(base, std::memory_order_release);
        }
        else
        {
            fillB_.store(available, std::memory_order_release);
            baseB_.store(base, std::memory_order_release);
        }
    }

    void readerLoop()
    {
        while (running_.load(std::memory_order_relaxed))
        {
            if (!swapRequest_.load(std::memory_order_acquire))
            {
                juce::Thread::sleep(2);
                continue;
            }

            swapRequest_.store(false, std::memory_order_relaxed);
            const int other = (activeBuffer_.load(std::memory_order_acquire) == 0) ? 1 : 0;
            const int64_t pos = targetPos_.load(std::memory_order_relaxed);
            fillBuffer(other, pos);
            activeBuffer_.store(other, std::memory_order_release);
        }
    }

    std::unique_ptr<juce::AudioFormatReader> reader_;
    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    bool ok_ = false;
    bool promoted_ = false;
    int numChannels_ = 0;
    int64_t sourceLength_ = 0;

    struct WholeFile
    {
        std::unique_ptr<std::int16_t[]> data[2];
        int channels = 0;
        int64_t length = 0;
    } wholeFile_;

    std::vector<std::int16_t> bufA_, bufB_;
    int64_t bufLen_ = 0;
    int64_t windowLen_ = 0;

    std::atomic<int64_t> baseA_{0}, baseB_{0};
    std::atomic<int64_t> fillA_{0}, fillB_{0};
    std::atomic<int64_t> targetPos_{0};
    std::atomic<int> activeBuffer_{0};
    std::atomic<bool> swapRequest_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> nonRealtime_{false};
    std::atomic<uint64_t> starvedCount_{0};

    std::thread worker_;
};

} // namespace HDAW
