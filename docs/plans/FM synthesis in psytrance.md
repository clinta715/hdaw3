FM synthesis is well-suited to psytrance because the genre leans on inharmonic, metallic, and "alive" timbres that subtractive synthesis struggles to produce cleanly. Here's how to think about programming it for that context.

## Core FM concept as it applies here

FM works by using one oscillator (the **modulator**) to modulate the frequency of another (the **carrier**) at audio rate. The **ratio** between modulator and carrier frequency, and the **modulation index** (depth), determine the harmonic content:

- **Integer ratios** (1:1, 1:2, 2:1, 1:3...) → harmonic, musical, bell-like or brassy tones
- **Non-integer ratios** (1:1.4, 1:2.7...) → inharmonic, metallic, clangorous tones — this is where a lot of psytrance's "alien" texture comes from
- **Higher modulation index** → more sidebands → brighter/noisier/more aggressive spectrum

## Applying this to specific psytrance roles

**Growl bass (from earlier)**
- Low ratios (1:1 to 1:4), moderate-to-high index, pushed until the waveform folds/distorts on its own before even hitting a separate waveshaper stage
- Often the modulator itself is enveloped — index decays slightly over the note so the growl "settles" rather than staying static

**Metallic/alien plucks and stabs**
- This is where FM really earns its keep over subtractive. Non-integer ratios (e.g., 1:3.14, 1:1.41) with a fast-decaying modulation envelope — the modulator index starts high (bright, clangy attack) and decays fast, leaving a purer tone as sustain. Classic "FM bell" behavior repurposed into something darker.
- Stack 2 modulators (2-op chain: carrier ← mod1 ← mod2) for more complex, evolving inharmonic spectra — each additional operator multiplies the sideband complexity.

**Acid-adjacent leads/screeches**
- FM can substitute for or layer with the self-oscillating filter approach — very high modulation index at audio rate produces filter-like resonant screams without needing a resonant filter at all. Sweep the index (not just the filter cutoff) with an envelope for a similar "opening up" effect.

**Risers/FX**
- Slow-moving modulator frequency (sub-audio rate, essentially LFO-speed) creates vibrato/wobble; crossing from sub-audio into audio-rate modulation as it speeds up produces the classic "riser that turns into a scream" effect — a nice trick since it's one parameter (mod rate) doing double duty across two perceptual domains.

## Programming approach (params to expose)

```cpp
struct FmVoiceParams
{
    float carrierFreqHz;
    float modRatio;           // ratio to carrier — non-integer for inharmonic psy tones
    float modIndex;           // depth — drives brightness/aggression
    float modIndexEnvAmount;  // how much the index envelope affects index over time
    ADSR  modIndexEnv;        // fast decay = plucky; slow = evolving/growly
    int   numOperators;       // 1 = simple FM, 2+ = chained for complexity
    float feedbackAmount;     // self-feedback on an operator — pushes toward noise/chaos at high values
};
```

**Feedback** is worth calling out specifically — routing an operator's output back into its own phase input (rather than modulating a separate carrier) is what Yamaha DX-style synths use to go from "clean FM tone" to "distorted, almost waveshaped noise" as feedback increases. It's a cheap way to get aggression without a separate distortion stage, and it interacts differently with the sound than adding a waveshaper afterward — feedback affects the harmonic generation itself, not just clips an already-formed waveform.

## Practical implementation note for a `juce::dsp` FM voice

A 2-operator FM voice is just:

```cpp
float modSample = modOsc.processSample(phaseInput) * modIndex;
carrierOsc.setFrequency(carrierBaseFreq + modSample * carrierBaseFreq); // or phase-mod variant
float out = carrierOsc.processSample(0.0f);
```

Phase modulation (modulating phase instead of frequency directly) is usually preferable in digital implementations — it avoids the frequency-domain artifacts/aliasing that pure frequency modulation can introduce at high indices, and it's what most software FM synths actually do under the hood despite calling it "FM."

Want this worked into a full FM voice class in the same style as the earlier `PsyArpVoice`/growl bass structs, so it slots into the same architecture?

That's a solid base to build psytrance FM patches on — the Android FM engine (originally derived from the same lineage as Dexed/DX7 emulation) gives you a proper 6-operator architecture with algorithm routing, which is more than enough for everything discussed above. A few thoughts on using it specifically for psy sounds rather than classic DX7 patches:

## What DX7-style architecture gives you that matters here

- **6 operators, selectable algorithms** — the DX7's 32 algorithms define which operators are carriers (audible) vs modulators, and how they're chained/stacked/feedback-routed. For psy sounds you want algorithms with **long modulator chains** (op6→op5→op4→op3, feeding into 1-2 carriers) rather than the parallel-carrier algorithms DX7 patches often use for keys/bells — chained modulators compound inharmonicity, which is what you want for metallic/alien tones.
- **Per-operator feedback** — DX7 architecture typically allows feedback on one operator (usually op6). This is your distortion/aggression knob as discussed — push it hard for growl bass or acid-lead character without needing a separate waveshaper.
- **Per-operator envelopes (rate/level, not just ADSR)** — this is actually *better* than a generic ADSR for psy plucks, since DX7-style rate/level envelopes let you shape complex multi-stage decay curves per operator, which is exactly how you'd get that "bright transient settling into inharmonic sustain" pluck character.

## Mapping the earlier roles onto DX7 algorithm choices

- **Growl bass**: pick a low algorithm number (heavily stacked, e.g. algorithm 1 or 2 in classic DX7 numbering) — single or few carriers, long modulator chain, feedback on the last operator in the chain. Ratios low and mostly-integer on the carrier-adjacent ops, then detune one deep modulator non-integer for grit.
- **Metallic plucks/stabs**: algorithms with 2-3 modulators feeding one carrier, non-integer ratios on at least one modulator, fast operator envelope decay.
- **Acid-style leads**: high feedback operator pushed near max, that operator's output as (or feeding) the carrier — feedback-heavy single-operator-ish patches get you into near-noise/self-oscillating territory the same way an analog resonant filter would.
- **Risers**: modulator rate envelope sweeping from very low (sub-audio, LFO-like vibrato) up into audio-rate over the note/automation duration — same trick as native FM, just implemented via the operator frequency ratio/coarse-tune modulation over time if the engine exposes that as automatable.

## One practical question for wiring this into the psy workflow

Does the Android-derived engine expose **operator ratio and feedback as real-time-automatable parameters** (not just patch-load-time settings)? That matters a lot here — a lot of what makes FM interesting for psytrance specifically (the riser trick, the "growl settling" envelope-on-index behavior) depends on modulating those values *during* the note or across a bar, not just setting them per-patch. If it's currently patch-static, that'd be the first thing worth exposing before trying to get these specific psy characters out of it.

Once ratio/feedback (and ideally per-operator output level, and detune) are automatable in real time, the next layer is building the **modulation routing** that actually drives them — the DX7 engine alone doesn't know it should riser or growl-settle, it just exposes knobs. You need a modulation matrix sitting above it.

## 1. Modulation source layer

You'd want at least these sources feeding into the now-automatable FM params:

```cpp
struct FmModSources
{
    ADSR      operatorIndexEnv[6];     // per-operator envelope, not just amp env
    LFO       ratioSweepLFO;           // sub-audio → audio-rate sweep (the riser trick)
    LFO       feedbackLFO;             // slow undulation on feedback amount
    BarClock  barSyncedAutomation;     // ties into the MutatorConductor concept from earlier
};
```

The key design decision: **do envelopes/LFOs target ratio and feedback directly, or through a "depth" parameter that scales a base value?** DX7-style engines usually want the latter — e.g., `feedbackBase + feedbackEnv.getValue() * feedbackEnvAmount` — so patches stay editable/predictable rather than the envelope just overwriting the knob.

## 2. Routing matrix

Rather than hardcoding "envelope 1 → operator 6 feedback," a small modulation matrix makes this reusable across all the psy roles you're targeting:

```cpp
struct ModMatrixEntry
{
    enum class Source { OpEnv1, OpEnv2, RatioSweepLFO, FeedbackLFO, BarClock, Velocity, ModWheel } source;
    enum class Dest   { Op1Ratio, Op2Ratio, /*...*/ Op6Feedback, OutputLevel } dest;
    float depth;
};
```

This is what lets one engine cover growl bass, plucks, acid leads, and risers — same DX7 core, different matrix routing per patch, rather than needing separate synths.

## 3. Patch-specific routing per role (concrete now that automation exists)

- **Growl bass**: `OpEnv1 → Op(feedback operator).feedback`, slow decay, so feedback is high at note-on and relaxes — the "growl settling" behavior. Bar clock could also nudge feedback per bar for evolving growls across a rolling bassline.
- **Riser**: `RatioSweepLFO → deep modulator's ratio`, LFO rate itself automated (slow → fast) over the riser's duration, sweeping from vibrato-speed into audio-rate — this needs the LFO's *own* rate to be automatable too, so that might be a second thing to check the engine supports (nested modulation).
- **Acid lead**: `ModWheel or filter-style envelope → feedback operator`, mapped so performing/automating it live feels like sweeping a resonant filter, even though there's no filter involved.
- **Metallic plucks**: `OpEnv (fast decay) → non-integer-ratio modulator's output level`, so the inharmonic content is loud at the transient and fades into a purer tone at sustain.

## 4. Practical implementation concerns worth flagging now

- **Ratio changes at audio rate can alias/click** — sweeping a modulator ratio continuously (not just at note-on) risks discontinuities if the phase isn't handled carefully. Worth deciding whether ratio sweeps are sample-accurate-smooth or quantized to control-rate updates (e.g., once per block) to sidestep this; the riser effect doesn't need sample-accurate ratio changes to sound convincing.
- **Feedback modulation is more forgiving than ratio modulation** — feedback is just a scalar gain on a delayed sample, so it can be swept smoothly at audio rate without the same phase-discontinuity risk. If you want to prioritize implementation effort, automatable feedback is lower-risk/higher-payoff than automatable ratio.
- **Tie the bar-synced sources into the `MutatorConductor` you already have** for the arp engine — rather than building a second bar-clock system, the FM engine's modulation matrix could subscribe to the same conductor tick, keeping bass/arp/lead mutation cycles musically related (e.g., all evolving on the same 4/8-bar structure) instead of independently drifting.

Want the modulation matrix sketched as actual `juce::dsp`-integratable code, wired to pull from the `MutatorConductor`?

```cpp
// ============================================================
// Modulation source pool — envelopes/LFOs feeding the FM matrix
// ============================================================
struct FmModSourcePool
{
    static constexpr int kNumOperators = 6;

    juce::ADSR opIndexEnv[kNumOperators];   // per-operator index/level envelopes
    juce::dsp::Oscillator<float> ratioSweepLFO;
    juce::dsp::Oscillator<float> feedbackLFO;
    float ratioSweepLFORateHz = 0.2f;       // itself automatable — see note below
    float modWheelValue = 0.0f;
    float velocityValue = 0.0f;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        for (auto& env : opIndexEnv)
            env.setSampleRate(spec.sampleRate);

        ratioSweepLFO.initialise([](float x) { return std::sin(x); });
        ratioSweepLFO.prepare(spec);
        feedbackLFO.initialise([](float x) { return std::sin(x); });
        feedbackLFO.prepare(spec);
    }

    // Called once per block; advances LFOs and returns per-source current values.
    // LFOs are updated at control rate (once per block) rather than sample-accurate,
    // sidestepping ratio-modulation aliasing risk noted earlier.
    void advanceControlRate(int numSamples, double sampleRate)
    {
        ratioSweepLFO.setFrequency(ratioSweepLFORateHz);
        feedbackLFO.setFrequency(feedbackLFORateHz);
        // Oscillator value fetched on demand via getValue() below; JUCE's Oscillator
        // advances internally per processSample call, so for control-rate use we
        // just step phase manually here rather than rendering audio through it.
        ratioSweepPhase_ += (ratioSweepLFORateHz * numSamples) / sampleRate;
        feedbackPhase_   += (feedbackLFORateHz   * numSamples) / sampleRate;
        if (ratioSweepPhase_ >= 1.0) ratioSweepPhase_ -= 1.0;
        if (feedbackPhase_   >= 1.0) feedbackPhase_   -= 1.0;
    }

    float getSourceValue(int sourceIndex) const
    {
        switch (sourceIndex)
        {
            case 0: return opIndexEnv[0].getNextSample(); // note: consumed once/block, see caveat below
            case 1: return opIndexEnv[1].getNextSample();
            case 2: return (float) std::sin(2.0 * juce::MathConstants<double>::pi * ratioSweepPhase_) * 0.5f + 0.5f;
            case 3: return (float) std::sin(2.0 * juce::MathConstants<double>::pi * feedbackPhase_) * 0.5f + 0.5f;
            case 4: return modWheelValue;
            case 5: return velocityValue;
            default: return 0.0f;
        }
    }

    float feedbackLFORateHz = 0.1f;
    double ratioSweepPhase_ = 0.0, feedbackPhase_ = 0.0;
};

// ============================================================
// Modulation matrix entry + full matrix
// ============================================================
struct ModMatrixEntry
{
    enum class Source { OpEnv1, OpEnv2, RatioSweepLFO, FeedbackLFO, ModWheel, Velocity, BarClock };
    enum class Dest   { Op1Ratio, Op2Ratio, Op3Ratio, Op4Ratio, Op5Ratio, Op6Ratio,
                         Op6Feedback, Op1OutputLevel, RatioSweepRateItself };

    Source source;
    Dest   dest;
    float  depth = 0.0f;   // signed — negative depth inverts the modulation direction
    bool   smoothed = true; // if true, depth changes ramp rather than jump (avoids zipper noise)
};

class FmModMatrix
{
public:
    void addRoute(ModMatrixEntry entry) { routes_.push_back(entry); }
    void clearRoutes() { routes_.clear(); }

    // Applies all routes to the base FM params for this block, writing into
    // liveParams (a copy of the patch's base params that gets modulated per-block).
    void apply(const FmModSourcePool& sources, FmVoiceParams& liveParams, float baseRatios[6], float baseFeedback)
    {
        // reset to base values before applying this block's modulation
        for (int i = 0; i < 6; ++i)
            liveParams.opRatio[i] = baseRatios[i];
        liveParams.feedbackAmount = baseFeedback;

        for (auto& route : routes_)
        {
            float srcVal = sources.getSourceValue(sourceIndexFor(route.source));
            float modAmount = srcVal * route.depth;
            applyToDest(route.dest, modAmount, liveParams);
        }
    }

private:
    int sourceIndexFor(ModMatrixEntry::Source s) const
    {
        switch (s)
        {
            case ModMatrixEntry::Source::OpEnv1:        return 0;
            case ModMatrixEntry::Source::OpEnv2:        return 1;
            case ModMatrixEntry::Source::RatioSweepLFO: return 2;
            case ModMatrixEntry::Source::FeedbackLFO:   return 3;
            case ModMatrixEntry::Source::ModWheel:      return 4;
            case ModMatrixEntry::Source::Velocity:      return 5;
            default: return 0;
        }
    }

    void applyToDest(ModMatrixEntry::Dest dest, float amount, FmVoiceParams& params)
    {
        switch (dest)
        {
            case ModMatrixEntry::Dest::Op6Feedback:
                params.feedbackAmount = juce::jlimit(0.0f, 1.0f, params.feedbackAmount + amount);
                break;
            case ModMatrixEntry::Dest::Op1Ratio:
                params.opRatio[0] += amount; // small nudges only — large ratio jumps risk aliasing, see caveat
                break;
            // ... remaining Op2-6 ratio cases follow same pattern ...
            case ModMatrixEntry::Dest::RatioSweepRateItself:
                // nested modulation: lets bar clock or mod wheel speed up the riser LFO itself
                break;
            default: break;
        }
    }

    std::vector<ModMatrixEntry> routes_;
};

// ============================================================
// Per-role patch presets, built from routing rather than hardcoded synths
// ============================================================
namespace PsyFmPatches
{
    inline FmModMatrix makeGrowlBassMatrix()
    {
        FmModMatrix m;
        // Feedback starts high, decays via OpEnv1 (fast attack, slow decay) — "settling growl"
        m.addRoute({ ModMatrixEntry::Source::OpEnv1, ModMatrixEntry::Dest::Op6Feedback, 0.4f, true });
        return m;
    }

    inline FmModMatrix makeRiserMatrix()
    {
        FmModMatrix m;
        // Bar clock slowly speeds up the ratio-sweep LFO rate itself (sub-audio -> audio-rate)
        m.addRoute({ ModMatrixEntry::Source::BarClock, ModMatrixEntry::Dest::RatioSweepRateItself, 1.0f, true });
        m.addRoute({ ModMatrixEntry::Source::RatioSweepLFO, ModMatrixEntry::Dest::Op4Ratio, 0.3f, true });
        return m;
    }

    inline FmModMatrix makeAcidLeadMatrix()
    {
        FmModMatrix m;
        // Mod wheel directly drives feedback toward self-oscillation, filter-sweep-style performance control
        m.addRoute({ ModMatrixEntry::Source::ModWheel, ModMatrixEntry::Dest::Op6Feedback, 0.9f, true });
        return m;
    }

    inline FmModMatrix makeMetallicPluckMatrix()
    {
        FmModMatrix m;
        // Fast-decay envelope drives a non-integer modulator's output level, bright transient -> pure sustain
        m.addRoute({ ModMatrixEntry::Source::OpEnv2, ModMatrixEntry::Dest::Op1OutputLevel, 0.6f, true });
        return m;
    }
}

// ============================================================
// Tying into MutatorConductor's bar clock
// ============================================================
class FmMutatorBridge
{
public:
    // Called from MutatorConductor's bar-boundary tick (same object driving arp mutation),
    // so bass/arp/lead evolution stays synchronized to the same musical structure.
    void onBarBoundary(int barCounter, FmModSourcePool& sources)
    {
        // Example: every 8 bars, nudge the riser's ratio-sweep rate up a step
        if (barCounter % 8 == 0)
            sources.ratioSweepLFORateHz = juce::jmin(sources.ratioSweepLFORateHz * 1.3f, 40.0f);
    }
};
```

A few things worth flagging rather than glossing over:

- **`juce::ADSR::getNextSample()` is designed to be called once per audio sample, not once per block** — I called it once per block above for the control-rate sketch, which will make the envelope run far slower than intended in a real implementation. You'd either need a proper sample-accurate envelope path feeding into per-sample FM param updates (higher CPU cost, more accurate), or a lighter control-rate envelope implementation that isn't `juce::ADSR` directly. Worth deciding before wiring this for real — sample-accurate matters more for `OpEnv` (fast plucks need real envelope shape) than for the LFO sources.
- **`RatioSweepRateItself` as a destination is nested modulation** (a mod source controlling another mod source's rate) — this is what makes the riser's "vibrato speeding into a scream" trick work, but it also means route ordering/evaluation order matters (you need to update `ratioSweepLFORateHz` *before* computing that LFO's phase for the block, which the sketch above does via the bridge calling in before `advanceControlRate`).
- **Ratio modulation depth is deliberately kept small** (`+= amount` rather than a wide scaled range) per the earlier aliasing caveat — large real-time ratio jumps on a phase-based oscillator will click/alias. If you want dramatic ratio sweeps (not just subtle drift), that likely needs a proper anti-aliased ratio-interpolation scheme rather than a naive per-block update.

Want me to work through the sample-accurate envelope question next, since that's probably the actual blocker between this sketch and something you'd ship?

The core tension: `juce::ADSR` (and most envelope classes) assume `getNextSample()` is called once per sample to produce a smoothly interpolated curve. But your FM operator params (ratio, feedback, output level) are being applied at **block rate** in the sketch above for CPU/aliasing reasons. So you need to decide, per modulation destination, which rate actually matters — and then bridge the two cleanly rather than picking one rate for everything.

## Which destinations actually need sample-accuracy

Not all of them do, and conflating them is where the design gets messy:

| Destination | Needs sample-accuracy? | Why |
|---|---|---|
| Operator output level / index | **Yes** | This *is* the amplitude envelope shape — audible zipper/stepping artifacts if block-rate, especially on fast plucks with short decay |
| Feedback amount | Mostly no | Scalar gain on a delayed sample; smooth interpolation between block-rate updates is enough, per the earlier note |
| Ratio | No — actively risky at sample-rate | You want *fewer* updates here, not more, to avoid phase discontinuities |
| Ratio-sweep LFO rate (nested) | No | It's already a slow meta-parameter; block-rate is plenty |

So the fix isn't "make everything sample-accurate" — it's **two update paths**: a sample-accurate envelope path feeding operator output level directly inside the audio-render loop, and the existing block-rate matrix for everything else.

## Sample-accurate envelope path

```cpp
class OperatorEnvelope
{
public:
    void setSampleRate(double sr) { adsr_.setSampleRate(sr); }
    void setParameters(juce::ADSR::Parameters p) { adsr_.setParameters(p); }
    void noteOn()  { adsr_.noteOn(); }
    void noteOff() { adsr_.noteOff(); }

    // Called once per sample, inside the operator's render loop
    float getNextSample() { return adsr_.getNextSample(); }

private:
    juce::ADSR adsr_;
};
```

And the operator render loop itself moves the envelope call inside the per-sample loop, rather than the block-rate matrix touching output level at all:

```cpp
void FmOperator::renderBlock(float* outBuffer, int numSamples, const FmVoiceParams& liveParams)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float envValue = indexEnv_.getNextSample();       // sample-accurate
        float modInput = feedbackAmount_ * lastOutput_;    // block-rate value, held constant across the block

        float phaseIncrement = juce::MathConstants<float>::twoPi * currentRatio_ * baseFreq_ / (float) sampleRate_;
        phase_ += phaseIncrement;
        if (phase_ > juce::MathConstants<float>::twoPi) phase_ -= juce::MathConstants<float>::twoPi;

        float sample = std::sin(phase_ + modInput) * envValue;
        outBuffer[i] = sample;
        lastOutput_ = sample;
    }
}
```

Here, `currentRatio_` and `feedbackAmount_` are set **once before the loop starts** (from the block-rate matrix pass), while `envValue` updates **every sample** from its own dedicated envelope object. That split is really the whole answer: ratio/feedback are block-constant parameters read into local variables before the sample loop, and only the envelope actually runs inside it.

## Updated modulation matrix apply() to reflect the split

```cpp
void FmModMatrix::apply(const FmModSourcePool& sources, FmVoiceParams& liveParams,
                          float baseRatios[6], float baseFeedback)
{
    // Block-rate destinations only — output level / index routes are excluded here
    // and handled instead via each operator's own OperatorEnvelope inside renderBlock().
    for (int i = 0; i < 6; ++i)
        liveParams.opRatio[i] = baseRatios[i];
    liveParams.feedbackAmount = baseFeedback;

    for (auto& route : routes_)
    {
        if (route.dest == ModMatrixEntry::Dest::Op1OutputLevel)
            continue; // sample-accurate path owns this destination now

        float srcVal = sources.getSourceValue(sourceIndexFor(route.source));
        applyToDest(route.dest, srcVal * route.depth, liveParams);
    }
}
```

## Why this split is the right line, not an arbitrary one

The thing that actually matters perceptually is: **envelope shape is what your ear tracks as "the sound's contour"** — any staircasing there is immediately audible, especially on percussive plucks with 10-30ms decay times where a block of 64-512 samples is a meaningful fraction of the whole envelope. Ratio and feedback, by contrast, are timbral/textural parameters where the ear is much more forgiving of block-rate stepping — and forcing them to sample-rate actually costs you correctness (aliasing) rather than buying you anything.

One remaining design decision: **where does `OperatorEnvelope` live relative to the matrix's `OpEnv1`/`OpEnv2` sources** — right now the matrix's `FmModSourcePool::opIndexEnv[]` and the operator's own `OperatorEnvelope` are describing the same concept in two places. Cleanest fix is probably to delete the envelope entries from `FmModSourcePool` entirely and have each `FmOperator` own its envelope directly, with the matrix only routing *other* sources (mod wheel, bar clock, LFOs) to *other* destinations (ratio, feedback) — output level becomes a first-class per-operator property rather than a matrix-routed one. Want me to refactor the full FM voice class with that ownership split cleaned up?

```cpp
// ============================================================
// FM Operator — owns its own envelope directly (sample-accurate),
// receives block-rate ratio/feedback from the voice's matrix pass
// ============================================================
class FmOperator
{
public:
    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        indexEnv_.setSampleRate(sampleRate);
    }

    void setEnvelopeParams(const juce::ADSR::Parameters& p) { indexEnv_.setParameters(p); }
    void noteOn()  { indexEnv_.noteOn(); phase_ = 0.0f; }
    void noteOff() { indexEnv_.noteOff(); }
    bool isActive() const { return indexEnv_.isActive(); }

    // Block-constant params, set once per block before renderBlock() runs
    void setBlockParams(float ratio, float feedbackAmount, float baseFreqHz)
    {
        currentRatio_ = ratio;
        feedbackAmount_ = feedbackAmount;
        baseFreq_ = baseFreqHz;
    }

    // modInputBuffer: per-sample phase modulation input from an upstream operator (nullptr if none —
    // i.e. this operator is a pure carrier or the first modulator in the chain)
    void renderBlock(float* outBuffer, const float* modInputBuffer, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float envValue = indexEnv_.getNextSample(); // sample-accurate — owns the amplitude/index contour

            float externalMod = modInputBuffer != nullptr ? modInputBuffer[i] : 0.0f;
            float selfFeedback = feedbackAmount_ * lastOutput_;

            float phaseIncrement = juce::MathConstants<float>::twoPi * currentRatio_ * baseFreq_ / (float) sampleRate_;
            phase_ += phaseIncrement;
            if (phase_ > juce::MathConstants<float>::twoPi)
                phase_ -= juce::MathConstants<float>::twoPi;

            float sample = std::sin(phase_ + externalMod + selfFeedback) * envValue;
            outBuffer[i] = sample;
            lastOutput_ = sample;
        }
    }

private:
    double sampleRate_ = 44100.0;
    juce::ADSR indexEnv_;
    float phase_ = 0.0f, lastOutput_ = 0.0f;
    float currentRatio_ = 1.0f, feedbackAmount_ = 0.0f, baseFreq_ = 220.0f;
};

// ============================================================
// Modulation source pool — LFOs and performance sources only.
// Per-operator envelopes have been removed; each FmOperator owns its own.
// ============================================================
struct FmModSourcePool
{
    float ratioSweepLFORateHz = 0.2f;
    float feedbackLFORateHz   = 0.1f;
    float modWheelValue = 0.0f;
    float velocityValue = 0.0f;
    double ratioSweepPhase_ = 0.0, feedbackPhase_ = 0.0;

    void advanceControlRate(int numSamples, double sampleRate)
    {
        ratioSweepPhase_ += (ratioSweepLFORateHz * numSamples) / sampleRate;
        feedbackPhase_   += (feedbackLFORateHz   * numSamples) / sampleRate;
        if (ratioSweepPhase_ >= 1.0) ratioSweepPhase_ -= 1.0;
        if (feedbackPhase_   >= 1.0) feedbackPhase_   -= 1.0;
    }

    float getSourceValue(int sourceIndex) const
    {
        switch (sourceIndex)
        {
            case 0: return (float) std::sin(2.0 * juce::MathConstants<double>::pi * ratioSweepPhase_) * 0.5f + 0.5f;
            case 1: return (float) std::sin(2.0 * juce::MathConstants<double>::pi * feedbackPhase_) * 0.5f + 0.5f;
            case 2: return modWheelValue;
            case 3: return velocityValue;
            default: return 0.0f;
        }
    }
};

// ============================================================
// Modulation matrix — now only routes to block-rate destinations
// (ratio, feedback, nested LFO rate). Output level/index is no
// longer a valid destination; it lives inside FmOperator.
// ============================================================
struct ModMatrixEntry
{
    enum class Source { RatioSweepLFO, FeedbackLFO, ModWheel, Velocity, BarClock };
    enum class Dest   { Op1Ratio, Op2Ratio, Op3Ratio, Op4Ratio, Op5Ratio, Op6Ratio,
                         Op6Feedback, RatioSweepRateItself };
    Source source;
    Dest   dest;
    float  depth = 0.0f;
};

class FmModMatrix
{
public:
    void addRoute(ModMatrixEntry entry) { routes_.push_back(entry); }
    void clearRoutes() { routes_.clear(); }

    void apply(const FmModSourcePool& sources, float baseRatios[6], float baseFeedback,
               float outRatios[6], float& outFeedback)
    {
        for (int i = 0; i < 6; ++i) outRatios[i] = baseRatios[i];
        outFeedback = baseFeedback;

        for (auto& route : routes_)
        {
            float srcVal = sources.getSourceValue(sourceIndexFor(route.source));
            float amount = srcVal * route.depth;

            switch (route.dest)
            {
                case ModMatrixEntry::Dest::Op1Ratio: outRatios[0] += amount; break;
                case ModMatrixEntry::Dest::Op2Ratio: outRatios[1] += amount; break;
                case ModMatrixEntry::Dest::Op3Ratio: outRatios[2] += amount; break;
                case ModMatrixEntry::Dest::Op4Ratio: outRatios[3] += amount; break;
                case ModMatrixEntry::Dest::Op5Ratio: outRatios[4] += amount; break;
                case ModMatrixEntry::Dest::Op6Ratio: outRatios[5] += amount; break;
                case ModMatrixEntry::Dest::Op6Feedback:
                    outFeedback = juce::jlimit(0.0f, 1.0f, outFeedback + amount);
                    break;
                case ModMatrixEntry::Dest::RatioSweepRateItself:
                    // handled by FmMutatorBridge before this pass runs, not here
                    break;
            }
        }
    }

private:
    int sourceIndexFor(ModMatrixEntry::Source s) const
    {
        switch (s)
        {
            case ModMatrixEntry::Source::RatioSweepLFO: return 0;
            case ModMatrixEntry::Source::FeedbackLFO:   return 1;
            case ModMatrixEntry::Source::ModWheel:      return 2;
            case ModMatrixEntry::Source::Velocity:      return 3;
            default: return 0;
        }
    }

    std::vector<ModMatrixEntry> routes_;
};

// ============================================================
// Full 6-operator FM voice — algorithm routing + block/sample split
// ============================================================
class FmVoice
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate_ = spec.sampleRate;
        for (auto& op : operators_)
            op.prepare(spec.sampleRate);

        scratchBuffers_.resize(6);
        for (auto& buf : scratchBuffers_)
            buf.resize((size_t) spec.maximumBlockSize, 0.0f);

        sources_.advanceControlRate(0, spec.sampleRate); // init
    }

    void setAlgorithm(std::function<void(FmVoice&, int numSamples)> algorithmFn)
    {
        algorithmFn_ = std::move(algorithmFn);
    }

    void setBaseRatios(const float ratios[6]) { std::copy(ratios, ratios + 6, baseRatios_); }
    void setBaseFeedback(float fb) { baseFeedback_ = fb; }
    void setModMatrix(FmModMatrix matrix) { matrix_ = std::move(matrix); }
    void setOperatorEnvelope(int opIndex, const juce::ADSR::Parameters& p) { operators_[opIndex].setEnvelopeParams(p); }

    void noteOn(int midiNote, float velocity)
    {
        baseFreqHz_ = (float) juce::MidiMessage::getMidiNoteInHertz(midiNote);
        sources_.velocityValue = velocity;
        for (auto& op : operators_) op.noteOn();
    }

    void noteOff() { for (auto& op : operators_) op.noteOff(); }

    // Called once per block from the conductor before rendering audio
    void onBarBoundary(int barCounter)
    {
        // e.g. nested modulation: bar clock speeds up the riser LFO's own rate
        if (barCounter % 8 == 0)
            sources_.ratioSweepLFORateHz = juce::jmin(sources_.ratioSweepLFORateHz * 1.3f, 40.0f);
    }

    void renderBlock(float* outBuffer, int numSamples)
    {
        // 1. Block-rate modulation pass — ratio/feedback resolved once for this block
        sources_.advanceControlRate(numSamples, sampleRate_);
        float liveRatios[6];
        float liveFeedback;
        matrix_.apply(sources_, baseRatios_, baseFeedback_, liveRatios, liveFeedback);

        for (int i = 0; i < 6; ++i)
            operators_[i].setBlockParams(liveRatios[i], (i == 5 ? liveFeedback : 0.0f), baseFreqHz_);

        // 2. Sample-accurate render pass — algorithm function decides operator chaining/summing
        std::fill(outBuffer, outBuffer + numSamples, 0.0f);
        algorithmFn_(*this, numSamples);

        for (int i = 0; i < numSamples; ++i)
            outBuffer[i] = carrierMixBuffer_[(size_t) i];
    }

    // Helpers algorithm functions use to chain operators
    float* getScratch(int opIndex) { return scratchBuffers_[(size_t) opIndex].data(); }
    FmOperator& op(int index) { return operators_[index]; }
    std::vector<float>& carrierMix() { return carrierMixBuffer_; }

private:
    double sampleRate_ = 44100.0;
    float baseFreqHz_ = 220.0f;
    float baseRatios_[6] = { 1,1,1,1,1,1 };
    float baseFeedback_ = 0.0f;

    FmOperator operators_[6];
    FmModSourcePool sources_;
    FmModMatrix matrix_;
    std::function<void(FmVoice&, int)> algorithmFn_;

    std::vector<std::vector<float>> scratchBuffers_;
    std::vector<float> carrierMixBuffer_;
};

// ============================================================
// Example algorithm: growl bass — chained modulators into one carrier,
// feedback on the last modulator in the chain (op6)
// ============================================================
inline void growlBassAlgorithm(FmVoice& voice, int numSamples)
{
    // op6 (with feedback) -> modulates op5 -> modulates op1 (carrier)
    float* op6Buf = voice.getScratch(5);
    voice.op(5).renderBlock(op6Buf, nullptr, numSamples);

    float* op5Buf = voice.getScratch(4);
    voice.op(4).renderBlock(op5Buf, op6Buf, numSamples);

    auto& carrierOut = voice.carrierMix();
    carrierOut.resize((size_t) numSamples);
    voice.op(0).renderBlock(carrierOut.data(), op5Buf, numSamples);
}
```

Key things this refactor settles vs. the earlier sketch:

- **Envelope ownership is unambiguous now** — `FmOperator::indexEnv_` is the single source of truth for that operator's amplitude/index contour, called sample-accurately inside `renderBlock()`. Nothing in the matrix touches output level anymore.
- **Block-rate vs sample-rate is enforced structurally, not by convention** — `setBlockParams()` is the only way ratio/feedback reach an operator, and it's only called once per block from `FmVoice::renderBlock()`. There's no path for something to accidentally update ratio mid-block.
- **Algorithm routing is a pluggable function** (`algorithmFn_`) rather than hardcoded operator chaining — `growlBassAlgorithm` above is one example (matches the earlier "low algorithm number, long modulator chain, feedback on the last op" spec), and acid-lead/pluck/riser algorithms would just be different functions with different chaining, reusing the same `FmVoice`/`FmOperator` machinery.
- **`onBarBoundary` lives directly on `FmVoice`** now rather than a separate bridge class, so the `MutatorConductor` can call `voice.onBarBoundary(barCounter)` alongside whatever it's already doing for the arp voice — one conductor, multiple synths subscribing to the same bar clock.

One thing left genuinely open: `carrierMixBuffer_` currently assumes a single carrier (op1). Algorithms with multiple parallel carriers (common in some DX7 algorithms, useful for layering two differently-tuned carriers under one modulator chain) would need `renderBlock` to sum multiple operator outputs rather than assume one. Worth addressing if any of the psy patches you have in mind want that — growl bass and acid lead are fine as single-carrier, but a pad-adjacent FM patch might not be.

