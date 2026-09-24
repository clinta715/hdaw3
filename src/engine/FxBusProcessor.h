#pragma once
#include "BusProcessorBase.h"
#include "InternalDelay.h"
#include "InternalFilter.h"
#include "../common/BusFxDefs.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace HDAW {

// FX return bus: one hardcoded DSP chain per fxType (reverb / delay / eq /
// compressor / filter), now PARAMETERIZED — the defs live in common/BusFxDefs.h
// and the values in a lock-free std::atomic<float> array (the
// MasterBusProcessor pattern), so the command thread never takes a lock the
// audio thread holds.
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
    // Room for every bus type's def list (delay is now the longest: 6).
    static constexpr int kMaxParams = 8;

    FxBusProcessor(const juce::String& name = "FX",
                   const juce::String& fxType = "reverb")
        : BusProcessorBase(name,
                           juce::AudioChannelSet::stereo(),
                           juce::AudioChannelSet::stereo()),
          currentFxType(fxType)
    {
        activeDefs.store(&busFxParamDefs(currentFxType), std::memory_order_release);
        resetParamsToDefaults();
    }

    ~FxBusProcessor() override = default;

    void setFxType(const juce::String& type)
    {
        // Gate 13: currentFxType (juce::String), the activeDefs pointer swap
        // and the DSP recreation below all happen under dspStateLock — the
        // audio-thread readers (getParam / setAutomationValue / the dirty
        // consume in processBlock) never take it, they read the atomic
        // activeDefs only. CriticalSection is re-entrant, so resetFxChain
        // taking the same lock below cannot self-deadlock.
        juce::ScopedLock lock(dspStateLock);
        if (type != currentFxType)
        {
            currentFxType = type;
            activeDefs.store(&busFxParamDefs(type), std::memory_order_release);
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
        const auto& defs = currentDefs();
        if (paramIndex < 0 || paramIndex >= (int) defs.size()) return;
        value = juce::jlimit(defs[(size_t) paramIndex].min, defs[(size_t) paramIndex].max, value);
        params[(size_t) paramIndex].store(value, std::memory_order_relaxed);
        // Gate 13 (control-thread write path): the DSP push runs under
        // dspStateLock — prepareToPlay/resetFxChain may be recreating (and
        // freeing) the DSP objects on the pump thread. A writer that cannot
        // enter keeps the atomic (already stored above) and flags the param
        // instead; processBlock's consume pass applies the flag, and
        // resetFxChain re-applies every atomic after recreation. The
        // store-then-flag / clear-then-read ordering closes the skip window
        // (no lost update).
        if (dspStateLock.tryEnter())
        {
            applyParamToDsp(paramIndex, value);
            dspStateLock.exit();
        }
        else
        {
            markAutomationDirty(paramIndex);
        }
    }

    // AUDIO-THREAD automation/modulation write — the pid 3000 + busID*8 +
    // paramIndex entry point, called from Track::processBlock's lane-apply and
    // LFO decode sites (the record site reads via getParam). Real-unit value:
    // a bus lane rides the def's own units (eq Frequency is 20..20000 Hz),
    // unlike TrackFXSlot's normalized automation entry. Clamp to the def
    // range, atomic store, set the DSP-dirty flag — NOTHING else: no lock, no
    // juce::String, no DSP member write on the audio thread (Gate 3); the DSP
    // push happens in processBlock under dspStateLock.tryEnter() (Gate 13).
    void setAutomationValue(int paramIndex, float value)
    {
        const auto& defs = currentDefs();
        if (paramIndex < 0 || paramIndex >= (int) defs.size()) return;
        value = juce::jlimit(defs[(size_t) paramIndex].min, defs[(size_t) paramIndex].max, value);
        params[(size_t) paramIndex].store(value, std::memory_order_relaxed);
        markAutomationDirty(paramIndex);
    }

    float getParam(int paramIndex) const
    {
        // Any-thread safe: bounds come from the atomic defs pointer — the
        // automation record and LFO base reads run on the audio thread and
        // must never touch the juce::String currentFxType (Gate 13).
        const auto& defs = currentDefs();
        if (paramIndex < 0 || paramIndex >= (int) defs.size()) return 0.0f;
        return params[(size_t) paramIndex].load(std::memory_order_relaxed);
    }

    // The defs THIS bus honors (empty for an unknown fxType), so a readback
    // can never advertise a param the DSP ignores (G5).
    const std::vector<BusFxParamDef>& paramDefs() const { return currentDefs(); }

    // Gate 1 restore helper: apply a BUS tree node (param_<i> properties) onto
    // the live processor. Called by RoutingManager::addBus right after
    // construction and by the rebuild path. Clamps every value (lesson 23).
    void applyFromTree(const juce::ValueTree& busNode)
    {
        if (! busNode.isValid()) return;
        resetParamsToDefaults();
        const auto& defs = currentDefs();
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
        // Gate 13: spec + scratch sizing + the DSP recreation inside
        // resetFxChain run under dspStateLock — applyParamToDsp (control
        // writers and the processBlock dirty consume) reads spec and the DSP
        // objects concurrently. Re-entrant, so resetFxChain below can take it
        // again; the audio thread only ever tryEnters this lock.
        juce::ScopedLock lock(dspStateLock);
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
        const int numSamples = buffer.getNumSamples();
        const int numChannels = juce::jmin(2, buffer.getNumChannels());

        // Fail safe when this bus is not prepared for this block size. A bus
        // created after the graph was last prepared is never prepared itself
        // (MainAudioProcessor::rebuildRoutingGraph only re-prepares when
        // getSampleRate() > 0), and releaseResources() shrinks the scratch buffer
        // to 1x1 - processing then would index the scratch buffer AND the delay
        // line out of bounds (measured 2026-09-23: channel-1 inf, then an access
        // violation). Pass the input through untouched instead of corrupting
        // memory; the next prepareToPlay builds the chain properly.
        if (scratchBuffer.getNumChannels() < 2 || scratchBuffer.getNumSamples() < numSamples)
            return;

        juce::ScopedNoDenormals noDenormals;

        // Consume DSP-dirty params flagged by audio-thread automation writes
        // (setAutomationValue) and by control writers that lost
        // dspStateLock.tryEnter(). tryEnter-or-skip — the audio thread never
        // blocks: when the lock is busy (prepareToPlay/resetFxChain recreating
        // the DSP objects under us, Gate 13) the push is skipped and the bits
        // stay set for the next block, and exchange() keeps bits re-set
        // concurrently too. The push itself is allocation-free — the EQ
        // coefficients go through ArrayCoefficients + array assignment, not
        // the allocating Coefficients wrapper (Gate 3).
        if (dspDirty.load(std::memory_order_acquire) != 0 && dspStateLock.tryEnter())
        {
            const uint32_t dirty = dspDirty.exchange(0, std::memory_order_acq_rel);
            const auto& defs = currentDefs();
            for (int p = 0; p < (int) defs.size() && p < 32; ++p)
                if ((dirty & (1u << p)) != 0)
                    applyParamToDsp(p, params[(size_t) p].load(std::memory_order_relaxed));
            dspStateLock.exit();
        }

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
        if (filterEnabled)
        {
            // The same per-sample SVF solve the track slot's Filter case runs,
            // over the scratch block in place (no allocation, no lock).
            filterProcess.process(block);
        }

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

    // Defs for the CURRENT fx type, safe from ANY thread (see activeDefs):
    // the reference lands in one of busFxParamDefs' immortal static tables —
    // address-stable, never freed — so audio-thread readers never touch the
    // juce::String currentFxType. The null fallback only covers a theoretical
    // pre-constructor call (the constructor stores activeDefs first).
    const std::vector<BusFxParamDef>& currentDefs() const
    {
        auto* d = activeDefs.load(std::memory_order_acquire);
        return d != nullptr ? *d : busFxParamDefs(juce::String());
    }

    // Flag one param for the processBlock DSP push (store-then-flag; callers
    // have already stored the atomic value).
    void markAutomationDirty(int paramIndex)
    {
        if (paramIndex >= 0 && paramIndex < 32)
            dspDirty.fetch_or(1u << paramIndex, std::memory_order_release);
    }

    void resetParamsToDefaults()
    {
        for (int p = 0; p < kMaxParams; ++p)
            params[(size_t) p].store(0.0f, std::memory_order_relaxed);
        const auto& defs = currentDefs();
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
        // Gate 13: hold the dedicated lock across the whole recreation —
        // DSP objects are reset()/prepare()d (destroyed and rebuilt) here on
        // the pump/command thread while setParam writers and the processBlock
        // dirty consume may be pushing into them. Re-entrant: setFxType and
        // prepareToPlay already hold it when they call this. The audio thread
        // only ever tryEnters, so it never blocks on us.
        juce::ScopedLock lock(dspStateLock);
        reverbEnabled = false;
        delayEnabled = false;
        eqEnabled = false;
        compEnabled = false;
        filterEnabled = false;

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
        else if (currentFxType == "filter")
        {
            // The shared internal filter (InternalFilter.h), the same DSP a
            // track's internal filter slot runs — so a return can be
            // high-passed (Mode 1), which the peak-filter eq cannot express.
            // prepare() only sets the sample rate and computes coefficients; the
            // stored params are pushed by applyAllParamsToDsp() below.
            filterEnabled = true;
            filterProcess.prepare(spec.sampleRate);
        }

        // Clear the dirty flags BEFORE the re-apply (both under
        // dspStateLock): a writer whose atomic landed before the reads below
        // is covered by applyAllParamsToDsp; one that stores after sets a
        // fresh flag — store-then-flag / clear-then-read, no lost update.
        dspDirty.store(0, std::memory_order_release);
        // Defaults are overridden by whatever setParam/applyFromTree stored.
        applyAllParamsToDsp();
    }

    void applyAllParamsToDsp()
    {
        const auto& defs = currentDefs();
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
            // ArrayCoefficients + array assignment: the same coefficients the
            // allocating Coefficients::makePeakFilter wrapper is built from,
            // without the heap allocation (this runs on the audio thread, once
            // per dirty param per block).
            *eqProcess.state = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(
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
            // The delay's six params all reach the shared DSP (the def table
            // and InternalDelay::paramDefs() are the same table): Delay Time /
            // Feedback / Mix / SyncToTempo / Division / Damping (index 5).
            // InternalDelay clamps again at its own entry and re-derives when the
            // derived value moved, so a running return responds without a rebuild.
            delayProcess.setParam(paramIndex, value);
        }
        else if (currentFxType == "filter")
        {
            // Cutoff / Mode / Resonance — one push into the shared DSP (the def
            // table and InternalFilter::paramDefs() are the same table), which
            // clamps again at its own entry and recomputes the TPT coefficients
            // on every change. Mode is the high-pass/low-pass switch.
            filterProcess.setParam(paramIndex, value);
        }
    }

    juce::String currentFxType;
    // Atomic pointer to the CURRENT type's def table — one of busFxParamDefs'
    // immortal static vectors (address stable forever). Swapped with release
    // ordering under dspStateLock by setFxType; loaded with acquire by ANY-
    // thread readers (setParam / getParam / setAutomationValue / the
    // processBlock dirty consume) so they never race the juce::String write
    // (Gate 13). See currentDefs().
    std::atomic<const std::vector<BusFxParamDef>*> activeDefs{ nullptr };
    // DSP-recreation vs writer guard (Gate 13 / lesson 13): prepareToPlay /
    // resetFxChain / setFxType hold it while recreating DSP objects; control
    // writers take it with tryEnter-or-skip; processBlock only ever tryEnters
    // it — a blocking lock inside processBlock is forbidden (Gate 3).
    juce::CriticalSection dspStateLock;
    // Bit p set = params[p] is ahead of the DSP push. Set by
    // setAutomationValue (audio thread) and by setParam writers that lost
    // tryEnter (release); consumed by processBlock's dirty pass (acquire +
    // exchange); cleared by resetFxChain before its re-apply — the
    // store-then-flag / clear-then-read pairing loses no update.
    std::atomic<uint32_t> dspDirty{ 0 };
    juce::AudioBuffer<float> scratchBuffer;
    juce::dsp::ProcessSpec spec;

    std::array<std::atomic<float>, (size_t) kMaxParams> params;

    std::atomic<bool> reverbEnabled{ false };
    std::atomic<bool> delayEnabled{ false };
    std::atomic<bool> eqEnabled{ false };
    std::atomic<bool> compEnabled{ false };
    std::atomic<bool> filterEnabled{ false };

    juce::dsp::Reverb reverbProcess;
    // The shared internal delay DSP (InternalDelay.h) — feedback, mix and
    // tempo-synced division, not the bare line this bus used to have.
    InternalDelay delayProcess;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> eqProcess;
    juce::dsp::Compressor<float> compProcess;
    // The shared internal filter DSP (InternalFilter.h) — the same SVF a track's
    // internal filter slot runs, so a return can be high-passed.
    InternalFilter filterProcess;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxBusProcessor)
};

} // namespace HDAW
