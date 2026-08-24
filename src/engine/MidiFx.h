#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <random>
#include <span>

namespace HDAW {

// A stateful MIDI effect transforms a MidiBuffer in place, given the current
// playhead position so tempo/transport-synced effects (arpeggiator) can derive
// their clock. Mirrors the role TrackFXSlot plays for audio, but MIDI-only.
class MidiEffect
{
public:
    virtual ~MidiEffect() = default;
    virtual void process(juce::MidiBuffer& buffer,
                         const juce::AudioPlayHead::PositionInfo* position,
                         double sampleRate, int numSamples) = 0;
    virtual void reset() {}
};

struct MidiFxParamDef {
    int index;
    const char* name;
    float defaultValue;
    float minValue;
    float maxValue;
};

std::span<const MidiFxParamDef> getMidiFxParamDefs(const juce::String& type);
int getMidiFxParamCount(const juce::String& type);

// Arpeggiator: collects incoming (held) notes and replays them as a
// tempo-synced pattern. rate = step length in beats; pattern 0=up, 1=down,
// 2=up-down; octaves stacks the held notes; gate = note length as a fraction
// of rate.
class Arpeggiator : public MidiEffect
{
public:
    double rate = 0.25;   // beats per step (1/16 note)
    int pattern = 0;      // 0 up, 1 down, 2 up-down
    int octaves = 1;
    double gate = 0.5;    // note length as a fraction of rate
    int velocity = 100;

    void reset() override
    {
        heldNotes.clear();
        currentNote = -1;
        currentNoteOffBeat = 0.0;
        channel = 1;
    }

    void process(juce::MidiBuffer& buffer,
                 const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples) override
    {
        // Note on/off update the held set; everything else passes through.
        juce::MidiBuffer thru;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                heldNotes.insert(msg.getNoteNumber());
                channel = msg.getChannel();
                if (msg.getVelocity() > 0) velocity = msg.getVelocity();
            }
            else if (msg.isNoteOff())
            {
                heldNotes.erase(msg.getNoteNumber());
            }
            else
            {
                thru.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = thru;

        if (heldNotes.empty())
        {
            if (currentNote >= 0)
            {
                buffer.addEvent(juce::MidiMessage::noteOff(channel, currentNote), 0);
                currentNote = -1;
            }
            return;
        }

        if (position == nullptr || !position->getIsPlaying())
            return;

        const double bpm = position->getBpm().orFallback(120.0);
        const double blockStart = position->getPpqPosition().orFallback(0.0);
        const double beatsPerSample = bpm / 60.0 / sampleRate;
        const double blockEnd = blockStart + numSamples * beatsPerSample;

        const auto seq = buildSequence();
        if (seq.empty()) return;

        // Close a sounding note whose release falls before this block.
        if (currentNote >= 0 && blockStart >= currentNoteOffBeat)
        {
            buffer.addEvent(juce::MidiMessage::noteOff(channel, currentNote), 0);
            currentNote = -1;
        }

        const int seqLen = static_cast<int>(seq.size());
        const int kStart = static_cast<int>(std::ceil(blockStart / rate - 1e-9));
        const int kEnd = static_cast<int>(std::ceil(blockEnd / rate - 1e-9));
        for (int k = kStart; k < kEnd; ++k)
        {
            const double stepBeat = k * rate;
            if (stepBeat < blockStart || stepBeat >= blockEnd) continue;
            int sample = static_cast<int>((stepBeat - blockStart) / beatsPerSample);
            sample = juce::jlimit(0, numSamples - 1, sample);

            if (currentNote >= 0 && stepBeat >= currentNoteOffBeat)
            {
                buffer.addEvent(juce::MidiMessage::noteOff(channel, currentNote), sample);
                currentNote = -1;
            }

            int idx = k % seqLen;
            if (idx < 0) idx += seqLen;
            const int note = seq[idx];
            const int vel = juce::jlimit(1, 127, velocity);
            buffer.addEvent(juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(vel)), sample);
            currentNote = note;
            currentNoteOffBeat = stepBeat + rate * gate;
        }
    }

private:
    std::vector<int> buildSequence() const
    {
        std::vector<int> base(heldNotes.begin(), heldNotes.end());
        std::sort(base.begin(), base.end());
        std::vector<int> seq;
        for (int oct = 0; oct < (std::max)(1, octaves); ++oct)
            for (int n : base)
                seq.push_back(n + 12 * oct);
        if (seq.empty()) return seq;
        if (pattern == 1)
        {
            std::reverse(seq.begin(), seq.end());
        }
        else if (pattern == 2 && seq.size() > 2)
        {
            std::vector<int> rev(seq.rbegin(), seq.rend());
            seq.insert(seq.end(), rev.begin() + 1, rev.end() - 1);
        }
        return seq;
    }

    std::set<int> heldNotes;
    int currentNote = -1;
    double currentNoteOffBeat = 0.0;
    int channel = 1;
};

// Scales note-on velocities by a factor (clamped to 1..127).
class VelocityScaler : public MidiEffect
{
public:
    double factor = 1.0;

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo*,
                 double, int) override
    {
        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                int v = static_cast<int>(std::lround(msg.getVelocity() * factor));
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

// Adds chord tones above each played note. chordType: 0 major, 1 minor,
// 2 power, 3 octave.
class Chorder : public MidiEffect
{
public:
    int chordType = 0;

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo*,
                 double, int) override
    {
        const auto intervals = intervalsFor(chordType);
        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                for (int iv : intervals)
                {
                    int note = msg.getNoteNumber() + iv;
                    if (note >= 0 && note < 128)
                        out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), note,
                                     static_cast<juce::uint8>(msg.getVelocity())), meta.samplePosition);
                }
            }
            else if (msg.isNoteOff())
            {
                for (int iv : intervals)
                {
                    int note = msg.getNoteNumber() + iv;
                    if (note >= 0 && note < 128)
                        out.addEvent(juce::MidiMessage::noteOff(msg.getChannel(), note), meta.samplePosition);
                }
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
            case 1: return { 0, 3, 7 };   // minor
            case 2: return { 0, 7 };      // power
            case 3: return { 0, 12 };     // octave
            default: return { 0, 4, 7 };  // major
        }
    }
};

// Snaps note pitches to a scale. scaleType: 0 major, 1 natural minor,
// 2 chromatic.
class ScaleQuantize : public MidiEffect
{
public:
    int root = 0;
    int scaleType = 0;

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
                out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), snap(msg.getNoteNumber(), intervals),
                             static_cast<juce::uint8>(msg.getVelocity())), meta.samplePosition);
            }
            else if (msg.isNoteOff())
            {
                out.addEvent(juce::MidiMessage::noteOff(msg.getChannel(), snap(msg.getNoteNumber(), intervals)),
                             meta.samplePosition);
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
            case 1: return { 0, 2, 3, 5, 7, 8, 10 };                 // natural minor
            case 2: return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };  // chromatic
            default: return { 0, 2, 4, 5, 7, 9, 11 };                // major
        }
    }

    int snap(int pitch, const std::vector<int>& intervals) const
    {
        int best = pitch;
        int bestDist = 999;
        const int octave = pitch / 12;
        for (int oct = octave - 1; oct <= octave + 1; ++oct)
        {
            for (int iv : intervals)
            {
                int cand = root + oct * 12 + iv;
                int d = std::abs(cand - pitch);
                if (d < bestDist) { bestDist = d; best = cand; }
            }
        }
        return juce::jlimit(0, 127, best);
    }
};

// Scales note durations by a factor, using the transport clock to reschedule
// note-offs. factor < 1 shortens (staccato), > 1 lengthens (legato).
class NoteLengthScaler : public MidiEffect
{
public:
    double factor = 1.0;

    void reset() override
    {
        pendingOns.clear();
        scheduledOffs.clear();
    }

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples) override
    {
        const double bpm = position != nullptr ? position->getBpm().orFallback(120.0) : 120.0;
        const double blockStart = position != nullptr ? position->getPpqPosition().orFallback(0.0) : 0.0;
        const double beatsPerSample = sampleRate > 0 ? bpm / 60.0 / sampleRate : 0.0;
        const double blockEnd = blockStart + numSamples * beatsPerSample;

        juce::MidiBuffer out;

        for (auto it = scheduledOffs.begin(); it != scheduledOffs.end(); )
        {
            if (it->offBeat < blockEnd)
            {
                int sample = beatsPerSample > 0
                    ? static_cast<int>((it->offBeat - blockStart) / beatsPerSample) : 0;
                out.addEvent(juce::MidiMessage::noteOff(it->channel, it->note),
                             juce::jlimit(0, numSamples - 1, sample));
                it = scheduledOffs.erase(it);
            }
            else
            {
                ++it;
            }
        }

        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            const double beat = blockStart + meta.samplePosition * beatsPerSample;
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                out.addEvent(msg, meta.samplePosition);
                pendingOns.insert({msg.getNoteNumber(), beat});
            }
            else if (msg.isNoteOff())
            {
                auto it = pendingOns.find(msg.getNoteNumber());
                if (it != pendingOns.end())
                {
                    const double onBeat = it->second;
                    const double offBeat = onBeat + (beat - onBeat) * factor;
                    pendingOns.erase(it);
                    if (offBeat < blockEnd)
                    {
                        int sample = beatsPerSample > 0
                            ? static_cast<int>((offBeat - blockStart) / beatsPerSample) : 0;
                        out.addEvent(juce::MidiMessage::noteOff(msg.getChannel(), msg.getNoteNumber()),
                                     juce::jlimit(0, numSamples - 1, sample));
                    }
                    else
                    {
                        scheduledOffs.push_back({ msg.getNoteNumber(), msg.getChannel(), offBeat });
                    }
                }
                else
                {
                    out.addEvent(msg, meta.samplePosition);
                }
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }

private:
    struct ScheduledOff { int note; int channel; double offBeat; };
    std::multimap<int, double> pendingOns;
    std::vector<ScheduledOff> scheduledOffs;
};

// Transposes note pitches by a fixed number of semitones (clamped to 0..127).
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
            if (msg.isNoteOn() || msg.isNoteOff())
            {
                int note = juce::jlimit(0, 127, msg.getNoteNumber() + semitones);
                if (msg.isNoteOn())
                    out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), note,
                                 static_cast<juce::uint8>(msg.getVelocity())), meta.samplePosition);
                else
                    out.addEvent(juce::MidiMessage::noteOff(msg.getChannel(), note), meta.samplePosition);
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }
};

// Filters notes by key/scale — drops notes whose pitch class is not in the
// selected scale. root: 0..11 (C..B). scaleType: 0=major, 1=minor, 2=chromatic.
class KeyFilter : public MidiEffect
{
public:
    int root = 0;
    int scaleType = 0;

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo*,
                 double, int) override
    {
        const auto intervals = intervalsFor(scaleType);
        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() || msg.isNoteOff())
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
            case 1: return { 0, 2, 3, 5, 7, 8, 10 };  // natural minor
            case 2: return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }; // chromatic
            default: return { 0, 2, 4, 5, 7, 9, 11 }; // major
        }
    }
};

// Generates additional notes at fixed intervals above each input note.
// intervals default {0, 7, 12} = root + fifth + octave.
class MultiNote : public MidiEffect
{
public:
    std::vector<int> intervals = { 0, 7, 12 };

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
                    int note = juce::jlimit(0, 127, msg.getNoteNumber() + iv);
                    out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), note,
                                 static_cast<juce::uint8>(msg.getVelocity())), meta.samplePosition);
                }
            }
            else if (msg.isNoteOff())
            {
                for (int iv : intervals)
                {
                    int note = juce::jlimit(0, 127, msg.getNoteNumber() + iv);
                    out.addEvent(juce::MidiMessage::noteOff(msg.getChannel(), note), meta.samplePosition);
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

// Reshapes note velocity through a transfer curve.
// curveType: 0=linear, 1=compress, 2=expand, 3=random, 4=fixed.
// curveAmount: 0..1 strength.
class VelocityCurve : public MidiEffect
{
public:
    int curveType = 0;
    double curveAmount = 0.5;

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
                double result = norm;
                switch (curveType)
                {
                    case 0: result = norm * curveAmount * 2.0; break;
                    case 1: result = norm + (0.5 - norm) * curveAmount; break;
                    case 2: result = norm + (norm - 0.5) * curveAmount; break;
                    case 3: result = norm + (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * curveAmount; break;
                    case 4: result = curveAmount; break;
                }
                int v = juce::jlimit(1, 127, static_cast<int>(std::lround(result * 127.0)));
                out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), msg.getNoteNumber(),
                             static_cast<juce::uint8>(v)), meta.samplePosition);
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }
};

// Randomly drops notes based on a probability.
// noteChance: 0..1 probability of passing through. Note-offs always pass.
class NoteChance : public MidiEffect
{
public:
    double noteChance = 1.0;

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo*,
                 double, int) override
    {
        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                if (static_cast<double>(std::rand()) / RAND_MAX < noteChance)
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

// Delays notes by a number of beats with optional feedback and mix.
// Needs transport (PPQ + BPM) to schedule delayed events.
class MidiDelay : public MidiEffect
{
public:
    double delayBeats = 0.25;
    double feedback = 0.0;
    double mix = 0.5;

    void reset() override { pending.clear(); }

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples) override
    {
        if (position == nullptr) return;
        const double bpm = position->getBpm().orFallback(120.0);
        const double blockStart = position->getPpqPosition().orFallback(0.0);
        const double beatsPerSample = sampleRate > 0 ? bpm / 60.0 / sampleRate : 0.0;
        const double blockEnd = blockStart + numSamples * beatsPerSample;

        // Emit pending delayed notes whose trigger beat falls in this block
        juce::MidiBuffer out;
        for (size_t i = 0; i < pending.size(); )
        {
            auto& note = pending[i];
            if (note.triggerBeat < blockEnd)
            {
                int sample = beatsPerSample > 0
                    ? static_cast<int>((note.triggerBeat - blockStart) / beatsPerSample) : 0;
                sample = juce::jlimit(0, numSamples - 1, sample);
                if (note.isNoteOn)
                    out.addEvent(juce::MidiMessage::noteOn(note.channel, note.note,
                                 static_cast<juce::uint8>(juce::jlimit(1, 127, note.velocity))), sample);
                else
                    out.addEvent(juce::MidiMessage::noteOff(note.channel, note.note), sample);

                // Feedback: re-schedule at decreasing velocity
                if (feedback > 0.0 && note.isNoteOn && note.velocity > 1)
                {
                    int nextVel = juce::jlimit(1, 127, static_cast<int>(note.velocity * feedback));
                    pending.push_back({ note.note, note.channel, nextVel, true,
                                        note.triggerBeat + delayBeats });
                }
                pending.erase(pending.begin() + i);
            }
            else
            {
                ++i;
            }
        }

        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                const double beat = blockStart + meta.samplePosition * beatsPerSample;
                // Pass through if mix > 0
                if (mix > 0.0)
                    out.addEvent(msg, meta.samplePosition);
                // Schedule delayed copy
                pending.push_back({ msg.getNoteNumber(), msg.getChannel(),
                                    msg.getVelocity(), true, beat + delayBeats });
            }
            else if (msg.isNoteOff())
            {
                const double beat = blockStart + meta.samplePosition * beatsPerSample;
                if (mix > 0.0)
                    out.addEvent(msg, meta.samplePosition);
                pending.push_back({ msg.getNoteNumber(), msg.getChannel(), 0, false,
                                    beat + delayBeats });
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }

private:
    struct PendingNote { int note; int channel; int velocity; bool isNoteOn; double triggerBeat; };
    std::vector<PendingNote> pending;
};

// Randomizes timing, velocity, and pitch of note-on events.
// humanizeTiming: 0..1 amount (offset based on 1/32 note).
// humanizeVelocity: 0..1 amount (±127 range).
// humanizePitch: 0..1 amount (±1 semitone).
class Humanize : public MidiEffect
{
public:
    double humanizeTiming = 0.0;
    double humanizeVelocity = 0.0;
    double humanizePitch = 0.0;

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples) override
    {
        const double bpm = position != nullptr ? position->getBpm().orFallback(120.0) : 120.0;
        const double beatsPerSample = sampleRate > 0 ? bpm / 60.0 / sampleRate : 0.0;
        // 1/32 note in beats = 0.125
        const double timingBeats = 0.125 * humanizeTiming;

        juce::MidiBuffer out;
        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
            {
                // Timing offset
                int sampleOffset = 0;
                if (humanizeTiming > 0.0 && beatsPerSample > 0)
                {
                    double randFactor = (static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 2.0;
                    sampleOffset = static_cast<int>(randFactor * timingBeats / beatsPerSample);
                }
                int newSample = juce::jlimit(0, numSamples - 1, meta.samplePosition + sampleOffset);

                // Velocity offset
                int vel = msg.getVelocity();
                if (humanizeVelocity > 0.0)
                {
                    int velOffset = static_cast<int>((static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 2.0 * humanizeVelocity * 127.0);
                    vel = juce::jlimit(1, 127, vel + velOffset);
                }

                // Pitch offset
                int note = msg.getNoteNumber();
                if (humanizePitch > 0.0)
                {
                    int pitchOffset = static_cast<int>((static_cast<double>(std::rand()) / RAND_MAX - 0.5) * 2.0 * humanizePitch);
                    note = juce::jlimit(0, 127, note + pitchOffset);
                }

                out.addEvent(juce::MidiMessage::noteOn(msg.getChannel(), note,
                             static_cast<juce::uint8>(vel)), newSample);
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }
        buffer = out;
    }
};

// Spreads simultaneous notes across time to create a strum effect.
// Collects notes at sample 0, sorts by pitch, spreads across strumTime beats.
// direction: 0=up (low to high), 1=down (high to low), 2=random.
class Strum : public MidiEffect
{
public:
    double strumTime = 0.02;
    int strumDirection = 0;

    void process(juce::MidiBuffer& buffer, const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples) override
    {
        const double bpm = position != nullptr ? position->getBpm().orFallback(120.0) : 120.0;
        const double beatsPerSample = sampleRate > 0 ? bpm / 60.0 / sampleRate : 0.0;

        // Collect notes at sample 0 and everything else
        struct StrumNote { int note; int channel; int velocity; bool isOn; };
        std::vector<StrumNote> simultaneous;
        juce::MidiBuffer out;

        for (const auto meta : buffer)
        {
            const auto msg = meta.getMessage();
            if (meta.samplePosition == 0 && (msg.isNoteOn() || msg.isNoteOff()))
            {
                simultaneous.push_back({ msg.getNoteNumber(), msg.getChannel(),
                                         msg.getVelocity(), msg.isNoteOn() });
            }
            else
            {
                out.addEvent(msg, meta.samplePosition);
            }
        }

        if (simultaneous.size() <= 1)
        {
            // Single note or no notes — pass through unchanged
            for (auto& sn : simultaneous)
            {
                if (sn.isOn)
                    out.addEvent(juce::MidiMessage::noteOn(sn.channel, sn.note,
                                 static_cast<juce::uint8>(sn.velocity)), 0);
                else
                    out.addEvent(juce::MidiMessage::noteOff(sn.channel, sn.note), 0);
            }
            buffer = out;
            return;
        }

        // Sort by pitch
        if (strumDirection == 0)
            std::sort(simultaneous.begin(), simultaneous.end(),
                      [](const StrumNote& a, const StrumNote& b) { return a.note < b.note; });
        else if (strumDirection == 1)
            std::sort(simultaneous.begin(), simultaneous.end(),
                      [](const StrumNote& a, const StrumNote& b) { return a.note > b.note; });
        else // random
            std::shuffle(simultaneous.begin(), simultaneous.end(), std::mt19937{ std::random_device{}() });

        const int count = static_cast<int>(simultaneous.size());
        for (int i = 0; i < count; ++i)
        {
            double beatOffset = (count > 1) ? strumTime * i / (count - 1) : 0.0;
            int sample = beatsPerSample > 0
                ? static_cast<int>(beatOffset / beatsPerSample) : 0;
            sample = juce::jlimit(0, numSamples - 1, sample);

            auto& sn = simultaneous[i];
            if (sn.isOn)
                out.addEvent(juce::MidiMessage::noteOn(sn.channel, sn.note,
                             static_cast<juce::uint8>(sn.velocity)), sample);
            else
                out.addEvent(juce::MidiMessage::noteOff(sn.channel, sn.note), sample);
        }
        buffer = out;
    }
};

// A slot wraps a MidiEffect with a bypass flag and parameter cache,
// mirroring TrackFXSlot's automation interface.
class MidiFxSlot
{
public:
    MidiFxSlot(std::unique_ptr<MidiEffect> effect, juce::String type);
    ~MidiFxSlot() = default;

    void process(juce::MidiBuffer& buffer,
                 const juce::AudioPlayHead::PositionInfo* position,
                 double sampleRate, int numSamples)
    {
        if (bypassed_.load(std::memory_order_relaxed) || !effect_) return;
        effect_->process(buffer, position, sampleRate, numSamples);
    }

    void setBypassed(bool b) { bypassed_.store(b, std::memory_order_relaxed); }
    bool isBypassed() const { return bypassed_.load(std::memory_order_relaxed); }
    const juce::String& getType() const { return slotType_; }
    MidiEffect* getEffect() const { return effect_.get(); }
    void reset() { if (effect_) effect_->reset(); }

    struct ParamInfo {
        juce::String name;
        int index;
        float defaultValue;
        float minValue;
        float maxValue;
    };

    void setAutomationParam(int paramIndex, float normalizedValue);
    float getAutomationParam(int paramIndex) const;
    void applyAutomation();
    const std::vector<ParamInfo>& getAutomatableParams() const { return cachedParamInfo_; }

    void loadParamsFromTree(const juce::ValueTree& slotTree);

private:
    void initParamCache();
    void applyToEffect(int paramIndex, float denormalizedValue);

    std::unique_ptr<MidiEffect> effect_;
    juce::String slotType_;
    std::atomic<bool> bypassed_{ false };

    int numParams_ = 0;
    std::unique_ptr<std::atomic<float>[]> paramValues_;
    std::unique_ptr<std::atomic<bool>[]> paramDirty_;
    std::vector<ParamInfo> cachedParamInfo_;
};

} // namespace HDAW
