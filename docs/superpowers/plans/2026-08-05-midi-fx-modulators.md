# MIDI FX Modulators Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 8 new MIDI note FX (Transpose, Key Filter, Multi-Note, Velocity Curve, Note Chance, MIDI Delay, Humanize, Strum) plus the missing `setMidiFxSlotParam` command, parameter editing UI, and MCP tools.

**Architecture:** Each effect is a new `MidiEffect` subclass in `MidiFx.h`, instantiated by `Track::rebuildMidiFXChain()` from the `MIDI_FX_CHAIN` ValueTree. Parameters are stored as named properties on the `MIDI_FX_SLOT` node. A new `setMidiFxSlotParam` command enables runtime param editing. The frontend `MidiFxChain` component gets inline param controls per slot. MCP tools mirror the RPC surface.

**Tech Stack:** C++ (JUCE), React 19 + TypeScript (Zustand), Vitest, gtest, Playwright.

---

## File Map

| File | Change |
|------|--------|
| `src/engine/MidiFx.h` | Add 8 new `MidiEffect` subclasses |
| `src/model/ProjectModel.h` | Add `DECLARE_ID` for all new param names |
| `src/engine/Track.cpp` | Add 8 instantiation cases in `rebuildMidiFXChain()` |
| `src/engine/AudioEngineCommands_Fx.cpp` | Add defaults in `addMidiFxSlot()`, add `setMidiFxSlotParam()` impl |
| `src/engine/AudioEngineCommands.h` | Declare `setMidiFxSlotParam` override |
| `src/common/ProjectCommands.h` | Declare virtual `setMidiFxSlotParam` |
| `src/common/ReadModel.h` | Add `params` map to `MidiFxSlotSnapshot` |
| `src/engine/ReadModelImpl.cpp` | Populate `params` in `getMidiFxSlots()` |
| `src/frontend/FrontendRpc.h` | Serialize `params` in `toJson(MidiFxSlotSnapshot)` |
| `src/frontend/FrontendRouter.cpp` | Route `setMidiFxSlotParam` RPC |
| `frontend/src/rpc/types.ts` | Add `params` to `MidiFxSlotSnapshot` |
| `frontend/src/components/MidiFxChain.tsx` | Add new FX types + inline param editors |
| `frontend/src/components/MidiFxChain.css` | Style param controls |
| `src/mcp/McpTools_Audio.cpp` | Add `registerMidiFxTools()` with 4 tools |
| `tests/unit/engine/midi_fx_test.cpp` | Add unit tests for all 8 effects |
| `tests/unit/engine/commands_test.cpp` | Add test for `setMidiFxSlotParam` |
| `tests/unit/common/rpc_surface_test.cpp` | Add test for params in ReadModel |
| `frontend/src/components/MidiFxChain.test.tsx` | Update for new types + param editing |

---

## Task 1: ValueTree IDs + MidiEffect subclasses

**Files:**
- Modify: `src/model/ProjectModel.h:109-119`
- Modify: `src/engine/MidiFx.h`

### Step 1: Add DECLARE_ID entries for all new params

In `src/model/ProjectModel.h`, after line 119 (`DECLARE_ID(lengthFactor)`), add:

```cpp
    // Transpose
    DECLARE_ID(semitones)
    // Key Filter
    DECLARE_ID(keyFilterRoot)
    DECLARE_ID(keyFilterScale)
    // Multi-Note
    DECLARE_ID(multiNoteIntervals)
    // Velocity Curve
    DECLARE_ID(curveType)
    DECLARE_ID(curveAmount)
    // Note Chance
    DECLARE_ID(noteChance)
    // MIDI Delay
    DECLARE_ID(delayBeats)
    DECLARE_ID(delayFeedback)
    DECLARE_ID(delayMix)
    // Humanize
    DECLARE_ID(humanizeTiming)
    DECLARE_ID(humanizeVelocity)
    DECLARE_ID(humanizePitch)
    // Strum
    DECLARE_ID(strumTime)
    DECLARE_ID(strumDirection)
```

### Step 2: Add 8 MidiEffect subclasses to MidiFx.h

Add after the `NoteLengthScaler` class (before `MidiFxSlot`):

```cpp
// Transposes all notes by a semitone offset.
class Transpose : public MidiEffect
{
public:
    int semitones = 0;

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo*,
                 double, int) override
    {
        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                int n = juce::jlimit(0, 127, msg.getNoteNumber() + semitones);
                out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), n,
                             static_cast<juce::uint8>(msg.getVelocity())), meta.samplePosition);
            }
            else if (msg.isNoteOff())
            {
                int n = juce::jlimit(0, 127, msg.getNoteNumber() + semitones);
                out.addEvent(juce::MidiMessage::noteOff(msg.getChannel(), n), meta.samplePosition);
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }
};

// Filters out notes not in the chosen key/scale. Unlike ScaleQuantize which
// snaps, this rejects — notes outside the scale are dropped entirely.
class KeyFilter : public MidiEffect
{
public:
    int root = 0;
    int scaleType = 0; // 0=major, 1=natural minor, 2=chromatic

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo*,
                 double, int) override
    {
        const auto intervals = intervalsFor(scaleType);
        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                int pc = (msg.getNoteNumber() - root + 120) % 12;
                bool inScale = std::find(intervals.begin(), intervals.end(), pc) != intervals.end();
                if (inScale)
                    out.addEvent(msg, meta.samplePosition);
            }
            else if (msg.isNoteOff())
            {
                int pc = (msg.getNoteNumber() - root + 120) % 12;
                bool inScale = std::find(intervals.begin(), intervals.end(), pc) != intervals.end();
                if (inScale)
                    out.addEvent(msg, meta.samplePosition);
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }

private:
    static std::vector<int> intervalsFor(int type)
    {
        switch (type)
        {
            case 1: return { 0, 2, 3, 5, 7, 8, 10 };
            case 2: return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
            default: return { 0, 2, 4, 5, 7, 9, 11 };
        }
    }
};

// Generates additional notes at fixed interval offsets from each input note.
class MultiNote : public MidiEffect
{
public:
    std::vector<int> intervals = { 0 };

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo*,
                 double, int) override
    {
        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                for (int iv : intervals)
                {
                    int n = juce::jlimit(0, 127, msg.getNoteNumber() + iv);
                    out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), n,
                                 static_cast<juce::uint8>(msg.getVelocity())), meta.samplePosition);
                }
            }
            else if (msg.isNoteOff())
            {
                for (int iv : intervals)
                {
                    int n = juce::jlimit(0, 127, msg.getNoteNumber() + iv);
                    out.addEvent(juce::MidiMessage::noteOff(msg.getChannel(), n), meta.samplePosition);
                }
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }
};

// Reshapes velocity with a curve. curveType: 0=linear, 1=compress, 2=expand,
// 3=randomize, 4=fixed (all notes get amount*127).
class VelocityCurve : public MidiEffect
{
public:
    int curveType = 0;
    double amount = 0.5;

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo*,
                 double, int) override
    {
        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                double norm = msg.getVelocity() / 127.0;
                double result;
                switch (curveType)
                {
                    case 1: // compress — push toward center
                        result = norm + (0.5 - norm) * amount;
                        break;
                    case 2: // expand — push away from center
                        result = norm + (norm - 0.5) * amount;
                        break;
                    case 3: // randomize
                        result = norm + (static_cast<double>(rand()) / RAND_MAX - 0.5) * amount;
                        break;
                    case 4: // fixed
                        result = amount;
                        break;
                    default: // linear — scale
                        result = norm * amount * 2.0;
                        break;
                }
                int v = static_cast<int>(std::lround(result * 127.0));
                out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), msg.getNoteNumber(),
                             static_cast<juce::uint8>(juce::jlimit(1, 127, v))), meta.samplePosition);
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }
};

// Drops notes randomly at the track level with a probability.
class NoteChance : public MidiEffect
{
public:
    double probability = 1.0;

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo*,
                 double, int) override
    {
        if (probability >= 1.0) return;
        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                if (static_cast<double>(rand()) / RAND_MAX < probability)
                    out.addEvent(msg, meta.samplePosition);
            }
            else if (msg.isNoteOff())
            {
                // Always pass note-offs to avoid stuck notes
                out.addEvent(msg, meta.samplePosition);
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }
};

// Delays notes by a beat fraction with optional feedback.
class MidiDelay : public MidiEffect
{
public:
    double delayBeats = 0.25;
    double feedback = 0.0;
    double mix = 0.5;

    void reset() override
    {
        pending.clear();
    }

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples) override
    {
        const double bpm = position != nullptr ? position->getBpm().orFallback(120.0) : 120.0;
        const double blockStart = position != nullptr ? position->getPpqPosition().orFallback(0.0) : 0.0;
        const double beatsPerSample = sampleRate > 0 ? bpm / 60.0 / sampleRate : 0.0;
        const double blockEnd = blockStart + numSamples * beatsPerSample;

        juce::MidiBuffer out;

        // Emit pending delayed notes that fall in this block
        for (auto it = pending.begin(); it != pending.end(); )
        {
            if (it->triggerBeat < blockEnd)
            {
                int sample = beatsPerSample > 0
                    ? static_cast<int>((it->triggerBeat - blockStart) / beatsPerSample) : 0;
                sample = juce::jlimit(0, numSamples - 1, sample);
                if (it->isNoteOn)
                    out.addEvent(juce::MidiMessage::noteOn(it->channel, it->note,
                                 static_cast<juce::uint8>(it->velocity)), sample);
                else
                    out.addEvent(juce::MidiMessage::noteOff(it->channel, it->note), sample);

                // Feedback: schedule another delay if feedback > 0
                if (feedback > 0.0 && it->isNoteOn)
                {
                    PendingNote fb;
                    fb.note = it->note;
                    fb.channel = it->channel;
                    fb.velocity = static_cast<int>(it->velocity * feedback);
                    fb.triggerBeat = it->triggerBeat + delayBeats;
                    fb.isNoteOn = true;
                    if (fb.velocity >= 1)
                        pending.push_back(fb);

                    PendingNote fbOff;
                    fbOff.note = it->note;
                    fbOff.channel = it->channel;
                    fbOff.velocity = 0;
                    fbOff.triggerBeat = it->triggerBeat + delayBeats * 0.5;
                    fbOff.isNoteOn = false;
                    pending.push_back(fbOff);
                }
                it = pending.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Process input: mix original + schedule delayed
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            const double beat = blockStart + meta.samplePosition * beatsPerSample;

            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                // Original (dry)
                if (mix > 0.0)
                    out.addEvent(msg, meta.samplePosition);

                // Schedule delayed (wet)
                PendingNote p;
                p.note = msg.getNoteNumber();
                p.channel = msg.getChannel();
                p.velocity = static_cast<int>(msg.getVelocity() * mix);
                p.triggerBeat = beat + delayBeats;
                p.isNoteOn = true;
                if (p.velocity >= 1)
                    pending.push_back(p);
            }
            else if (msg.isNoteOff())
            {
                if (mix > 0.0)
                    out.addEvent(msg, meta.samplePosition);

                PendingNote p;
                p.note = msg.getNoteNumber();
                p.channel = msg.getChannel();
                p.velocity = 0;
                p.triggerBeat = beat + delayBeats;
                p.isNoteOn = false;
                pending.push_back(p);
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }

private:
    struct PendingNote {
        int note = 0;
        int channel = 1;
        int velocity = 0;
        double triggerBeat = 0.0;
        bool isNoteOn = true;
    };
    std::vector<PendingNote> pending;
};

// Randomizes timing, velocity, and pitch for an organic feel.
class Humanize : public MidiEffect
{
public:
    double timingAmount = 0.0;   // 0-1, fraction of 1/32 note
    double velocityAmount = 0.0; // 0-1, fraction of 127
    double pitchAmount = 0.0;    // 0-1, fraction of 1 semitone

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples) override
    {
        const double bpm = position != nullptr ? position->getBpm().orFallback(120.0) : 120.0;
        const double beatsPerSample = sampleRate > 0 ? bpm / 60.0 / sampleRate : 0.0;
        // 1/32 note in samples, used as max timing offset
        const double maxTimingSamples = beatsPerSample > 0
            ? (0.125 / beatsPerSample) : 0.0; // 0.125 beats = 1/32

        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                // Timing jitter
                int timingOffset = 0;
                if (timingAmount > 0.0)
                {
                    double r = (static_cast<double>(rand()) / RAND_MAX - 0.5) * 2.0;
                    timingOffset = static_cast<int>(r * timingAmount * maxTimingSamples);
                }
                int newSample = juce::jlimit(0, numSamples - 1, meta.samplePosition + timingOffset);

                // Velocity jitter
                int vel = msg.getVelocity();
                if (velocityAmount > 0.0)
                {
                    double r = (static_cast<double>(rand()) / RAND_MAX - 0.5) * 2.0;
                    vel = static_cast<int>(vel + r * velocityAmount * 127.0);
                    vel = juce::jlimit(1, 127, vel);
                }

                // Pitch jitter (only for note-on; note-off must match)
                int note = msg.getNoteNumber();
                if (pitchAmount > 0.0)
                {
                    double r = (static_cast<double>(rand()) / RAND_MAX - 0.5) * 2.0;
                    note = juce::jlimit(0, 127, note + static_cast<int>(r * pitchAmount));
                }

                out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), note,
                             static_cast<juce::uint8>(vel)), newSample);
            }
            else if (msg.isNoteOff())
            {
                out.addEvent(msg, meta.samplePosition);
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }
};

// Staggers chord notes in time (strum effect). Collects simultaneous notes
// and spreads them across strumTime beats.
class Strum : public MidiEffect
{
public:
    double strumTime = 0.02; // beats
    int direction = 0;       // 0=up, 1=down, 2=random

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples) override
    {
        const double bpm = position != nullptr ? position->getBpm().orFallback(120.0) : 120.0;
        const double beatsPerSample = sampleRate > 0 ? bpm / 60.0 / sampleRate : 0.0;

        // Collect all note-on events at sample 0 (or same position)
        struct NoteEvent { int note; int velocity; int channel; };
        std::vector<NoteEvent> chordNotes;
        juce::MidiBuffer out;

        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0 && meta.samplePosition == 0)
            {
                chordNotes.push_back({ msg.getNoteNumber(), msg.getVelocity(), msg.getChannel() });
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }

        if (chordNotes.size() <= 1)
        {
            // Single note or no notes — pass through as-is
            for (const auto meta : buffer)
                out.addEvent(meta.getMessage(), meta.samplePosition);
            buffer = out;
            return;
        }

        // Sort by pitch
        std::sort(chordNotes.begin(), chordNotes.end(),
                  [](const NoteEvent& a, const NoteEvent& b) { return a.note < b.note; });
        if (direction == 1)
            std::reverse(chordNotes.begin(), chordNotes.end());

        // Spread notes across strumTime
        double step = chordNotes.size() > 1
            ? strumTime / (chordNotes.size() - 1) : 0.0;

        for (size_t i = 0; i < chordNotes.size(); ++i)
        {
            double beatOffset = i * step;
            if (direction == 2)
                beatOffset = (static_cast<double>(rand()) / RAND_MAX) * strumTime;

            int sample = beatsPerSample > 0
                ? static_cast<int>(beatOffset / beatsPerSample) : 0;
            sample = juce::jlimit(0, numSamples - 1, sample);

            out.addEvent(juce::MidiMessage::noteOn(chordNotes[i].channel, chordNotes[i].note,
                         static_cast<juce::uint8>(chordNotes[i].velocity)), sample);
        }

        buffer = out;
    }
};
```

### Step 3: Build to verify compilation

Run: `cmake --build build --config Debug`

Expected: Compiles without errors.

---

## Task 2: Track rebuild + AudioEngineCommands defaults + setMidiFxSlotParam command

**Files:**
- Modify: `src/engine/Track.cpp:224-276`
- Modify: `src/engine/AudioEngineCommands_Fx.cpp:93-141`
- Modify: `src/common/ProjectCommands.h:143-145`
- Modify: `src/engine/AudioEngineCommands.h`

### Step 1: Add 8 instantiation cases in Track::rebuildMidiFXChain

In `src/engine/Track.cpp`, inside `rebuildMidiFXChain()`, after the `notelength` block (line 268) and before `if (effect)` (line 269), add:

```cpp
        else if (type == "transpose")
        {
            auto t = std::make_unique<Transpose>();
            t->semitones = static_cast<int>(slotTree.getProperty(IDs::semitones, 0));
            effect = std::move(t);
        }
        else if (type == "keyfilter")
        {
            auto kf = std::make_unique<KeyFilter>();
            kf->root = static_cast<int>(slotTree.getProperty(IDs::keyFilterRoot, 0));
            kf->scaleType = static_cast<int>(slotTree.getProperty(IDs::keyFilterScale, 0));
            effect = std::move(kf);
        }
        else if (type == "multinote")
        {
            auto mn = std::make_unique<MultiNote>();
            juce::String ivStr = slotTree.getProperty(IDs::multiNoteIntervals, "0").toString();
            mn->intervals.clear();
            for (auto& tok : juce::StringArray::fromTokens(ivStr, ",", ""))
            {
                int iv = tok.trim().getIntValue();
                mn->intervals.push_back(iv);
            }
            if (mn->intervals.empty()) mn->intervals.push_back(0);
            effect = std::move(mn);
        }
        else if (type == "velocitycurve")
        {
            auto vc = std::make_unique<VelocityCurve>();
            vc->curveType = static_cast<int>(slotTree.getProperty(IDs::curveType, 0));
            vc->amount = static_cast<double>(slotTree.getProperty(IDs::curveAmount, 0.5));
            effect = std::move(vc);
        }
        else if (type == "notechance")
        {
            auto nc = std::make_unique<NoteChance>();
            nc->probability = static_cast<double>(slotTree.getProperty(IDs::noteChance, 1.0));
            effect = std::move(nc);
        }
        else if (type == "mididelay")
        {
            auto md = std::make_unique<MidiDelay>();
            md->delayBeats = static_cast<double>(slotTree.getProperty(IDs::delayBeats, 0.25));
            md->feedback = static_cast<double>(slotTree.getProperty(IDs::delayFeedback, 0.0));
            md->mix = static_cast<double>(slotTree.getProperty(IDs::delayMix, 0.5));
            effect = std::move(md);
        }
        else if (type == "humanize")
        {
            auto h = std::make_unique<Humanize>();
            h->timingAmount = static_cast<double>(slotTree.getProperty(IDs::humanizeTiming, 0.0));
            h->velocityAmount = static_cast<double>(slotTree.getProperty(IDs::humanizeVelocity, 0.0));
            h->pitchAmount = static_cast<double>(slotTree.getProperty(IDs::humanizePitch, 0.0));
            effect = std::move(h);
        }
        else if (type == "strum")
        {
            auto st = std::make_unique<Strum>();
            st->strumTime = static_cast<double>(slotTree.getProperty(IDs::strumTime, 0.02));
            st->direction = static_cast<int>(slotTree.getProperty(IDs::strumDirection, 0));
            effect = std::move(st);
        }
```

### Step 2: Add default params in addMidiFxSlot

In `src/engine/AudioEngineCommands_Fx.cpp`, inside `addMidiFxSlot()`, after the `notelength` block (line 133) and before `int n = chain.getNumChildren()` (line 135), add:

```cpp
    else if (type == "transpose")
    {
        slot.setProperty(IDs::semitones, 0, &um);
    }
    else if (type == "keyfilter")
    {
        slot.setProperty(IDs::keyFilterRoot, 0, &um);
        slot.setProperty(IDs::keyFilterScale, 0, &um);
    }
    else if (type == "multinote")
    {
        slot.setProperty(IDs::multiNoteIntervals, "0,7,12", &um);
    }
    else if (type == "velocitycurve")
    {
        slot.setProperty(IDs::curveType, 0, &um);
        slot.setProperty(IDs::curveAmount, 0.5, &um);
    }
    else if (type == "notechance")
    {
        slot.setProperty(IDs::noteChance, 1.0, &um);
    }
    else if (type == "mididelay")
    {
        slot.setProperty(IDs::delayBeats, 0.25, &um);
        slot.setProperty(IDs::delayFeedback, 0.0, &um);
        slot.setProperty(IDs::delayMix, 0.5, &um);
    }
    else if (type == "humanize")
    {
        slot.setProperty(IDs::humanizeTiming, 0.0, &um);
        slot.setProperty(IDs::humanizeVelocity, 0.0, &um);
        slot.setProperty(IDs::humanizePitch, 0.0, &um);
    }
    else if (type == "strum")
    {
        slot.setProperty(IDs::strumTime, 0.02, &um);
        slot.setProperty(IDs::strumDirection, 0, &um);
    }
```

### Step 3: Add setMidiFxSlotParam to ProjectCommands.h

In `src/common/ProjectCommands.h`, after line 145 (`virtual void setMidiFxSlotBypassed(...)`), add:

```cpp
    virtual void setMidiFxSlotParam(int trackIndex, int slotIndex,
                                    const std::string& paramName, double value) = 0;
```

### Step 4: Add setMidiFxSlotParam to AudioEngineCommands.h

In `src/engine/AudioEngineCommands.h`, add the declaration alongside the other MIDI FX commands:

```cpp
    void setMidiFxSlotParam(int trackIndex, int slotIndex,
                            const std::string& paramName, double value) override;
```

### Step 5: Implement setMidiFxSlotParam in AudioEngineCommands_Fx.cpp

After `setMidiFxSlotBypassed` (around line 161), add:

```cpp
void AudioEngineCommands::setMidiFxSlotParam(int trackIndex, int slotIndex,
                                              const std::string& paramName, double value)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto slot = findMidiFxSlot(trackIndex, slotIndex);
    if (!slot.isValid()) return;

    slot.setProperty(juce::Identifier(paramName), value, &um);

    if (auto* proc = engine_.getMainProcessor())
        proc->rebuildMidiTrackFX(trackIndex);
}
```

### Step 6: Build to verify

Run: `cmake --build build --config Debug`

Expected: Compiles without errors.

---

## Task 3: ReadModel + RPC routing

**Files:**
- Modify: `src/common/ReadModel.h:117-121`
- Modify: `src/engine/ReadModelImpl.cpp:426-444`
- Modify: `src/frontend/FrontendRpc.h:180-186`
- Modify: `src/frontend/FrontendRouter.cpp:437`

### Step 1: Add params map to MidiFxSlotSnapshot

In `src/common/ReadModel.h`, replace the `MidiFxSlotSnapshot` struct:

```cpp
struct MidiFxSlotSnapshot {
    int slotIndex = 0;
    std::string fxType;
    bool bypassed = false;
    std::map<std::string, double> params;
};
```

### Step 2: Populate params in ReadModelImpl::getMidiFxSlots

In `src/engine/ReadModelImpl.cpp`, inside the loop in `getMidiFxSlots()`, after `s.bypassed = slot.getProperty(IDs::bypassed, false);` (line 443), add:

```cpp
        // Collect all non-standard properties as params
        static const std::set<juce::String> reserved = { "fxType", "bypassed" };
        for (int p = 0; p < slot.getNumProperties(); ++p)
        {
            auto name = slot.getPropertyName(p).toString();
            if (reserved.count(name) == 0)
            {
                auto val = slot.getProperty(slot.getPropertyName(p));
                if (val.isDouble() || val.isInt() || val.isInt64())
                    s.params[name.toStdString()] = static_cast<double>(val);
                else if (val.isString())
                    s.params[name.toStdString()] = 0.0; // string params stored separately
            }
        }
```

Also add `#include <set>` at the top if not already present.

### Step 3: Update toJson for MidiFxSlotSnapshot

In `src/frontend/FrontendRpc.h`, replace the `toJson(const MidiFxSlotSnapshot& f)` function:

```cpp
inline QJsonObject toJson(const MidiFxSlotSnapshot& f) {
    QJsonObject obj{
        { "slotIndex", f.slotIndex },
        { "fxType",    QString::fromStdString(f.fxType) },
        { "bypassed",  f.bypassed },
    };
    QJsonObject params;
    for (auto& [k, v] : f.params)
        params[QString::fromStdString(k)] = v;
    obj["params"] = params;
    return obj;
}
```

### Step 4: Route setMidiFxSlotParam RPC

In `src/frontend/FrontendRouter.cpp`, after the `setMidiFxSlotBypassed` line (line 437), add:

```cpp
    if (m == "setMidiFxSlotParam") {
        int i, s; std::string paramName; double v;
        if (!requireInt(o, "trackIndex", i, nullptr) || !requireInt(o, "slotIndex", s, nullptr)
            || !requireString(o, "paramName", paramName, nullptr) || !requireDouble(o, "value", v, nullptr))
            return makeError(-32602, "trackIndex, slotIndex, paramName, value required");
        c.setMidiFxSlotParam(i, s, paramName, v);
        return { false, QJsonValue::Null };
    }
```

Note: check if `requireFloat` takes a `float*` or `double*` — adjust cast accordingly. Also check if there's a `requireDouble` helper.

### Step 5: Build to verify

Run: `cmake --build build --config Debug`

Expected: Compiles without errors.

---

## Task 4: Frontend types + component updates

**Files:**
- Modify: `frontend/src/rpc/types.ts:141-145`
- Modify: `frontend/src/components/MidiFxChain.tsx`
- Modify: `frontend/src/components/MidiFxChain.css`

### Step 1: Update MidiFxSlotSnapshot interface

In `frontend/src/rpc/types.ts`, replace the `MidiFxSlotSnapshot` interface:

```typescript
export interface MidiFxSlotSnapshot {
  slotIndex: number;
  fxType: string;
  bypassed: boolean;
  params: Record<string, number>;
}
```

### Step 2: Update MIDI_FX_TYPES list

In `frontend/src/components/MidiFxChain.tsx`, replace the `MIDI_FX_TYPES` array:

```typescript
const MIDI_FX_TYPES = [
  { type: "arpeggiator", label: "Arpeggiator" },
  { type: "velocity", label: "Velocity" },
  { type: "chord", label: "Chord" },
  { type: "scale", label: "Scale Quantize" },
  { type: "notelength", label: "Note Length" },
  { type: "transpose", label: "Transpose" },
  { type: "keyfilter", label: "Key Filter" },
  { type: "multinote", label: "Multi-Note" },
  { type: "velocitycurve", label: "Velocity Curve" },
  { type: "notechance", label: "Note Chance" },
  { type: "mididelay", label: "MIDI Delay" },
  { type: "humanize", label: "Humanize" },
  { type: "strum", label: "Strum" },
];
```

### Step 3: Add param editing config per FX type

Add a config map after `MIDI_FX_TYPES`:

```typescript
const PARAM_CONFIGS: Record<string, { key: string; label: string; min: number; max: number; step: number; type?: string }[]> = {
  arpeggiator: [
    { key: "arpRate", label: "Rate", min: 0.0625, max: 2, step: 0.0625 },
    { key: "arpPattern", label: "Pattern", min: 0, max: 2, step: 1 },
    { key: "arpOctaves", label: "Octaves", min: 1, max: 4, step: 1 },
    { key: "arpGate", label: "Gate", min: 0.1, max: 1, step: 0.05 },
  ],
  velocity: [
    { key: "velFactor", label: "Factor", min: 0, max: 2, step: 0.05 },
  ],
  chord: [
    { key: "chordType", label: "Type", min: 0, max: 3, step: 1 },
  ],
  scale: [
    { key: "scaleRoot", label: "Root", min: 0, max: 11, step: 1 },
    { key: "scaleType", label: "Scale", min: 0, max: 2, step: 1 },
  ],
  notelength: [
    { key: "lengthFactor", label: "Factor", min: 0.1, max: 4, step: 0.1 },
  ],
  transpose: [
    { key: "semitones", label: "Semitones", min: -24, max: 24, step: 1 },
  ],
  keyfilter: [
    { key: "keyFilterRoot", label: "Root", min: 0, max: 11, step: 1 },
    { key: "keyFilterScale", label: "Scale", min: 0, max: 2, step: 1 },
  ],
  multinote: [
    { key: "multiNoteIntervals", label: "Intervals", min: -24, max: 24, step: 1, type: "text" },
  ],
  velocitycurve: [
    { key: "curveType", label: "Curve", min: 0, max: 4, step: 1 },
    { key: "curveAmount", label: "Amount", min: 0, max: 1, step: 0.05 },
  ],
  notechance: [
    { key: "noteChance", label: "Probability", min: 0, max: 1, step: 0.05 },
  ],
  mididelay: [
    { key: "delayBeats", label: "Delay", min: 0.0625, max: 2, step: 0.0625 },
    { key: "delayFeedback", label: "Feedback", min: 0, max: 0.95, step: 0.05 },
    { key: "delayMix", label: "Mix", min: 0, max: 1, step: 0.05 },
  ],
  humanize: [
    { key: "humanizeTiming", label: "Timing", min: 0, max: 1, step: 0.05 },
    { key: "humanizeVelocity", label: "Velocity", min: 0, max: 1, step: 0.05 },
    { key: "humanizePitch", label: "Pitch", min: 0, max: 1, step: 0.05 },
  ],
  strum: [
    { key: "strumTime", label: "Time", min: 0, max: 0.5, step: 0.005 },
    { key: "strumDirection", label: "Direction", min: 0, max: 2, step: 1 },
  ],
};
```

### Step 4: Add setParam callback and param editor rendering

Add a `setParam` callback in the component:

```typescript
const setParam = useCallback(async (slotIndex: number, paramName: string, value: number) => {
    if (selectedTrackIndex == null) return;
    try {
      await rpc.call("project.setMidiFxSlotParam", {
        trackIndex: selectedTrackIndex,
        slotIndex,
        paramName,
        value,
      });
      refresh();
    } catch (err) {
      reportRpcError("project.setMidiFxSlotParam", err);
    }
  }, [selectedTrackIndex, refresh]);
```

In the slot rendering, after the type label and before the buttons, add a param editor section:

```tsx
{(PARAM_CONFIGS[slot.fxType] ?? []).map((p) => (
  <label key={p.key} className="mfx-param">
    <span className="mfx-param-label">{p.label}</span>
    {p.type === "text" ? (
      <input
        className="mfx-param-input"
        type="text"
        value={String(slot.params[p.key] ?? "")}
        onChange={(e) => setParam(slot.slotIndex, p.key, parseFloat(e.target.value) || 0)}
      />
    ) : (
      <input
        className="mfx-param-slider"
        type="range"
        min={p.min}
        max={p.max}
        step={p.step}
        value={slot.params[p.key] ?? p.min}
        onChange={(e) => setParam(slot.slotIndex, p.key, parseFloat(e.target.value))}
      />
    )}
  </label>
))}
```

### Step 5: Add CSS for param controls

In `frontend/src/components/MidiFxChain.css`, add:

```css
.mfx-param {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
}

.mfx-param-label {
  min-width: 50px;
  color: var(--text-secondary, #999);
}

.mfx-param-slider {
  flex: 1;
  min-width: 60px;
  height: 4px;
  accent-color: var(--accent, #d97706);
}

.mfx-param-input {
  width: 60px;
  background: var(--bg-input, #2a2a2a);
  border: 1px solid var(--border, #444);
  color: var(--text-primary, #eee);
  padding: 1px 4px;
  font-size: 11px;
}
```

### Step 6: Build frontend

Run: `cd frontend && npm run build`

Expected: Builds without errors.

---

## Task 5: MCP tools

**Files:**
- Modify: `src/mcp/McpTools_Audio.cpp`

### Step 1: Add registerMidiFxTools function

At the end of `McpTools_Audio.cpp`, add a new function:

```cpp
void registerMidiFxTools(McpServer& s, AudioEngine* e)
{
    s.registerTool({"add_midi_fx",
        "Add a MIDI FX slot to a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"fxType", QJsonObject{{"type","string"},
                       {"enum", QJsonArray{"arpeggiator","velocity","chord","scale","notelength",
                                           "transpose","keyfilter","multinote","velocitycurve",
                                           "notechance","mididelay","humanize","strum"}}}},
                   {"position", QJsonObject{{"type","integer"}}}}, {"trackId","fxType"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            auto tl = e->getProjectModel().getTrackListTree();
            if (ti < 0 || ti >= tl.getNumChildren())
                return McpToolResult::text("track not found", true);
            std::string type = a.value("fxType").toString().toStdString();
            int pos = a.value("position").toInt(-1);
            e->getProjectCommands().addMidiFxSlot(ti, type, pos);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"remove_midi_fx",
        "Remove a MIDI FX slot from a track.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}}}, {"trackId","slotIndex"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            e->getProjectCommands().removeMidiFxSlot(ti, si);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_midi_fx_bypass",
        "Bypass or unbypass a MIDI FX slot.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"bypassed", QJsonObject{{"type","boolean"}}}}, {"trackId","slotIndex","bypassed"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            bool b = a.value("bypassed").toBool();
            e->getProjectCommands().setMidiFxSlotBypassed(ti, si, b);
            return McpToolResult::text("ok");
        }});

    s.registerTool({"set_midi_fx_param",
        "Set a parameter on a MIDI FX slot.",
        objSchema({{"trackId", QJsonObject{{"type","integer"}}},
                   {"slotIndex", QJsonObject{{"type","integer"}}},
                   {"paramName", QJsonObject{{"type","string"}}},
                   {"value", QJsonObject{{"type","number"}}}}, {"trackId","slotIndex","paramName","value"}),
        [e](const QJsonObject& a) -> McpToolResult {
            int ti = a.value("trackId").toInt();
            int si = a.value("slotIndex").toInt();
            std::string pn = a.value("paramName").toString().toStdString();
            double v = a.value("value").toDouble();
            e->getProjectCommands().setMidiFxSlotParam(ti, si, pn, v);
            return McpToolResult::text("ok");
        }});
}
```

### Step 2: Wire registration

In `src/mcp/McpTools_Private.h`, add declaration:
```cpp
void registerMidiFxTools(McpServer& s, AudioEngine* e);
```

In `src/mcp/McpTools_Audio.cpp`, find the `registerAudioDomain` function and add `registerMidiFxTools(s, e);` call inside it.

### Step 3: Build

Run: `cmake --build build --config Debug`

Expected: Compiles without errors.

---

## Task 6: Tests

**Files:**
- Modify: `tests/unit/engine/midi_fx_test.cpp`
- Modify: `tests/unit/engine/commands_test.cpp`
- Modify: `tests/unit/common/rpc_surface_test.cpp`
- Modify: `frontend/src/components/MidiFxChain.test.tsx`

### Step 1: Add unit tests for each new effect

In `tests/unit/engine/midi_fx_test.cpp`, add tests for all 8 effects:

```cpp
TEST(Transpose, ShiftsUp)
{
    Transpose t;
    t.semitones = 7;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOff(1, 60), 100);
    t.process(buf, nullptr, 44100.0, 22050);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], 67);
}

TEST(Transpose, ClampsTo127)
{
    Transpose t;
    t.semitones = 24;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 120, (juce::uint8)100), 0);
    t.process(buf, nullptr, 44100.0, 22050);
    auto notes = collectNoteOns(buf);
    EXPECT_EQ(notes[0], 127);
}

TEST(KeyFilter, DropsOutOfKey)
{
    KeyFilter kf;
    kf.root = 0; kf.scaleType = 0; // C major
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0); // C - in
    buf.addEvent(juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), 0); // C# - out
    buf.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 0); // D - in
    kf.process(buf, nullptr, 44100.0, 22050);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 2u);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 62);
}

TEST(MultiNote, AddsOctaveAndFifth)
{
    MultiNote mn;
    mn.intervals = { 0, 7, 12 };
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    mn.process(buf, nullptr, 44100.0, 22050);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 67);
    EXPECT_EQ(notes[2], 72);
}

TEST(VelocityCurve, Compress)
{
    VelocityCurve vc;
    vc.curveType = 1; vc.amount = 1.0;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)127), 0);
    vc.process(buf, nullptr, 44100.0, 22050);
    // With compress at amount=1, velocity 127 -> 64 (pushed to center)
    for (const auto meta : buf)
    {
        auto msg = meta.getMessage();
        if (msg.isNoteOn())
            EXPECT_LE(msg.getVelocity(), 80);
    }
}

TEST(NoteChance, AlwaysPassAtOne)
{
    NoteChance nc;
    nc.probability = 1.0;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    nc.process(buf, nullptr, 44100.0, 22050);
    auto notes = collectNoteOns(buf);
    EXPECT_EQ(notes.size(), 1u);
}

TEST(Humanize, DoesNotCrash)
{
    Humanize h;
    h.timingAmount = 0.5; h.velocityAmount = 0.3; h.pitchAmount = 0.1;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOff(1, 60), 100);
    h.process(buf, nullptr, 44100.0, 22050);
    // Note should still be present (may have shifted slightly)
    EXPECT_GE(collectNoteOns(buf).size(), 1u);
}

TEST(Strum, DoesNotCrash)
{
    Strum st;
    st.strumTime = 0.02; st.direction = 0;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 0);
    st.process(buf, &makePos(0.0, 120.0), 44100.0, 22050);
    auto notes = collectNoteOns(buf);
    EXPECT_EQ(notes.size(), 3u);
}
```

### Step 2: Add setMidiFxSlotParam command test

In `tests/unit/engine/commands_test.cpp`, add:

```cpp
TEST(Commands, SetMidiFxSlotParam)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addMidiFxSlot(0, "transpose");
    cmds.setMidiFxSlotParam(0, 0, "semitones", 7.0);
    auto slot = engine.getProjectModel().getTrackListTree()
        .getChild(0).getChildWithName(IDs::MIDI_FX_CHAIN).getChild(0);
    EXPECT_EQ(static_cast<int>(slot.getProperty(IDs::semitones)), 7);
}
```

### Step 3: Add ReadModel params test

In `tests/unit/common/rpc_surface_test.cpp`, add:

```cpp
TEST(FxSurface, MidiFxSlotParams)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    cmds.addMidiFxSlot(0, "transpose");
    auto slots = engine.getReadModel().getMidiFxSlots(0);
    ASSERT_EQ(slots.size(), 1u);
    EXPECT_DOUBLE_EQ(slots[0].params["semitones"], 0.0);
    cmds.setMidiFxSlotParam(0, 0, "semitones", 5.0);
    slots = engine.getReadModel().getMidiFxSlots(0);
    EXPECT_DOUBLE_EQ(slots[0].params["semitones"], 5.0);
}
```

### Step 4: Update frontend Vitest

In `frontend/src/components/MidiFxChain.test.tsx`, update the `TWO_SLOTS` test data to include `params`:

```typescript
const TWO_SLOTS: MidiFxSlotSnapshot[] = [
  { slotIndex: 0, fxType: "arpeggiator", bypassed: false, params: { arpRate: 0.25, arpPattern: 0, arpOctaves: 1, arpGate: 0.5 } },
  { slotIndex: 1, fxType: "velocity", bypassed: true, params: { velFactor: 1.0 } },
];
```

Add a test for the new dropdown options:

```typescript
it("dropdown includes all 13 MIDI FX types", async () => {
    useUiStore.setState({ selectedTrackIndex: 0 });
    mockedCall.mockResolvedValue(EMPTY);
    render(<MidiFxChain />);
    await flushRead();
    const options = screen.getByRole("combobox").querySelectorAll("option");
    const typeOptions = Array.from(options).filter(o => o.value !== "").map(o => o.value);
    expect(typeOptions).toEqual([
      "arpeggiator", "velocity", "chord", "scale", "notelength",
      "transpose", "keyfilter", "multinote", "velocitycurve",
      "notechance", "mididelay", "humanize", "strum"
    ]);
});
```

### Step 5: Run all tests

Run:
```
cmake --build build --config Debug
build/Debug/hdaw_tests.exe
cd frontend && npm test
```

Expected: All tests pass.

---

## Task 7: Final verification

### Step 1: Build everything

Run: `cmake --build build --config Debug`

### Step 2: Run gtest suite

Run: `build/Debug/hdaw_tests.exe`

### Step 3: Run frontend tests

Run: `cd frontend && npm test`

### Step 4: Run frontend build

Run: `cd frontend && npm run build`

### Step 5: Verify MCP tools appear

Run the MCP test or start the engine and verify the 4 new tools are registered.
