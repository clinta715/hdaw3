Good context — since you're working in JUCE for HDAW, here's how to add a subbass synth as a simple internal instrument (not a hosted VST/CLAP plugin, just a native `juce::Synthesiser`-based voice).

## Architecture overview

Use JUCE's built-in `juce::Synthesiser` + `SynthesiserVoice` + `SynthesiserSound` classes. A subbass synth is simple: sine or triangle oscillator (maybe with slight saturation for harmonics), fast-ish envelope, optional sub-octave layering, and a low-pass filter to keep it clean.

## 1. Define the sound class

```cpp
class SubBassSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};
```

## 2. Define the voice class

```cpp
class SubBassVoice : public juce::SynthesiserVoice
{
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SubBassSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                     juce::SynthesiserSound*, int /*pitchWheel*/) override
    {
        frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        phase = 0.0;
        level = velocity;
        env.noteOn();
        tailOff = false;
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            env.noteOff();
            tailOff = true;
        }
        else
        {
            clearCurrentNote();
            env.reset();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);
        juce::ADSR::Parameters p { 0.005f, 0.1f, 0.9f, 0.3f }; // fast attack, sustained
        env.setParameters (p);
        env.setSampleRate (newRate);
    }

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                           int startSample, int numSamples) override
    {
        if (! isVoiceActive()) return;

        auto sampleRate = getSampleRate();
        auto phaseInc = juce::MathConstants<double>::twoPi * frequency / sampleRate;

        for (int i = 0; i < numSamples; ++i)
        {
            // fundamental sine + a quiet sub-octave sine for extra weight
            float sample = (float) std::sin (phase);
            float sub    = (float) std::sin (phase * 0.5) * 0.5f;

            float mixed = (sample * 0.8f + sub * 0.2f) * level * env.getNextSample();

            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.addSample (ch, startSample + i, mixed);

            phase += phaseInc;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
        }

        if (tailOff && ! env.isActive())
            clearCurrentNote();
    }

private:
    double frequency = 110.0, phase = 0.0;
    float level = 0.0f;
    juce::ADSR env;
    bool tailOff = false;
};
```

## 3. Wire it into your processor

```cpp
juce::Synthesiser subBassSynth;

// in prepareToPlay:
subBassSynth.clearVoices();
for (int i = 0; i < 8; ++i)
    subBassSynth.addVoice (new SubBassVoice());
subBassSynth.clearSounds();
subBassSynth.addSound (new SubBassSound());
subBassSynth.setCurrentPlaybackSampleRate (sampleRate);

// in processBlock:
subBassSynth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());
```

## Sub-bass-specific tips

- **Low-pass filter it hard** — put a `juce::dsp::StateVariableTPTFilter` after the oscillator, cutoff around 150–300 Hz, to strip any harmonics that muddy the low end.
- **Mono/legato mode** matters a lot for subbass — use a single-voice `Synthesiser` (or `MPESynthesiser` with note-stealing) so bass notes glide rather than stack and phase-cancel.
- **Watch phase alignment** between the sub-octave layer and fundamental — if they drift out of phase you'll get cancellation instead of reinforcement (this is why many subbass synths just resync the sub oscillator's phase on note-on, as done above).
- **Sample-rate–independent tuning**: since you already have IPC/child-process plugin hosting in HDAW, if you ever want this to be swappable/hot-reloadable like your other plugins, you could equally build it as an internal CLAP/VST3 plugin and route it through the same isolated-host pipeline — but for a built-in instrument, keeping it in-process (as above) avoids the extra IPC latency you already noted isolation adds.

Want me to sketch the filter + saturation stage, or a MIDI-triggered mono glide voice instead of polyphonic?

Here's both, combined into a mono glide voice with filter and saturation — much better suited to actual subbass than the polyphonic version.

## Mono synth with glide (portamento)

For subbass, you typically want one voice, gliding between notes rather than retriggering the envelope on every legato note. JUCE doesn't hand this to you for free with `Synthesiser`, so it's easiest to bypass `Synthesiser`/`SynthesiserVoice` entirely and just track MIDI state yourself:

```cpp
class MonoSubBass
{
public:
    void prepare (double sampleRate, int samplesPerBlock)
    {
        sr = sampleRate;
        env.setSampleRate (sr);
        env.setParameters ({ 0.005f, 0.08f, 0.9f, 0.25f });

        juce::dsp::ProcessSpec spec { sr, (juce::uint32) samplesPerBlock, 1 };
        filter.prepare (spec);
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filter.setCutoffFrequency (220.0f);
        filter.setResonance (0.7f);
    }

    void noteOn (int midiNote, float velocity)
    {
        float newTarget = (float) juce::MidiMessage::getMidiNoteInHertz (midiNote);

        if (currentlyPlaying && glideTimeSeconds > 0.0f)
        {
            // start gliding from wherever we currently are
            targetFreq = newTarget;
            glideIncrement = (targetFreq - currentFreq) / (float) (glideTimeSeconds * sr);
        }
        else
        {
            currentFreq = targetFreq = newTarget;
            glideIncrement = 0.0f;
            phase = 0.0; // resync sub-oscillator phase only on a fresh note
        }

        level = velocity;
        env.noteOn();
        currentlyPlaying = true;
        heldNotes.add (midiNote);
    }

    void noteOff (int midiNote)
    {
        heldNotes.removeAllInstancesOf (midiNote);

        if (heldNotes.isEmpty())
        {
            env.noteOff();
            currentlyPlaying = false;
        }
        else
        {
            // legato: fall back to the next held note (monosynth note-priority = last held)
            noteOn (heldNotes.getLast(), level);
        }
    }

    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
    {
        auto* out = buffer.getWritePointer (0);

        for (int i = 0; i < numSamples; ++i)
        {
            if (glideIncrement != 0.0f)
            {
                currentFreq += glideIncrement;
                if ((glideIncrement > 0 && currentFreq >= targetFreq) ||
                    (glideIncrement < 0 && currentFreq <= targetFreq))
                {
                    currentFreq = targetFreq;
                    glideIncrement = 0.0f;
                }
            }

            double phaseInc = juce::MathConstants<double>::twoPi * currentFreq / sr;

            float fundamental = (float) std::sin (phase);
            float sub         = (float) std::sin (phase * 0.5) * 0.5f;
            float raw = fundamental * 0.8f + sub * 0.2f;

            // soft saturation — tanh drive for a bit of harmonic grit/definition
            float driven = std::tanh (raw * driveAmount) / std::tanh (driveAmount);

            float sample = driven * level * env.getNextSample();
            out[startSample + i] = sample;

            phase += phaseInc;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
        }

        // run the block through the low-pass filter (mono)
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 1,
                                             (size_t) startSample, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        filter.process (ctx);

        // copy mono result to other channels if needed
        for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom (ch, startSample, buffer, 0, startSample, numSamples);
    }

    void setGlideTime (float seconds) { glideTimeSeconds = seconds; }
    void setDrive (float amount) { driveAmount = juce::jmax (0.01f, amount); }
    void setCutoff (float hz) { filter.setCutoffFrequency (hz); }

private:
    double sr = 44100.0, phase = 0.0;
    float currentFreq = 55.0f, targetFreq = 55.0f, glideIncrement = 0.0f;
    float glideTimeSeconds = 0.05f;
    float level = 0.0f, driveAmount = 1.5f;
    bool currentlyPlaying = false;
    juce::Array<int> heldNotes;

    juce::ADSR env;
    juce::dsp::StateVariableTPTFilter<float> filter;
};
```

## Notes on tuning it

- **`driveAmount`**: 1.0 = no saturation, higher = more tanh drive. Divide by `tanh(driveAmount)` to keep output level roughly normalized as drive increases. 1.5–3.0 is a good subbass range — adds harmonics that make it audible on small speakers without pushing the fundamental up in pitch perception.
- **`glideTimeSeconds`**: 0 = no glide (retriggers cleanly note to note), 0.03–0.08s is typical "slidey" subbass. You can tie this to a knob or a "legato glide" toggle in your UI.
- **Filter cutoff**: 200–350 Hz is a reasonable default; lower it further if you're layering with a separate low-passed kick to keep them from fighting.
- **This is intentionally mono** (one instance, one voice) — if you want subbass triggered polyphonically anyway (unusual, but some styles do it), you'd go back to the `Synthesiser`/`SynthesiserVoice` version from before instead.

Want a UI panel (knobs for drive/cutoff/glide) to go with this, or MIDI routing into your existing HDAW plugin-hosting pipeline?

Since this is a native in-process instrument rather than an isolated VST3/CLAP plugin, MIDI routing needs to plug into whatever track/graph structure HDAW already uses for the isolated plugins, but bypass the IPC layer entirely — no child process, no shared-memory ring buffer needed since it's a native `AudioProcessor` living directly in your audio graph.

## Typical approach: wrap it as an `AudioProcessor`

The cleanest way to make it a first-class citizen in your JUCE `AudioProcessorGraph` (assuming HDAW uses one for track routing, which is standard for a DAW) is to wrap `MonoSubBass` in a small internal `AudioProcessor`:

```cpp
class SubBassProcessor : public juce::AudioProcessor
{
public:
    SubBassProcessor()
        : AudioProcessor (BusesProperties()
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {}

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        synth.prepare (sampleRate, samplesPerBlock);
    }

    void processBlock (juce::AudioBuffer<float>& buffer,
                        juce::MidiBuffer& midiMessages) override
    {
        buffer.clear();

        // walk MIDI buffer, split into sub-blocks at each event so glide/env
        // updates land on the correct sample
        int samplePos = 0;
        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            int eventSample = metadata.samplePosition;

            if (eventSample > samplePos)
                synth.renderNextBlock (buffer, samplePos, eventSample - samplePos);

            if (msg.isNoteOn())
                synth.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
                synth.noteOff (msg.getNoteNumber());

            samplePos = eventSample;
        }

        if (samplePos < buffer.getNumSamples())
            synth.renderNextBlock (buffer, samplePos, buffer.getNumSamples() - samplePos);
    }

    // boilerplate AudioProcessor overrides
    const juce::String getName() const override { return "SubBass"; }
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    MonoSubBass synth;
};
```

## Adding it to the graph and routing MIDI

If HDAW's tracks are `AudioProcessorGraph::Node`s (typical for a JUCE DAW, and consistent with hosting isolated plugins as nodes too):

```cpp
// when creating the track/node:
auto node = graph.addNode (std::make_unique<SubBassProcessor>());

// route track's MIDI input to it, same as you'd route MIDI to any hosted plugin:
graph.addConnection ({ { midiInputNodeID, juce::AudioProcessorGraph::midiChannelIndex },
                        { node->nodeID,   juce::AudioProcessorGraph::midiChannelIndex } });

// route its audio output into the track's mixer/output chain:
graph.addConnection ({ { node->nodeID, 0 }, { trackOutputNodeID, 0 } });
graph.addConnection ({ { node->nodeID, 0 }, { trackOutputNodeID, 1 } }); // mono->stereo if needed
```

This means the subbass node sits in the *same* graph as your isolated plugin host nodes, gets scheduled by the graph the same way, but never touches the named-pipe/shared-memory IPC path — it's just a regular in-process node, so no extra latency block like your isolated plugins incur.

## A few routing considerations specific to your setup

- **MIDI channel filtering**: if a track can host both this internal synth and external plugins, make sure only one is "armed" to receive the track's MIDI at a time (or make it explicit in your UI which slot is active), otherwise you'll double-trigger notes.
- **Latency reporting**: since this bypasses isolation, it reports 0 added latency (via `getLatencySamples()`), which matters if your graph does automatic latency compensation across other (isolated) plugin nodes — you'd want the graph's PDC to treat this node as zero-latency correctly, not assume the isolation's ~1-block offset.
- **Export/offline rendering**: this node already needs no special-casing for your RAII isolation-bypass-on-export guard, since it was never isolated in the first place — it behaves identically in both live and offline render paths.

Want the corresponding editor/UI component for it, or how to make it swappable/preset-savable via `getStateInformation`/`setStateInformation`?

