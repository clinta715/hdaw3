#pragma once
#include "BusProcessorBase.h"
#include "../common/BufferCheck.h"
#include "../common/MasterFxDefs.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <vector>

namespace HDAW {

// ── Master FX chain ───────────────────────────────────────────────────────
// Master-bus tone/loudness shaping ("increase volume by EQ/dynamics, not by
// the fader"): per-slot param defs (common/MasterFxDefs.h) + DSP for
// eq / compressor / limiter. Slots live as FX_SLOT-style children of the
// root MASTER_FX node; params are std::atomic<float> so the command thread
// never touches a lock the audio thread holds. EQ coefficients are
// recomputed per block from the atomics (a few tan/mul ops — cheaper and
// safer than a coefficient-swap dance; contrast TrackFXSlot's stateLock
// pattern, lesson 13).
class MasterBusProcessor : public BusProcessorBase
{
public:
    // Slot kinds — indexes match the default MASTER_FX slot order.
    static constexpr int kMaxSlots = 4;

    MasterBusProcessor()
        : BusProcessorBase("Master Bus",
                           juce::AudioChannelSet::stereo(),
                           juce::AudioChannelSet::stereo())
    {
        resetSlotsToDefaults();
    }

    ~MasterBusProcessor() override = default;

    // Slot type by index (0=eq, 1=limiter in the default stamp; read from the
    // tree on restore). Unknown/empty = bypassed no-op slot.
    void setSlotType(int slotIndex, const juce::String& fxType)
    {
        if (slotIndex < 0 || slotIndex >= kMaxSlots) return;
        slotTypes[(size_t) slotIndex] = fxType;
    }
    juce::String getSlotType(int slotIndex) const
    {
        if (slotIndex < 0 || slotIndex >= kMaxSlots) return {};
        return slotTypes[(size_t) slotIndex];
    }

    // Thread-safe: called from the message/command thread; the audio thread
    // only reads the atomics. Clamped to the param def (lesson 23) at this
    // entry point AND at the command write-side.
    void setSlotParam(int slotIndex, int paramIndex, float value)
    {
        if (slotIndex < 0 || slotIndex >= kMaxSlots) return;
        const auto& defs = masterFxParamDefs(slotTypes[(size_t) slotIndex]);
        if (paramIndex < 0 || paramIndex >= (int) defs.size()) return;
        value = juce::jlimit(defs[(size_t) paramIndex].min, defs[(size_t) paramIndex].max, value);
        slotParams[(size_t) slotIndex][(size_t) paramIndex].store(value, std::memory_order_relaxed);
    }
    float getSlotParam(int slotIndex, int paramIndex) const
    {
        if (slotIndex < 0 || slotIndex >= kMaxSlots) return 0.0f;
        const auto& defs = masterFxParamDefs(slotTypes[(size_t) slotIndex]);
        if (paramIndex < 0 || paramIndex >= (int) defs.size()) return 0.0f;
        return slotParams[(size_t) slotIndex][(size_t) paramIndex].load(std::memory_order_relaxed);
    }
    void setSlotBypassed(int slotIndex, bool bypassed)
    {
        if (slotIndex < 0 || slotIndex >= kMaxSlots) return;
        slotBypassed[(size_t) slotIndex].store(bypassed, std::memory_order_relaxed);
    }
    bool isSlotBypassed(int slotIndex) const
    {
        if (slotIndex < 0 || slotIndex >= kMaxSlots) return true;
        return slotBypassed[(size_t) slotIndex].load(std::memory_order_relaxed);
    }

    // Gate 1 restore helper: apply a whole MASTER_FX tree (children of type
    // FX_SLOT) onto the live slots. Called from the rebuild path and from
    // the listener for property changes. Clamps every value (lesson 23).
    void applyFromTree(const juce::ValueTree& masterFxNode)
    {
        if (! masterFxNode.isValid()) return;
        resetSlotsToDefaults();
        int slot = 0;
        for (int i = 0; i < masterFxNode.getNumChildren() && slot < kMaxSlots; ++i)
        {
            auto c = masterFxNode.getChild(i);
            if (! c.hasType("FX_SLOT")) continue;
            const juce::String fxType = c.getProperty("fxType", "").toString();
            const auto& defs = masterFxParamDefs(fxType);
            setSlotType(slot, fxType);
            for (int p = 0; p < (int) defs.size(); ++p)
            {
                const float raw = static_cast<float>(c.getProperty("param_" + juce::String(p),
                                                                    (double) defs[(size_t) p].def));
                setSlotParam(slot, p, raw);
            }
            setSlotBypassed(slot, static_cast<bool>(c.getProperty("bypassed", true)));
            ++slot;
        }
        // Slots beyond the tree children stay defaults (bypassed, default params).
    }

    // Thread-safe: called from the message/command thread (listener or
    // rebuild); the audio thread only reads the atomic.
    void setGain(float newGain) { gain.store(std::max(0.0f, newGain), std::memory_order_relaxed); }
    float getGain() const { return gain.load(std::memory_order_relaxed); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        currentSampleRate = sampleRate;
        scratchBuffer.setSize(2, samplesPerBlock);
        meter.setComputeLufs(true);
        meter.prepare(sampleRate, samplesPerBlock);
        gainSmooth.reset(sampleRate, 0.02);
        gainSmooth.setCurrentAndTargetValue(gain.load(std::memory_order_relaxed));

        // (Re)prepare slot DSP at the real rate. ScopedNoDenormals + fixed
        // allocations: nothing here is called on the audio thread except
        // processBlock, and juce dsp prepare()s are plain member sets.
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) std::max(1, samplesPerBlock), 2 };
        for (int i = 0; i < kMaxSlots; ++i)
            prepareSlot(i, spec);
    }

    void releaseResources() override
    {
        scratchBuffer.setSize(1, 1);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int numSamples = buffer.getNumSamples();
        const int numChannels = juce::jmin(2, buffer.getNumChannels());

        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        // ── Master FX chain (eq -> compressor -> limiter), lock-free ──
        // EQ coefficients are recomputed per block from the atomics; the
        // compressor/limiter read their member params directly (same benign
        // pattern as TrackFXSlot::applyInternalParamToDsp under stateLock).
        if (currentSampleRate > 0.0)
        {
            for (int i = 0; i < kMaxSlots; ++i)
            {
                if (slotBypassed[(size_t) i].load(std::memory_order_relaxed)) continue;
                const auto& type = slotTypes[(size_t) i];
                if (type == "eq")
                {
                    const float freq = slotParams[(size_t) i][0].load(std::memory_order_relaxed);
                    const float q    = slotParams[(size_t) i][1].load(std::memory_order_relaxed);
                    const float gDb  = slotParams[(size_t) i][2].load(std::memory_order_relaxed);
                    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                        currentSampleRate, freq, q, juce::Decibels::decibelsToGain(gDb));
                    // Stereo-linked via ProcessorDuplicator: identical shared
                    // coefficients on every channel (TrackFXSlot EQ pattern;
                    // note makePeakFilter takes a LINEAR gain factor, dB-converted
                    // here — 2026-08-27 pitfall).
                    *eq[(size_t) i].state = *coeffs;
                    juce::dsp::AudioBlock<float> block(buffer);
                    juce::dsp::ProcessContextReplacing<float> ctx(block);
                    eq[(size_t) i].process(ctx);
                }
                else if (type == "compressor")
                {
                    auto& c = compressor[(size_t) i];
                    c.setThreshold(slotParams[(size_t) i][0].load(std::memory_order_relaxed));
                    c.setRatio(slotParams[(size_t) i][1].load(std::memory_order_relaxed));
                    c.setAttack(slotParams[(size_t) i][2].load(std::memory_order_relaxed));
                    c.setRelease(slotParams[(size_t) i][3].load(std::memory_order_relaxed));
                    // Block-level process: juce's Compressor derives its
                    // envelope from the MAX abs across channels — stereo-linked
                    // by construction (same API TrackFXSlot uses).
                    juce::dsp::AudioBlock<float> block(buffer);
                    juce::dsp::ProcessContextReplacing<float> ctx(block);
                    c.process(ctx);
                }
                else if (type == "limiter")
                {
                    auto& l = limiter[(size_t) i];
                    l.setThreshold(slotParams[(size_t) i][0].load(std::memory_order_relaxed));
                    l.setRelease(slotParams[(size_t) i][1].load(std::memory_order_relaxed));
                    juce::dsp::AudioBlock<float> block(buffer);
                    juce::dsp::ProcessContextReplacing<float> ctx(block);
                    l.process(ctx);
                }
            }
        }

        // Realtime-safe gain: atomic written off-thread, smoothed on the
        // audio thread (ClipSourceProcessor.h:429 idiom). No alloc/lock.
        // getNextValue() returns the current value when not smoothing.
        gainSmooth.setTargetValue(gain.load(std::memory_order_relaxed));
        for (int s = 0; s < numSamples; ++s)
        {
            const float g = gainSmooth.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, s, buffer.getSample(ch, s) * g);
        }

        // Meter reads POST-gain (after FX, so the meter reflects the true
        // output level the limiter is holding at the ceiling).
        meter.update(buffer);

        HDAW::BufferCheck::checkBuffer(buffer, getSampleRate(), 0);
    }

private:
    void resetSlotsToDefaults()
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            slotTypes[(size_t) i] = {};
            slotBypassed[(size_t) i].store(true, std::memory_order_relaxed);
            for (int p = 0; p < 8; ++p)
                slotParams[(size_t) i][(size_t) p].store(0.0f, std::memory_order_relaxed);
        }
    }

    void prepareSlot(int i, const juce::dsp::ProcessSpec& spec)
    {
        eq[(size_t) i].prepare(spec);
        compressor[(size_t) i].prepare(spec);
        limiter[(size_t) i].prepare(spec);
    }

    juce::AudioBuffer<float> scratchBuffer;
    std::atomic<float> gain{1.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainSmooth;

    // ── Master FX state (lock-free) ──
    double currentSampleRate = 0.0;
    std::array<juce::String, (size_t) kMaxSlots> slotTypes;
    std::array<std::array<std::atomic<float>, 8>, (size_t) kMaxSlots> slotParams;
    std::array<std::atomic<bool>, (size_t) kMaxSlots> slotBypassed;
    // Per-slot DSP instances (prepared even when the slot type is unused —
    // switching a slot's type never needs a realloc on the audio thread).
    using MasterEq = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;
    std::array<MasterEq, (size_t) kMaxSlots> eq;
    std::array<juce::dsp::Compressor<float>, (size_t) kMaxSlots> compressor;
    std::array<juce::dsp::Limiter<float>, (size_t) kMaxSlots> limiter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterBusProcessor)
};

} // namespace HDAW
