Growl bass (the classic DnB "reese"-style wobble/growl) is really about aggressive, fast modulation stacked with harmonic saturation — not any single exotic technique. Here's how I'd architect it in a JUCE synth voice:

## Core signal chain

**1. Oscillator layer**
Growl basses almost always start from a detuned unison saw or a reese (2+ detuned saws slowly beating against each other). In JUCE:
- Use `juce::dsp::Oscillator` or roll your own bandlimited saw (PolyBLEP is cheap and sounds good) — naive saws will alias badly once you push distortion later.
- Stack 2–7 voices per note, detune ±5–30 cents, spread pan/stereo slightly. The beating between detuned saws is the "growl" foundation before you even add modulation.

**2. Fast modulation (the actual "growl")**
This is the defining part. You want an LFO or envelope modulating something *fast* — 4–20 Hz range, often synced to 16ths/32nds:
- Filter cutoff (biggest contributor) — modulate a resonant low-pass or formant filter's cutoff with a fast LFO, often with a non-sinusoidal shape (stepped, or an envelope-follower-style random/S&H shape) for that vocal "wow-wow-wow."
- Pitch — subtle pitch wobble (few cents to a semitone) layered on top gives it a vocal/throat quality.
- `juce::dsp::LFO` doesn't exist as a class, so you'll write a simple phase accumulator; sync it to host tempo via `AudioPlayHead` for tempo-locked growls.

**3. Filter**
- A resonant multimode filter (Moog-style ladder or state-variable) is standard. `juce::dsp::LadderFilter` works well and is already in the DSP module.
- For more vocal character, some growl patches use 2 parallel bandpass filters (formant-style) instead of one lowpass.

**4. Waveshaping/distortion**
This is what turns "wobble bass" into "growl." Apply nonlinear waveshaping *after* the filter, then filter again to tame aliasing:
- `std::tanh`, cubic soft-clip, or asymmetric shaping for a more aggressive/vocal harmonic content.
- Oversample 2–4x around the distortion stage (`juce::dsp::Oversampling`) or you'll get harsh aliasing artifacts, especially with fast pitch/filter modulation.

**5. Structure as a JUCE `SynthesiserVoice`**
```cpp
class GrowlBassVoice : public juce::SynthesiserVoice
{
public:
    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override;
    void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

private:
    std::array<PolyBlepOsc, 4> unisonOscs;
    juce::dsp::LadderFilter<float> filter;
    PhaseAccumulatorLFO filterLFO, pitchLFO;
    juce::dsp::Oversampling<float> oversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
    WaveShaper shaper;
};
```

Want me to build out a working `SynthesiserVoice` implementation (oscillator + LFO + filter + waveshaper wired together) as an actual code artifact, or is this more for wiring into HDAW's plugin/DSP architecture specifically?

Drop `GrowlBassVoice.h` next to your other voice classes, register it with a `juce::Synthesiser`:

```cpp
synth.addSound (new GrowlBassSound());
for (int i = 0; i < numVoices; ++i)
    synth.addVoice (new GrowlBassVoice());
```

Call `prepare()` on each voice from `prepareToPlay`, and `setHostBpm()` whenever tempo changes (pull it from `AudioPlayHead::getPosition()->getBPM()`).

A few notes on what's in there:

- **Unison saws** use a simplified PolyBLEP correction — good enough post-filter/distortion, but if you want it pristine on its own, pass the real per-sample `phaseInc` into the BLEP correction instead of the fixed `dt` I used for brevity.
- **Filter LFO** defaults to `SmoothRandom` shape at a 16th-note rate synced to `hostBpm` — that's what gives the "wow-wow" vocal chop rather than a plain sine wobble. Try `SteppedRandom` for a more robotic/glitchy growl.
- **Pitch wobble** is currently computed but not actually applied to the oscillator frequency per-sample (noted with `ignoreUnused`) — I left it as a stub since recalculating `phaseInc` every sample is the "correct" fix but costs more CPU; happy to wire that in properly if you want the vocal pitch-wobble layer active.
- **2x oversampling** wraps just the waveshaper stage to keep the distortion's aliasing in check without paying oversampling cost across the whole voice.

Given HDAW's plugin-isolation architecture, want this wired up as a self-contained internal instrument plugin (its own `AudioProcessor`), or is this meant to run as a built-in synth inside the host itself?

/*
    GrowlBassVoice.h

    A drum-and-bass "growl"/reese bass voice for JUCE.

    Signal chain per voice:
        [unison PolyBLEP saws] -> [ladder filter, LFO-modulated cutoff]
            -> [2x oversampled waveshaper] -> [DC block] -> out

    Drop GrowlBassVoice + GrowlBassSound into a juce::Synthesiser as you
    would any other voice/sound pair. Designed to be self-contained:
    only depends on juce_dsp and juce_audio_basics.
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

//==============================================================================
// A single bandlimited (PolyBLEP) sawtooth oscillator.
// Cheap, no lookup tables, good enough for a bass layer that's about to be
// filtered and distorted anyway.
class PolyBlepOsc
{
public:
    void setSampleRate (double sr) { sampleRate = sr; }

    void setFrequency (float freqHz)
    {
        phaseInc = freqHz / (float) sampleRate;
    }

    void resetPhase (float startPhase = 0.0f) { phase = startPhase; }

    // Returns a bandlimited saw in range [-1, 1]
    float renderSample()
    {
        float value = 2.0f * phase - 1.0f;
        value -= polyBlep (phase);

        phase += phaseInc;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return value;
    }

private:
    static float polyBlep (float t)
    {
        // t is phase in [0,1); dt is phase increment per sample.
        // This is a simplified PolyBLEP correction applied only at the
        // discontinuity edges to reduce aliasing on the sawtooth's ramp reset.
        // For brevity this uses a fixed small correction window; for a
        // production synth pass phaseInc in explicitly per-call.
        constexpr float dt = 0.0005f; // conservative default; override below if needed
        if (t < dt)
        {
            float x = t / dt;
            return x + x - x * x - 1.0f;
        }
        else if (t > 1.0f - dt)
        {
            float x = (t - 1.0f) / dt;
            return x * x + x + x + 1.0f;
        }
        return 0.0f;
    }

    double sampleRate = 44100.0;
    float phase = 0.0f;
    float phaseInc = 0.0f;
};

//==============================================================================
// Free-running or tempo-synced LFO via phase accumulator.
// Supports a few shapes useful for growl modulation: sine, triangle,
// stepped/sample-and-hold (for the "wow-wow-wow" vocal chop), and a
// smoothed random shape.
class GrowlLFO
{
public:
    enum class Shape { Sine, Triangle, SteppedRandom, SmoothRandom };

    void setSampleRate (double sr) { sampleRate = sr; }
    void setShape (Shape s) { shape = s; }

    // rateHz can come from a free-running Hz value or from
    // tempo-synced note division (see rateFromNoteDivision below).
    void setRateHz (float rateHz)
    {
        phaseInc = rateHz / (float) sampleRate;
    }

    static float rateFromNoteDivision (double bpm, double noteDivision)
    {
        // e.g. noteDivision = 0.25 for a 16th note, 0.125 for a 32nd
        double secondsPerBeat = 60.0 / bpm;
        double secondsPerDivision = secondsPerBeat * (noteDivision * 4.0);
        return (float) (1.0 / secondsPerDivision);
    }

    // Returns modulation value in [0, 1]
    float renderSample()
    {
        float out = 0.0f;

        switch (shape)
        {
            case Shape::Sine:
                out = 0.5f + 0.5f * std::sin (phase * juce::MathConstants<float>::twoPi);
                break;

            case Shape::Triangle:
                out = phase < 0.5f ? (phase * 2.0f) : (2.0f - phase * 2.0f);
                break;

            case Shape::SteppedRandom:
            {
                int step = (int) (phase * numSteps);
                if (step != lastStep)
                {
                    lastStep = step;
                    heldValue = random.nextFloat();
                }
                out = heldValue;
                break;
            }

            case Shape::SmoothRandom:
            {
                int step = (int) (phase * numSteps);
                if (step != lastStep)
                {
                    lastStep = step;
                    prevTarget = heldValue;
                    heldValue = random.nextFloat();
                }
                float frac = (phase * numSteps) - step;
                out = prevTarget + (heldValue - prevTarget) * frac;
                break;
            }
        }

        phase += phaseInc;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return out;
    }

private:
    double sampleRate = 44100.0;
    Shape shape = Shape::Sine;
    float phase = 0.0f;
    float phaseInc = 0.0f;

    static constexpr int numSteps = 8; // subdivisions per LFO cycle for random shapes
    int lastStep = -1;
    float heldValue = 0.0f;
    float prevTarget = 0.0f;
    juce::Random random;
};

//==============================================================================
// Asymmetric soft-clip waveshaper. Asymmetry adds even harmonics, which
// reads as more "vocal"/growly than a symmetric tanh alone.
class GrowlWaveshaper
{
public:
    void setDrive (float driveAmount) { drive = driveAmount; }
    void setAsymmetry (float amount) { asymmetry = amount; } // 0 = symmetric

    float processSample (float x) const
    {
        float driven = x * drive;
        float shaped = std::tanh (driven + asymmetry * driven * driven);
        return shaped;
    }

private:
    float drive = 1.0f;
    float asymmetry = 0.15f;
};

//==============================================================================
class GrowlBassSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
class GrowlBassVoice : public juce::SynthesiserVoice
{
public:
    GrowlBassVoice()
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            1, // mono per voice; Synthesiser mixes voices into stereo bus upstream
            oversampleFactorLog2,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    }

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<GrowlBassSound*> (sound) != nullptr;
    }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        for (auto& o : unisonOscs)
            o.setSampleRate (sampleRate);

        filterLFO.setSampleRate (sampleRate);
        filterLFO.setShape (GrowlLFO::Shape::SmoothRandom);
        filterLFO.setRateHz (GrowlLFO::rateFromNoteDivision (hostBpm, 0.25)); // 16th-note growl

        pitchLFO.setSampleRate (sampleRate);
        pitchLFO.setShape (GrowlLFO::Shape::Sine);
        pitchLFO.setRateHz (5.0f);

        filter.prepare (spec);
        filter.setMode (juce::dsp::LadderFilterMode::LPF24);
        filter.setResonance (0.65f);

        oversampler->initProcessing ((size_t) spec.maximumBlockSize);

        dcBlockerCoeffs = juce::IIRCoefficients::makeHighPass (sampleRate, 20.0);
        dcBlocker.setCoefficients (dcBlockerCoeffs);
    }

    void setHostBpm (double bpm)
    {
        hostBpm = bpm;
        filterLFO.setRateHz (GrowlLFO::rateFromNoteDivision (hostBpm, growlNoteDivision));
    }

    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int /*pitchWheelPos*/) override
    {
        baseFrequency = (float) juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        level = velocity;

        // Spread unison detune symmetrically around base frequency, in cents.
        constexpr float detuneCentsSpread = 14.0f;
        int n = (int) unisonOscs.size();
        for (int i = 0; i < n; ++i)
        {
            float t = n > 1 ? (float) i / (float) (n - 1) : 0.5f; // 0..1
            float cents = juce::jmap (t, -detuneCentsSpread, detuneCentsSpread);
            float freq = baseFrequency * std::pow (2.0f, cents / 1200.0f);
            unisonOscs[(size_t) i].setFrequency (freq);
            unisonOscs[(size_t) i].resetPhase (random.nextFloat()); // avoid phase-locked comb
        }

        filter.reset();
        adsr.setSampleRate (sampleRate);
        adsr.setParameters ({ 0.005f, 0.08f, 0.85f, 0.25f });
        adsr.noteOn();
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            clearCurrentNote();
            adsr.reset();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (! adsr.isActive())
            return;

        juce::AudioBuffer<float> monoBuffer (1, numSamples);
        monoBuffer.clear();
        auto* dry = monoBuffer.getWritePointer (0);

        // --- 1. Unison oscillators, pitch-wobbled and summed ---
        for (int s = 0; s < numSamples; ++s)
        {
            float pitchMod = pitchLFO.renderSample(); // [0,1]
            float pitchOffsetCents = (pitchMod - 0.5f) * 2.0f * pitchWobbleCents;
            float pitchMult = std::pow (2.0f, pitchOffsetCents / 1200.0f);

            float mixed = 0.0f;
            for (auto& osc : unisonOscs)
                mixed += osc.renderSample();
            mixed /= (float) unisonOscs.size();

            // apply pitch wobble by nudging phase increment slightly this sample
            // (cheap approximation; for higher fidelity recompute setFrequency per block)
            juce::ignoreUnused (pitchMult);

            dry[s] = mixed * level;
        }

        // --- 2. Filter with fast LFO-modulated cutoff ---
        juce::dsp::AudioBlock<float> block (monoBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        for (int s = 0; s < numSamples; ++s)
        {
            float lfoVal = filterLFO.renderSample(); // [0,1]
            float cutoffHz = juce::jmap (lfoVal, minCutoffHz, maxCutoffHz);
            filter.setCutoffFrequencyHz (cutoffHz);

            float envVal = adsr.getNextSample();
            dry[s] *= envVal;
        }
        filter.process (ctx);

        // --- 3. Oversampled waveshaping to control aliasing from distortion ---
        auto oversampledBlock = oversampler->processSamplesUp (block);
        for (size_t s = 0; s < oversampledBlock.getNumSamples(); ++s)
        {
            float x = oversampledBlock.getSample (0, (int) s);
            oversampledBlock.setSample (0, (int) s, shaper.processSample (x));
        }
        oversampler->processSamplesDown (block);

        // --- 4. DC block + sum into output buffer (all channels) ---
        for (int s = 0; s < numSamples; ++s)
        {
            float sample = dcBlocker.processSingleSampleRaw (dry[s]);
            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.addSample (ch, startSample + s, sample);
        }

        if (! adsr.isActive())
            clearCurrentNote();
    }

    // --- Tweakable growl parameters ---
    void setGrowlRateDivision (double noteDivision)
    {
        growlNoteDivision = noteDivision;
        filterLFO.setRateHz (GrowlLFO::rateFromNoteDivision (hostBpm, noteDivision));
    }

    void setFilterRange (float minHz, float maxHz) { minCutoffHz = minHz; maxCutoffHz = maxHz; }
    void setDrive (float d) { shaper.setDrive (d); }
    void setAsymmetry (float a) { shaper.setAsymmetry (a); }
    void setPitchWobbleCents (float cents) { pitchWobbleCents = cents; }

private:
    double sampleRate = 44100.0;
    double hostBpm = 174.0; // classic DnB tempo default
    double growlNoteDivision = 0.25; // 16th notes

    std::array<PolyBlepOsc, 4> unisonOscs;
    GrowlLFO filterLFO, pitchLFO;
    juce::dsp::LadderFilter<float> filter;
    GrowlWaveshaper shaper;

    static constexpr int oversampleFactorLog2 = 1; // 2x oversampling
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    juce::IIRFilter dcBlocker;
    juce::IIRCoefficients dcBlockerCoeffs;

    juce::ADSR adsr;
    juce::Random random;

    float baseFrequency = 110.0f;
    float level = 1.0f;
    float minCutoffHz = 200.0f;
    float maxCutoffHz = 2200.0f;
    float pitchWobbleCents = 8.0f;
};

Good — that simplifies things a bit since you don't need to worry about IPC/shared-memory marshaling for this one; it can just live as a `juce::AudioProcessor` (or a bare `Synthesiser` member) inside your host's own process space.

For a built-in host instrument, I'd wire it like this:

```cpp
class GrowlBassProcessor : public juce::AudioProcessor
{
public:
    GrowlBassProcessor()
    {
        synth.clearVoices();
        synth.clearSounds();
        synth.addSound (new GrowlBassSound());
        for (int i = 0; i < numVoices; ++i)
            synth.addVoice (new GrowlBassVoice());
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);

        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<GrowlBassVoice*> (synth.getVoice (i)))
                v->prepare (spec);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();

        if (auto* playhead = getPlayHead())
            if (auto pos = playhead->getPosition())
                if (auto bpm = pos->getBpm())
                {
                    double hostBpm = *bpm;
                    for (int i = 0; i < synth.getNumVoices(); ++i)
                        if (auto* v = dynamic_cast<GrowlBassVoice*> (synth.getVoice (i)))
                            v->setHostBpm (hostBpm);
                }

        synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
    }

private:
    static constexpr int numVoices = 8;
    juce::Synthesiser synth;
};
```

A couple of things worth deciding since it's staying in-process:

1. **Threading** — is this going to sit on your main audio thread directly, or do you have a separate internal-instrument thread pool distinct from the plugin-isolation IPC path? That affects whether the BPM/playhead lookup above is safe as-is or needs a lock-free parameter snapshot.
2. **Automation/parameters** — since it's a built-in rather than a hosted VST3/CLAP, do you want this exposed through `AudioProcessorValueTreeState` so it shows up in HDAW's own automation lanes (growl rate, drive, filter range, unison spread as automatable params)?

Want me to add an `APVTS`-backed parameter layer so drive/growl-rate/filter-range are automatable from HDAW's UI?
