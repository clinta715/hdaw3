#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SidechainBus + SidechainTapProcessor (TRACK-FX-SLOT COMPRESSOR SIDECHAIN v1)
//
// A track's audio output can drive the detector of an internal `compressor`
// FX slot on ANOTHER track. RoutingManager places a SidechainTapProcessor
// node between the source track and the destination track:
//
//   sourceTrack ──┬── tap ──(digital silence)──> destTrack   (main path)
//                 └── writes level-gained frames ──> SidechainBus
//
// The SAME SidechainBus (std::shared_ptr) is held by the tap node and the
// destination TrackFXSlot. The tap writes its input (after the bus level
// gain) into the bus; the destination slot reads it in TrackFXSlot::process
// and feeds the SidechainCompressor's external envelope.
//
// The tap's output is DIGITAL SILENCE by design: RoutingManager connects
// tap → destTrack, and that connection is the graph dependency edge that
// guarantees the topological order source → tap → dest within ONE graph
// render block (the bus write happens before the dest slot reads it). The
// zeros sum +0.0f into the destination's main signal — bit-safe
// (SilenceEdgeSumsExactly test pins this).
//
// AUDIO-THREAD SAFETY (Gate 3): the bus is a fixed-size plain array plus
// atomics — no heap allocation, no locks after construction. Both endpoints
// cache a raw pointer for the audio path; no shared_ptr copies on the audio
// thread.
// ─────────────────────────────────────────────────────────────────────────────

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>

namespace HDAW {

class SidechainBus
{
public:
    // Largest block the bus can carry. capacity is clamped to this in
    // prepare(); tap writes and slot reads are bounded by it (Gate 9).
    static constexpr int maxBlock = 4096;

    float data[2][maxBlock] {};

    std::atomic<float> level { 1.0f };   // 0..1 tap gain into the bus
    std::atomic<bool> enabled { true };  // off = slot ignores the bus entirely
    std::atomic<int> framesWritten { 0 };

    int capacity = maxBlock;

    void prepare(int maxBlockSize)
    {
        capacity = juce::jlimit(1, maxBlock, maxBlockSize);
    }
};

class SidechainTapProcessor : public juce::AudioProcessor
{
public:
    SidechainTapProcessor()
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
    }

    ~SidechainTapProcessor() override = default;

    // Called on the message thread by RoutingManager while building the node
    // (before the node can ever process). Stores the shared handle; the audio
    // path below uses a raw cached pointer only.
    void setBus(std::shared_ptr<SidechainBus> newBus) { bus = std::move(newBus); }
    SidechainBus* getBus() const { return bus.get(); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        juce::ignoreUnused(sampleRate);
        if (auto* b = bus.get())
            b->prepare(samplesPerBlock);
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // 1. Publish level-gained input frames into the bus (bounded to its
        //    capacity), THEN
        // 2. overwrite the output with DIGITAL SILENCE — load-bearing, see the
        //    header comment (graph dependency edge + bit-safe sum).
        if (auto* b = bus.get())
        {
            if (b->enabled.load(std::memory_order_relaxed))
            {
                const int frames = juce::jmin(buffer.getNumSamples(), b->capacity);
                const float gain = b->level.load(std::memory_order_relaxed);
                const int numSrcChannels = juce::jmin(2, buffer.getNumChannels());
                for (int ch = 0; ch < numSrcChannels; ++ch)
                {
                    const auto* src = buffer.getReadPointer(ch);
                    auto* dst = b->data[ch];
                    for (int s = 0; s < frames; ++s)
                        dst[s] = src[s] * gain;
                }
                b->framesWritten.store(frames, std::memory_order_relaxed);
            }
        }
        buffer.clear();
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "SidechainTap"; }
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
    std::shared_ptr<SidechainBus> bus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidechainTapProcessor)
};

} // namespace HDAW
