#pragma once
#include "BusProcessorBase.h"
#include "InternalDelay.h"
#include "../common/BusFxDefs.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace HDAW {

// FX return bus: one hardcoded DSP chain per fxType (reverb / delay / eq /
// compressor), now PARAMETERIZED — the defs live in common/BusFxDefs.h and the
// values in a lock-free std::atomic<float> array (the MasterBusProcessor
// pattern), so the command thread never takes a lock the audio thread holds.
//
// Persistence is the BUS node's param_<i> property (the same grammar
// MasterBusProcessor uses on MASTER_FX slots), which is why the values are
// re-applied by resetFxChain() — prepareToPlay re-runs it, and RoutingManager
// ::addBus restores them from the tree after construction (Gates 1/6/10).
//
// The delay return is NOT a bare juce::dsp::DelayLine any more: it runs the
// shared InternalDelay DSP (slice C3 of docs/plans/2026-09-22-bus-fx-params.md),
// the same object TrackFXSlot uses, so a send into a delay bus really repeats
// (Feedback), can be a pure wet return (Mix) and can lock to the project BPM
// (SyncToTempo/Division) — fed by the graph playhead, exactly like the track
// slots are fed by Track::processBlock.
class FxBusProcessor : public BusProcessorBase
{
public:
    // Room for every bus type's def list (reverb is the longest: 5).
    static constexpr int kMaxParams = 8;

    FxBusProcessor(const juce::String& name = "FX",
                   const juce::String& fxType = "reverb")
        : BusProcessorBase(name,
                           juce::AudioChannelSet::stereo(),
                           juce::AudioChannelSet::stereo()),
          currentFxType(fxType)
    {
        resetParamsToDefaults();
    }

    ~FxBusProcessor() override = default;

    void setFxType(const juce::String& type)
    {
        if (type != currentFxType)
        {
            currentFxType = type;
            // A different type means a different def list: start from ITS
            // defaults rather than carrying the previous type's values.
            resetParamsToDefaults();
        }
        resetFxChain();
    }

    const juce::String& getFxType() const { return currentFxType; }

    // Thread-safe: called from the message/command thread; the audio thread
    // only reads the atomics. Clamped to the param def (lesson 23) at this
    // entry point AND at the command write-side, then pushed into the live DSP
    // object (the same member-set pattern TrackFXSlot::applyInternalParamToDsp
    // uses for these exact types) so a running bus responds without a rebuild.
    void setParam(int paramIndex, float value)
    {
        const auto& defs = busFxParamDefs(currentFxType);
        if (paramIndex < 0 || paramIndex >= (int) defs.size()) return;
        value = juce::jlimit(defs[(size_t) paramIndex].min, defs[(size_t) paramIndex].max, value);
        params[(size_t) paramIndex].store(value, std::memory_order_relaxed);
        applyParamToDsp(paramIndex, value);
    }

    float getParam(int paramIndex) const
    {
        const auto& defs = busFxParamDefs(currentFxType);
        if (paramIndex < 0 || paramIndex >= (int) defs.size()) return 0.0f;
        return params[(size_t) paramIndex].load(std::memory_order_relaxed);
    }

    // The defs THIS bus honors (empty for an unknown fxType), so a readback
    // can never advertise a param the DSP ignores (G5).
    const std::vector<BusFxParamDef>& paramDefs() const { return busFxParamDefs(currentFxType); }

    // Gate 1 restore helper: apply a BUS tree node (param_<i> properties) onto
    // the live processor. Called by RoutingManager::addBus right after
    // construction and by the rebuild path. Clamps every value (lesson 23).
    void applyFromTree(const juce::ValueTree& busNode)
    {
        if (! busNode.isValid()) return;
        resetParamsToDefaults();
        const auto& defs = busFxParamDefs(currentFxType);
        for (int p = 0; p < (int) defs.size(); ++p)
        {
            const float raw = static_cast<float>(
                static_cast<double>(busNode.getProperty("param_" + juce::String(p),
                                                        (double) defs[(size_t) p].def)));
            setParam(p, raw);
        }
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        scratchBuffer.setSize(2, samplesPerBlock);

        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 2;

        resetFxChain();
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

        scratchBuffer.clear();
        for (int ch = 0; ch < numChannels; ++ch)
            scratchBuffer.addFrom(ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> block(scratchBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        if (reverbEnabled)
            reverbProcess.process(context);
        if (delayEnabled)
        {
            // Project BPM, read from the playhead exactly like
            // Track::processBlock feeds its internal FX slots (Track.cpp:584
            // `slot->setTempo(bpm)`); the fx bus is the bus-side equivalent and
            // is a graph node, so the same playhead reaches it — live AND in the
            // offline export (ExportManager installs an InternalPlayHead on the
            // render graph). Atomic store + a derived-time compare; no
            // allocation, no lock. Defaults to 120 while there is no playhead,
            // which is the same fallback the track slots use.
            delayProcess.setTempo(currentBpm());
            delayProcess.process(block);
        }
        if (eqEnabled)
            eqProcess.process(context);
        if (compEnabled)
            compProcess.process(context);

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom(ch, 0, scratchBuffer, ch, 0, numSamples);

        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        meter.update(buffer);
    }

private:
    // Project BPM from the graph playhead — the bus-side equivalent of the
    // tempo Track::processBlock hands its internal FX slots. Falls back to 120
    // with no playhead / no position / no bpm, exactly like the track path
    // (Track.cpp:535-538). No allocation: PositionInfo is plain POD. Called from
    // the audio thread once per block for a delay bus.
    double currentBpm() const
    {
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                return pos->getBpm().orFallback(120.0);
        return 120.0;
    }

    void resetParamsToDefaults()
    {
        for (int p = 0; p < kMaxParams; ++p)
            params[(size_t) p].store(0.0f, std::memory_order_relaxed);
        const auto& defs = busFxParamDefs(currentFxType);
        for (int p = 0; p < (int) defs.size(); ++p)
            params[(size_t) p].store(defs[(size_t) p].def, std::memory_order_relaxed);
    }

    // Rebuilds the chain for currentFxType and re-applies the stored params, so
    // a value survives prepareToPlay (which calls this) and setFxType. An
    // unprepared processor (spec.sampleRate == 0) only clears the enable flags:
    // juce's DSP prepare()s assert on a zero sample rate, and prepareToPlay
    // re-runs this with the real spec.
    void resetFxChain()
    {
        reverbEnabled = false;
        delayEnabled = false;
        eqEnabled = false;
        compEnabled = false;

        if (spec.sampleRate <= 0.0)
            return;

        if (currentFxType == "reverb")
        {
            reverbEnabled = true;
            reverbProcess.reset();
            reverbProcess.prepare(spec);
        }
        else if (currentFxType == "delay")
        {
            delayEnabled = true;
            // The shared internal delay (InternalDelay.h) sizes its line to the
            // Delay Time def's 5 s top before juce's prepare() allocates it —
            // that is why this must run under the real spec (an unprepared
            // processor returns above). It is the same DSP the track FX slots
            // use, so a return can repeat (feedback) and follow the tempo.
            delayProcess.prepare(spec);
        }
        else if (currentFxType == "eq")
        {
            eqEnabled = true;
            eqProcess.reset();
            eqProcess.prepare(spec);
        }
        else if (currentFxType == "compressor")
        {
            compEnabled = true;
            compProcess.reset();
            compProcess.prepare(spec);
        }

        // Defaults are overridden by whatever setParam/applyFromTree stored.
        applyAllParamsToDsp();
    }

    void applyAllParamsToDsp()
    {
        const auto& defs = busFxParamDefs(currentFxType);
        for (int p = 0; p < (int) defs.size(); ++p)
            applyParamToDsp(p, params[(size_t) p].load(std::memory_order_relaxed));
    }

    // Push one (already clamped) param into the live DSP object. Plain member
    // sets / parameter struct writes — no allocation, no lock, the benign-tear
    // class TrackFXSlot documents for the same objects. Values written before
    // prepare are kept in the atomics and applied by resetFxChain.
    void applyParamToDsp(int paramIndex, float value)
    {
        if (spec.sampleRate <= 0.0) return;      // not prepared yet

        if (currentFxType == "reverb")
        {
            auto p = reverbProcess.getParameters();
            switch (paramIndex)
            {
                case 0: p.roomSize = value; break;
                case 1: p.damping  = value; break;
                case 2: p.wetLevel = value; break;
                case 3: p.dryLevel = value; break;
                case 4: p.width    = value; break;
                default: return;
            }
            reverbProcess.setParameters(p);
        }
        else if (currentFxType == "eq")
        {
            // All three coefficients are reconstructed from the stored values
            // (dB -> linear, as TrackFXSlot does: passing raw dB as the linear
            // factor silenced the EQ at the default 0 dB gain).
            const float freq   = params[0].load(std::memory_order_relaxed);
            const float q      = params[1].load(std::memory_order_relaxed);
            const float gainDb = params[2].load(std::memory_order_relaxed);
            *eqProcess.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                spec.sampleRate, freq, q, juce::Decibels::decibelsToGain(gainDb));
        }
        else if (currentFxType == "compressor")
        {
            switch (paramIndex)
            {
                case 0: compProcess.setThreshold(value); break;
                case 1: compProcess.setRatio(value);     break;
                case 2: compProcess.setAttack(value);    break;
                case 3: compProcess.setRelease(value);   break;
                default: return;
            }
        }
        else if (currentFxType == "delay")
        {
            // The delay's five params all reach the shared DSP (the def table
            // and InternalDelay::paramDefs() are the same table): Delay Time /
            // Feedback / Mix / SyncToTempo / Division. InternalDelay clamps
            // again at its own entry and re-derives the delay time only when the
            // derived value moved, so a running return responds without a rebuild.
            delayProcess.setParam(paramIndex, value);
        }
    }

    juce::String currentFxType;
    juce::AudioBuffer<float> scratchBuffer;
    juce::dsp::ProcessSpec spec;

    std::array<std::atomic<float>, (size_t) kMaxParams> params;

    std::atomic<bool> reverbEnabled{ false };
    std::atomic<bool> delayEnabled{ false };
    std::atomic<bool> eqEnabled{ false };
    std::atomic<bool> compEnabled{ false };

    juce::dsp::Reverb reverbProcess;
    // The shared internal delay DSP (InternalDelay.h) — feedback, mix and
    // tempo-synced division, not the bare line this bus used to have.
    InternalDelay delayProcess;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> eqProcess;
    juce::dsp::Compressor<float> compProcess;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxBusProcessor)
};

} // namespace HDAW
