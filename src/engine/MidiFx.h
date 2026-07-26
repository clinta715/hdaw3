#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <set>
#include <memory>
#include <atomic>
#include <algorithm>

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
