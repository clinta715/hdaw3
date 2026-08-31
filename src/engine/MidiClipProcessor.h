#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include "TransportManager.h"
#include "../model/ProjectModel.h"
#include "../common/DebugLog.h"
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
    // Cache slot ceiling per clip. Note/CC caches are heap vectors sized to
    // the actual list length (no practical limit); the ceiling is a hard
    // safety clamp, and a clip exceeding it is truncated WITH a loud log.
    // The old fixed 512-entry arrays truncated silently — long compositions
    // lost their tails (docs/handoffs/2026-08-27-mcp-cluster-compose-session-bugs.md §1).
    static constexpr int MAX_NOTE_SLOTS = 8192;
    static constexpr int MAX_CC_SLOTS = 8192;

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
    int getNumCachedNotes() const { return noteCount.load(std::memory_order_acquire); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        static const bool audioDiag = juce::SystemStats::getEnvironmentVariable("HDAW_AUDIO_THREAD_DIAG", "") == "1";
        if (audioDiag)
            HDAW_LOG("MidiClipEntry", "prepareToPlay sr=" + juce::String(sampleRate) + " spb=" + juce::String(samplesPerBlock));
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
        static const bool audioDiag = juce::SystemStats::getEnvironmentVariable("HDAW_AUDIO_THREAD_DIAG", "") == "1";
        const int numSamples = buffer.getNumSamples();

        if (audioDiag && procEntryCount < 5)
            HDAW_LOG("MidiClipEntry", "call=" + juce::String(procEntryCount) + " bufS=" + juce::String(numSamples));
        ++procEntryCount;

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

        // Immutable cache snapshots. Retained (shared_ptr) for this block so a
        // concurrent rebuild on the command thread can never free storage the
        // callback is reading — even two rebuilds between callbacks. Audio path
        // stays allocation-free; refcount release is the same discipline JUCE's
        // AudioProcessorGraph already applies to Node::Ptr on the audio thread.
        const std::shared_ptr<const NoteCache> noteSnap = noteSnapshot.load(std::memory_order_acquire);
        const std::shared_ptr<const CcCache> ccSnap = ccSnapshot.load(std::memory_order_acquire);
        const int count = noteSnap ? static_cast<int>(noteSnap->notes.size()) : 0;
        const int ccCnt = ccSnap ? static_cast<int>(ccSnap->points.size()) : 0;

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

        if (audioDiag && diagCount < 5)
        {
            HDAW_LOG("MidiClipDiag", "call=" + juce::String(diagCount)
                + " noteCount=" + juce::String(count)
                + " clipLocalSec=" + juce::String(clipLocalSec, 4)
                + " durSec=" + juce::String(durSec, 4)
                + " currentBeat=" + juce::String(currentBeat, 4)
                + " transportSample=" + juce::String(transportSample));
        }
        ++diagCount;

        // Snapshot recurrence decisions before any writes to previousNotePlayed,
        // so same-pitch notes don't contaminate each other within this block.
        // Pitch tables are 128 wide (MIDI); a tree can carry out-of-range
        // noteNumber values (e.g. written by a generator bug), so every raw
        // index below is clamped — mirrors the adjustedNoteNumber jlimit.
        std::fill_n(recurrenceDecision.begin(), count, false);
        for (int j = 0; j < count; ++j)
        {
            const NoteData& nd = noteSnap->notes[j];
            if (nd.recurrence != 0)
                recurrenceDecision[j] = recurrenceCheck(nd.recurrence,
                    previousNotePlayed[juce::jlimit (0, 127, nd.noteNumber)]);
        }

for (int i = 0; i < count; ++i)
        {
            const NoteData& note = noteSnap->notes[i];
            double noteEnd = note.startBeat + note.durationBeats;

            int adjustedNoteNumber = note.noteNumber + static_cast<int>(note.notePitch);
            adjustedNoteNumber = juce::jlimit(0, 127, adjustedNoteNumber);
            // Raw (pre-offset) pitch clamped for the 128-wide bookkeeping
            // tables below — an out-of-range tree value must never index past
            // the arrays (debug STL asserts; release = UB).
            const int tableNoteNumber = juce::jlimit(0, 127, note.noteNumber);

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
                    previousNotePlayed[tableNoteNumber] = false;
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
                    previousNotePlayed[tableNoteNumber] = true;
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
                previousNotePlayed[tableNoteNumber] = true;
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
                const CcData& cc = ccSnap->points[i];
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
    // Immutable cache snapshot machinery. Rebuilds run only on non-audio
    // threads (setClipTree from RoutingManager / tests). Each rebuild allocates
    // a fresh snapshot and publishes it with an atomic store; the audio thread
    // takes a shared_ptr copy at the top of processBlock. Snapshots are never
    // mutated or reused after publication, so there is no ABA hazard and no
    // use-after-free (a double-buffered array/vector can tolerate only one
    // rebuild between callbacks — the second rewrites/frees the slot a callback
    // is still reading).
    struct NoteCache
    {
        std::vector<NoteData> notes;
    };

    struct CcCache
    {
        std::vector<CcData> points;
    };

    void rebuildNoteCache()
    {
        auto nl = clipTree.getChildWithName(IDs::MIDI_NOTE_LIST);
        int count = 0;
        if (nl.isValid())
        {
            const int numChildren = nl.getNumChildren();
            count = (std::min)(numChildren, MAX_NOTE_SLOTS);
            if (numChildren > MAX_NOTE_SLOTS)
            {
                HDAW_LOG("MidiClip", "clip '" + clipTree.getProperty(IDs::name, juce::String()).toString()
                    + "' has " + juce::String(numChildren) + " notes, engine ceiling is " + juce::String(MAX_NOTE_SLOTS)
                    + " - notes past the ceiling will NOT play; split the clip into smaller clips");
            }
        }

        if (count == 0)
        {
            noteCount.store(0, std::memory_order_release);
            noteSnapshot.store({}, std::memory_order_release);
            return;
        }

        auto fresh = std::make_shared<NoteCache>();
        fresh->notes.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            auto n = nl.getChild(i);
            fresh->notes[i].noteNumber = n.getProperty(IDs::noteNumber);
            fresh->notes[i].velocity = n.getProperty(IDs::velocity);
            fresh->notes[i].startBeat = n.getProperty(IDs::startBeat);
            fresh->notes[i].durationBeats = n.getProperty(IDs::durationBeats);
            fresh->notes[i].chance = n.getProperty(IDs::chance, 1.0f);
            fresh->notes[i].repeatCount = n.getProperty(IDs::repeatCount, 0);
            fresh->notes[i].repeatRate = n.getProperty(IDs::repeatRate, 0.25f);
            fresh->notes[i].repeatCurve = n.getProperty(IDs::repeatCurve, 0.0f);
            fresh->notes[i].occurrence = n.getProperty(IDs::occurrence, 0);
            fresh->notes[i].recurrence = n.getProperty(IDs::recurrence, 0);
            fresh->notes[i].noteGain = n.getProperty(IDs::noteGain, 1.0f);
            fresh->notes[i].notePan = n.getProperty(IDs::notePan, 0.0f);
            fresh->notes[i].notePitch = n.getProperty(IDs::notePitch, 0.0f);
            fresh->notes[i].noteTimbre = n.getProperty(IDs::noteTimbre, 0.5f);
            fresh->notes[i].notePressure = n.getProperty(IDs::notePressure, 0.0f);
        }

        noteCount.store(count, std::memory_order_release);
        noteSnapshot.store(std::move(fresh), std::memory_order_release);
    }

    void rebuildCcCache()
    {
        auto cl = clipTree.getChildWithName(IDs::CC_LIST);
        int count = 0;
        if (cl.isValid())
        {
            const int numChildren = cl.getNumChildren();
            count = (std::min)(numChildren, MAX_CC_SLOTS);
            if (numChildren > MAX_CC_SLOTS)
            {
                HDAW_LOG("MidiClip", "clip '" + clipTree.getProperty(IDs::name, juce::String()).toString()
                    + "' has " + juce::String(numChildren) + " CC points, engine ceiling is " + juce::String(MAX_CC_SLOTS)
                    + " - points past the ceiling are ignored; split the clip into smaller clips");
            }
        }

        if (count == 0)
        {
            ccCount.store(0, std::memory_order_release);
            ccSnapshot.store({}, std::memory_order_release);
            return;
        }

        auto fresh = std::make_shared<CcCache>();
        fresh->points.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            auto pt = cl.getChild(i);
            fresh->points[i].controllerNumber = pt.getProperty(IDs::controllerNumber);
            fresh->points[i].beat = pt.getProperty(IDs::beat);
            fresh->points[i].value = pt.getProperty(IDs::value);
        }

        std::sort(fresh->points.begin(), fresh->points.end(),
                  [](const CcData& a, const CcData& b) { return a.beat < b.beat; });

        ccCount.store(count, std::memory_order_release);
        ccSnapshot.store(std::move(fresh), std::memory_order_release);
    }

    int procEntryCount = 0;
    int diagCount = 0;

    HDAW::TransportManager& transportManager;
    juce::ValueTree clipTree;

    std::atomic<double> startTime{ 0.0 };
    std::atomic<double> duration{ 1.0 };
    std::atomic<float> gain{ 1.0f };
    std::atomic<bool> muted{ false };

    std::atomic<int> midiChannel{ 1 }; // 1-16 = specific MIDI channel
    uint8_t lastCcByte = 255;
    std::array<bool, 128> activeNotes{};
    std::array<bool, MAX_NOTE_SLOTS> noteActive{};
    std::array<bool, MAX_NOTE_SLOTS> recurrenceDecision{}; // per-block scratch, audio thread only
    std::array<int, 128> pitchOwner{ -1 };
    std::array<bool, 128> previousNotePlayed{};
    std::atomic<uint64_t> clipSeed{0};

    // Immutable cache snapshots (see NoteCache/CcCache above). noteCount /
    // ccCount mirror the published snapshot sizes for cheap off-thread reads.
    std::atomic<std::shared_ptr<const NoteCache>> noteSnapshot{};
    std::atomic<std::shared_ptr<const CcCache>> ccSnapshot{};
    std::atomic<int> noteCount{0};
    std::atomic<int> ccCount{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiClipProcessor)
};

} // namespace HDAW
