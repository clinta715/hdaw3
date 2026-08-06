#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <algorithm>
#include <cmath>
#include "TransportManager.h"
#include "../model/ProjectModel.h"
#include "OperatorLogic.h"

namespace HDAW {

struct NoteData {
    int noteNumber;
    float velocity;
    double startBeat;
    double durationBeats;
    float chance = 1.0f;
    int repeatCount = 0;
    float repeatRate = 0.25f;
    float repeatCurve = 0.0f;
    int occurrence = 0;
    int recurrence = 0;
    float noteGain = 1.0f;
    float notePan = 0.0f;
    float notePitch = 0.0f;
    float noteTimbre = 0.5f;
    float notePressure = 0.0f;
};

struct CcData {
    int controllerNumber;
    double beat;
    int value;
};

class MidiClipProcessor : public juce::AudioProcessor
{
public:
    static constexpr int MAX_NOTES = 512;
    static constexpr int MAX_CC = 512;

    MidiClipProcessor(HDAW::TransportManager& tm)
        : AudioProcessor(BusesProperties()
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          transportManager(tm)
    {
    }

    ~MidiClipProcessor() override = default;

    void setClipTree(juce::ValueTree tree)
    {
        clipTree = tree;
        clipSeed.store(static_cast<uint64_t>(static_cast<int64_t>(tree.getProperty(IDs::seed, 0))), std::memory_order_relaxed);
        std::fill(previousNotePlayed.begin(), previousNotePlayed.end(), false);
        rebuildNoteCache();
        rebuildCcCache();
    }

    juce::ValueTree getClipTree() const { return clipTree; }

    void setStartTime(double t) { startTime.store(t); }
    void setDuration(double d) { duration.store(d); }
    double getStartTime() const { return startTime.load(); }
    double getDuration() const { return duration.load(); }
    void setGain(float g) { gain.store(g); }
    void setMuted(bool m) { muted.store(m); }
    bool isMuted() const { return muted.load(); }
    void setMidiChannel(int ch) { midiChannel.store(juce::jlimit(1, 16, ch)); }
    int  getMidiChannel() const { return midiChannel.load(); }
    void setSeed(uint64_t s) { clipSeed.store(s, std::memory_order_relaxed); }
    uint64_t getSeed() const { return clipSeed.load(std::memory_order_relaxed); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
        std::fill(activeNotes.begin(), activeNotes.end(), false);
        std::fill(noteActive.begin(), noteActive.end(), false);
        std::fill(pitchOwner.begin(), pitchOwner.end(), -1);
        // midiChannel is set externally via setMidiChannel; default is 1.
    }

    void releaseResources() override
    {
        std::fill(activeNotes.begin(), activeNotes.end(), false);
        std::fill(noteActive.begin(), noteActive.end(), false);
        std::fill(pitchOwner.begin(), pitchOwner.end(), -1);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
    {
        const int numSamples = buffer.getNumSamples();

        buffer.clear();

        if (numSamples <= 0)
            return;

        if (muted.load())
        {
            for (int note = 0; note < 128; ++note)
            {
                if (!activeNotes[note]) continue;
                midiMessages.addEvent(juce::MidiMessage::noteOff(midiChannel.load(), note, 0.0f), 0);
            }
            std::fill(activeNotes.begin(), activeNotes.end(), false);
            std::fill(noteActive.begin(), noteActive.end(), false);
            std::fill(pitchOwner.begin(), pitchOwner.end(), -1);
            std::fill(previousNotePlayed.begin(), previousNotePlayed.end(), false);
            return;
        }

        int idx = activeCacheIndex.load(std::memory_order_acquire);
        int count = noteCount.load(std::memory_order_acquire);
        int ccIdx = activeCcCacheIndex.load(std::memory_order_acquire);
        int ccCnt = ccCount.load(std::memory_order_acquire);

        int64_t transportSample = transportManager.getCurrentSample();
        double sr = transportManager.getSampleRate();
        double startSec = startTime.load();
        double durSec = duration.load();

        double currentTimeSec = static_cast<double>(transportSample) / sr;
        double clipLocalSec = currentTimeSec - startSec;

        const int channel = midiChannel.load();

        if (clipLocalSec < 0.0 || clipLocalSec >= durSec)
        {
            for (int note = 0; note < 128; ++note)
            {
                if (!activeNotes[note]) continue;
                midiMessages.addEvent(juce::MidiMessage::noteOff(channel, note, 0.0f),
                                       juce::jmin(numSamples - 1, 0));
            }
            std::fill(activeNotes.begin(), activeNotes.end(), false);
            std::fill(noteActive.begin(), noteActive.end(), false);
            std::fill(pitchOwner.begin(), pitchOwner.end(), -1);
            return;
        }

        double currentBeat = transportManager.secondsToPpq(currentTimeSec)
                           - transportManager.secondsToPpq(startSec);

        double clipDurationBeats = transportManager.secondsToPpq(durSec);
        int loopCount = (clipDurationBeats > 0.0) ? static_cast<int>(currentBeat / clipDurationBeats) : 0;
        uint64_t seed = clipSeed.load(std::memory_order_relaxed);

        // Snapshot recurrence decisions before any writes to previousNotePlayed,
        // so same-pitch notes don't contaminate each other within this block.
        std::array<bool, MAX_NOTES> recurrenceDecision{};
        for (int j = 0; j < count; ++j)
        {
            const NoteData& nd = noteCaches[idx][j];
            if (nd.recurrence != 0)
                recurrenceDecision[j] = recurrenceCheck(nd.recurrence, previousNotePlayed[nd.noteNumber]);
        }

for (int i = 0; i < count; ++i)
        {
            const NoteData& note = noteCaches[idx][i];
            double noteEnd = note.startBeat + note.durationBeats;

            int adjustedNoteNumber = note.noteNumber + static_cast<int>(note.notePitch);
            adjustedNoteNumber = juce::jlimit(0, 127, adjustedNoteNumber);

            if (currentBeat >= note.startBeat && currentBeat < noteEnd)
            {
                bool shouldPlay = chanceCheck(note.chance, seed, i, loopCount);
                if (note.occurrence != 0)
                    shouldPlay = shouldPlay && occurrenceCheck(note.occurrence, loopCount, 8);
                if (note.recurrence != 0)
                    shouldPlay = shouldPlay && recurrenceDecision[i];

                if (!shouldPlay)
                {
                    if (noteActive[i])
                    {
                        midiMessages.addEvent(juce::MidiMessage::noteOff(channel, adjustedNoteNumber, 0.0f), 0);
                        noteActive[i] = false;
                        if (pitchOwner[adjustedNoteNumber] == i)
                        {
                            activeNotes[adjustedNoteNumber] = false;
                            pitchOwner[adjustedNoteNumber] = -1;
                        }
                    }
                    previousNotePlayed[note.noteNumber] = false;
                    continue;
                }

                // Repeats: if repeatCount > 0, subdivide the note's duration
                if (note.repeatCount > 0 && note.repeatRate > 0.0f)
                {
                    int rCount = note.repeatCount;
                    float rRate = note.repeatRate;
                    double totalRepeatSpan = rCount * rRate;
                    double repeatStartBeat = note.startBeat;
                    if (totalRepeatSpan > note.durationBeats)
                        totalRepeatSpan = note.durationBeats;
                    if (note.repeatCurve != 0.0f)
                    {
                        double curve = static_cast<double>(note.repeatCurve);
                        double t = (currentBeat - note.startBeat) / totalRepeatSpan;
                        if (t < 0.0) t = 0.0;
                        if (t > 1.0) t = 1.0;
                        double curvedT = t;
                        if (curve < 0.0)
                            curvedT = 1.0 - std::pow(1.0 - t, 1.0 - curve);
                        else if (curve > 0.0)
                            curvedT = std::pow(t, 1.0 + curve);
                        repeatStartBeat = note.startBeat + curvedT * totalRepeatSpan;
                    }
                    bool inRepeat = false;
                    for (int r = 0; r < rCount; ++r)
                    {
                        double rs = note.startBeat + r * rRate;
                        double re = rs + rRate * 0.5;
                        if (currentBeat >= rs && currentBeat < re)
                        {
                            inRepeat = true;
                            break;
                        }
                    }
                    if (inRepeat)
                    {
                        if (!noteActive[i])
                        {
                            float adjustedVelocity = note.velocity * gain.load() * note.noteGain;
                            adjustedVelocity = (std::max)(0.0f, (std::min)(1.0f, adjustedVelocity));
                            uint8_t velByte = static_cast<uint8_t>(adjustedVelocity * 127.0f);
                            midiMessages.addEvent(juce::MidiMessage::noteOn(channel, adjustedNoteNumber, velByte), 0);
                            noteActive[i] = true;
                            activeNotes[adjustedNoteNumber] = true;
                            pitchOwner[adjustedNoteNumber] = i;

                            if (note.notePan != 0.0f) {
                                int panCC = 64 + static_cast<int>(note.notePan * 64.0f);
                                panCC = juce::jlimit(0, 127, panCC);
                                midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 10, panCC), 0);
                            }
                            if (note.noteTimbre != 0.5f) {
                                int timbreCC = static_cast<int>(note.noteTimbre * 127.0f);
                                timbreCC = juce::jlimit(0, 127, timbreCC);
                                midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 74, timbreCC), 0);
                            }
                            if (note.notePressure > 0.0f) {
                                int pressureVal = static_cast<int>(note.notePressure * 127.0f);
                                pressureVal = juce::jlimit(0, 127, pressureVal);
                                midiMessages.addEvent(juce::MidiMessage::channelPressureChange(channel, pressureVal), 0);
                            }
                        }
                    }
                    else
                    {
                        if (noteActive[i])
                        {
                            midiMessages.addEvent(juce::MidiMessage::noteOff(channel, adjustedNoteNumber, 0.0f), 0);
                            noteActive[i] = false;
                            if (pitchOwner[adjustedNoteNumber] == i)
                            {
                                activeNotes[adjustedNoteNumber] = false;
                                pitchOwner[adjustedNoteNumber] = -1;
                            }
                        }
                    }
                    previousNotePlayed[note.noteNumber] = true;
                    continue;
                }

                if (!noteActive[i])
                {
                    float adjustedVelocity = note.velocity * gain.load() * note.noteGain;
                    adjustedVelocity = (std::max)(0.0f, (std::min)(1.0f, adjustedVelocity));
                    uint8_t velByte = static_cast<uint8_t>(adjustedVelocity * 127.0f);
                    midiMessages.addEvent(juce::MidiMessage::noteOn(channel, adjustedNoteNumber, velByte), 0);
                    noteActive[i] = true;
                    activeNotes[adjustedNoteNumber] = true;
                    pitchOwner[adjustedNoteNumber] = i;

                    if (note.notePan != 0.0f) {
                        int panCC = 64 + static_cast<int>(note.notePan * 64.0f);
                        panCC = juce::jlimit(0, 127, panCC);
                        midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 10, panCC), 0);
                    }
                    if (note.noteTimbre != 0.5f) {
                        int timbreCC = static_cast<int>(note.noteTimbre * 127.0f);
                        timbreCC = juce::jlimit(0, 127, timbreCC);
                        midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 74, timbreCC), 0);
                    }
                    if (note.notePressure > 0.0f) {
                        int pressureVal = static_cast<int>(note.notePressure * 127.0f);
                        pressureVal = juce::jlimit(0, 127, pressureVal);
                        midiMessages.addEvent(juce::MidiMessage::channelPressureChange(channel, pressureVal), 0);
                    }
                }
                previousNotePlayed[note.noteNumber] = true;
            }
            else if (noteActive[i])
            {
                midiMessages.addEvent(juce::MidiMessage::noteOff(channel, adjustedNoteNumber, 0.0f), 0);
                noteActive[i] = false;
                if (pitchOwner[adjustedNoteNumber] == i)
                {
                    activeNotes[adjustedNoteNumber] = false;
                    pitchOwner[adjustedNoteNumber] = -1;
                }
            }
        }

        if (ccCnt > 0)
        {
            double blockEndBeat = transportManager.secondsToPpq(currentTimeSec + static_cast<double>(numSamples) / sr)
                                - transportManager.secondsToPpq(startSec);
            double beatSpan = blockEndBeat - currentBeat;
            for (int i = 0; i < ccCnt; ++i)
            {
                const CcData& cc = ccCaches[ccIdx][i];
                if (cc.beat >= currentBeat && cc.beat < blockEndBeat)
                {
                    int sampleOffset = 0;
                    if (beatSpan > 1e-9)
                        sampleOffset = static_cast<int>(((cc.beat - currentBeat) / beatSpan) * numSamples);
                    sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
                    int v = juce::jlimit(0, 127, cc.value);
                    midiMessages.addEvent(
                        juce::MidiMessage::controllerEvent(channel, cc.controllerNumber, v),
                        sampleOffset);
                }
            }
        }

        float ccVal = gain.load();
        uint8_t ccByte = static_cast<uint8_t>((std::max)(0.0f, (std::min)(1.0f, ccVal)) * 127.0f);
        if (ccByte != lastCcByte)
        {
            midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 7, ccByte), 0);
            lastCcByte = ccByte;
        }
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "MidiClip"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    void rebuildNoteCache()
    {
        int inactiveIdx = 1 - activeCacheIndex.load(std::memory_order_relaxed);
        auto& inactive = noteCaches[inactiveIdx];

        auto nl = clipTree.getChildWithName(IDs::MIDI_NOTE_LIST);
        if (!nl.isValid())
        {
            noteCount.store(0, std::memory_order_release);
            activeCacheIndex.store(inactiveIdx, std::memory_order_release);
            return;
        }

        int count = (std::min)(nl.getNumChildren(), MAX_NOTES);
        for (int i = 0; i < count; ++i)
        {
            auto n = nl.getChild(i);
            inactive[i].noteNumber = n.getProperty(IDs::noteNumber);
            inactive[i].velocity = n.getProperty(IDs::velocity);
            inactive[i].startBeat = n.getProperty(IDs::startBeat);
            inactive[i].durationBeats = n.getProperty(IDs::durationBeats);
            inactive[i].chance = n.getProperty(IDs::chance, 1.0f);
            inactive[i].repeatCount = n.getProperty(IDs::repeatCount, 0);
            inactive[i].repeatRate = n.getProperty(IDs::repeatRate, 0.25f);
            inactive[i].repeatCurve = n.getProperty(IDs::repeatCurve, 0.0f);
            inactive[i].occurrence = n.getProperty(IDs::occurrence, 0);
            inactive[i].recurrence = n.getProperty(IDs::recurrence, 0);
            inactive[i].noteGain = n.getProperty(IDs::noteGain, 1.0f);
            inactive[i].notePan = n.getProperty(IDs::notePan, 0.0f);
            inactive[i].notePitch = n.getProperty(IDs::notePitch, 0.0f);
            inactive[i].noteTimbre = n.getProperty(IDs::noteTimbre, 0.5f);
            inactive[i].notePressure = n.getProperty(IDs::notePressure, 0.0f);
        }

        noteCount.store(count, std::memory_order_release);
        activeCacheIndex.store(inactiveIdx, std::memory_order_release);
    }

    void rebuildCcCache()
    {
        int inactiveIdx = 1 - activeCcCacheIndex.load(std::memory_order_relaxed);
        auto& inactive = ccCaches[inactiveIdx];

        auto cl = clipTree.getChildWithName(IDs::CC_LIST);
        if (!cl.isValid())
        {
            ccCount.store(0, std::memory_order_release);
            activeCcCacheIndex.store(inactiveIdx, std::memory_order_release);
            return;
        }

        int count = (std::min)(cl.getNumChildren(), MAX_CC);
        for (int i = 0; i < count; ++i)
        {
            auto p = cl.getChild(i);
            inactive[i].controllerNumber = p.getProperty(IDs::controllerNumber);
            inactive[i].beat = p.getProperty(IDs::beat);
            inactive[i].value = p.getProperty(IDs::value);
        }

        std::sort(inactive.begin(), inactive.begin() + count,
                  [](const CcData& a, const CcData& b) { return a.beat < b.beat; });

        ccCount.store(count, std::memory_order_release);
        activeCcCacheIndex.store(inactiveIdx, std::memory_order_release);
    }

    HDAW::TransportManager& transportManager;
    juce::ValueTree clipTree;

    std::atomic<double> startTime{ 0.0 };
    std::atomic<double> duration{ 1.0 };
    std::atomic<float> gain{ 1.0f };
    std::atomic<bool> muted{ false };

    std::atomic<int> midiChannel{ 1 }; // 1-16 = specific MIDI channel
    uint8_t lastCcByte = 255;
    std::array<bool, 128> activeNotes{};
    std::array<bool, MAX_NOTES> noteActive{};
    std::array<int, 128> pitchOwner{ -1 };
    std::array<bool, 128> previousNotePlayed{};
    std::atomic<uint64_t> clipSeed{0};

    std::array<std::array<NoteData, MAX_NOTES>, 2> noteCaches{};
    std::atomic<int> activeCacheIndex{0};
    std::atomic<int> noteCount{0};

    std::array<std::array<CcData, MAX_CC>, 2> ccCaches{};
    std::atomic<int> activeCcCacheIndex{0};
    std::atomic<int> ccCount{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiClipProcessor)
};

} // namespace HDAW
