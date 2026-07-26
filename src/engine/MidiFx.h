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
        const int kEnd = static_cast<int>(std::floor(blockEnd / rate + 1e-9));
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
                pendingOns[msg.getNoteNumber()] = beat;
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
    std::map<int, double> pendingOns;
    std::vector<ScheduledOff> scheduledOffs;
};

// A slot wraps a MidiEffect with a bypass flag, mirroring TrackFXSlot.
class MidiFxSlot
{
public:
    MidiFxSlot(std::unique_ptr<MidiEffect> effect, juce::String type)
        : effect_(std::move(effect)), slotType_(std::move(type)) {}

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

private:
    std::unique_ptr<MidiEffect> effect_;
    juce::String slotType_;
    std::atomic<bool> bypassed_{ false };
};

} // namespace HDAW
