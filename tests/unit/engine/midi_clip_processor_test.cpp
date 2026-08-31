#include <gtest/gtest.h>
#include "engine/MidiClipProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace {

juce::ValueTree makeCcOnlyClip(int controllerNumber, double beat, int value)
{
    juce::ValueTree clip(IDs::CLIP);
    clip.setProperty(IDs::clipType, "midi", nullptr);
    auto ccList = juce::ValueTree(IDs::CC_LIST);
    juce::ValueTree pt(IDs::CC_POINT);
    pt.setProperty(IDs::controllerNumber, controllerNumber, nullptr);
    pt.setProperty(IDs::beat, beat, nullptr);
    pt.setProperty(IDs::value, value, nullptr);
    ccList.addChild(pt, -1, nullptr);
    clip.addChild(ccList, -1, nullptr);
    return clip;
}

bool hasController(juce::MidiBuffer& midi, int controllerNumber, int value)
{
    for (const auto& meta : midi)
    {
        const auto& msg = meta.getMessage();
        if (msg.isController()
            && msg.getControllerNumber() == controllerNumber
            && msg.getControllerValue() == value)
            return true;
    }
    return false;
}

bool hasNoteOn(juce::MidiBuffer& midi, int noteNumber)
{
    for (const auto& meta : midi)
    {
        const auto& msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getNoteNumber() == noteNumber)
            return true;
    }
    return false;
}

bool hasNoteOff(juce::MidiBuffer& midi, int noteNumber)
{
    for (const auto& meta : midi)
    {
        const auto& msg = meta.getMessage();
        if (msg.isNoteOff() && msg.getNoteNumber() == noteNumber)
            return true;
    }
    return false;
}

juce::ValueTree makeNoteClip(int noteNumber, float velocity, double startBeat, double durationBeats)
{
    juce::ValueTree clip(IDs::CLIP);
    clip.setProperty(IDs::clipType, "midi", nullptr);
    auto noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
    juce::ValueTree n(IDs::MIDI_NOTE);
    n.setProperty(IDs::noteNumber, noteNumber, nullptr);
    n.setProperty(IDs::velocity, velocity, nullptr);
    n.setProperty(IDs::startBeat, startBeat, nullptr);
    n.setProperty(IDs::durationBeats, durationBeats, nullptr);
    n.setProperty(IDs::chance, 1.0f, nullptr);
    noteList.addChild(n, -1, nullptr);
    clip.addChild(noteList, -1, nullptr);
    return clip;
}

} // namespace

TEST(MidiClipProcessor, CcPlaybackEmitsAtBeat)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    proc.setClipTree(makeCcOnlyClip(74, 2.0, 100));
    proc.setStartTime(0.0);
    proc.setDuration(8.0);
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);

    tm.setCurrentSample(0);
    juce::MidiBuffer early;
    proc.processBlock(buffer, early);
    EXPECT_FALSE(hasController(early, 74, 100));

    tm.setCurrentSample(44100);
    juce::MidiBuffer atBeat;
    proc.processBlock(buffer, atBeat);
    EXPECT_TRUE(hasController(atBeat, 74, 100));
}

TEST(MidiClipProcessor, CcPlaybackEmitsOnlyOnce)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    proc.setClipTree(makeCcOnlyClip(74, 1.0, 90));
    proc.setStartTime(0.0);
    proc.setDuration(8.0);
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);

    tm.setCurrentSample(22050);
    juce::MidiBuffer first;
    proc.processBlock(buffer, first);
    EXPECT_TRUE(hasController(first, 74, 90));

    tm.setCurrentSample(22050 + 512);
    juce::MidiBuffer second;
    proc.processBlock(buffer, second);
    EXPECT_FALSE(hasController(second, 74, 90));
}

TEST(MidiClipProcessor, ClipSilentAtExactDurationBoundary)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    // 120 BPM → 2 beats/sec. 4 beats = 2 seconds.
    proc.setClipTree(makeNoteClip(60, 100, 0.0, 4.0));
    proc.setStartTime(1.0);
    proc.setDuration(2.0);
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);

    // Position transport at exactly the clip end (1.0 + 2.0 = 3.0 seconds)
    tm.setCurrentSample(static_cast<int64_t>(3.0 * 44100.0));
    tm.setPlaying(true);

    juce::MidiBuffer midi;
    proc.processBlock(buffer, midi);

    EXPECT_EQ(midi.getNumEvents(), 0);
}

TEST(MidiClipProcessor, ActiveNotesGetNoteOffAtClipEnd)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    // Long note (10 beats = 5 seconds) that extends past the 2-second clip
    proc.setClipTree(makeNoteClip(60, 100, 0.0, 10.0));
    proc.setStartTime(0.0);
    proc.setDuration(2.0);
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);

    // Play within the clip to activate the note
    tm.setCurrentSample(0);
    tm.setPlaying(true);

    juce::MidiBuffer midi;
    proc.processBlock(buffer, midi);
    EXPECT_TRUE(hasNoteOn(midi, 60));

    // Position past the clip end (2.5 > 2.0 duration)
    midi.clear();
    tm.setCurrentSample(static_cast<int64_t>(2.5 * 44100.0));
    proc.processBlock(buffer, midi);

    EXPECT_TRUE(hasNoteOff(midi, 60));
}


// ---------------------------------------------------------------------------
// Regression: the note/CC caches used to be fixed 512-entry arrays that
// truncated SILENTLY — clips with >512 notes lost everything past entry 512
// (docs/handoffs/2026-08-27-mcp-cluster-compose-session-bugs.md §1).
// The caches are now heap vectors sized to the list, with a loud-log hard
// ceiling at MAX_NOTE_SLOTS/MAX_CC_SLOTS.
// ---------------------------------------------------------------------------

juce::ValueTree makeManyNoteClip(int totalNotes)
{
    juce::ValueTree clip(IDs::CLIP);
    clip.setProperty(IDs::clipType, "midi", nullptr);
    auto noteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
    for (int i = 0; i < totalNotes; ++i)
    {
        juce::ValueTree n(IDs::MIDI_NOTE);
        // Entries before 512 sit far outside the playback window (beat 999);
        // entries at/past the legacy 512 cap sit inside it (beat 1.0..1.5).
        // Below-512 pitch 30 is outside the 36..123 range used by past-512
        // entries, so a collision cannot mask the window check.
        n.setProperty(IDs::noteNumber, i < 512 ? 30 : 36 + (i - 512), nullptr);
        n.setProperty(IDs::velocity, 100, nullptr);
        n.setProperty(IDs::startBeat, i < 512 ? 999.0 : 1.0, nullptr);
        n.setProperty(IDs::durationBeats, i < 512 ? 0.01 : 0.5, nullptr);
        n.setProperty(IDs::chance, 1.0f, nullptr);
        noteList.addChild(n, -1, nullptr);
    }
    clip.addChild(noteList, -1, nullptr);
    return clip;
}

juce::ValueTree makeManyCcClip(int totalPoints)
{
    juce::ValueTree clip(IDs::CLIP);
    clip.setProperty(IDs::clipType, "midi", nullptr);
    auto ccList = juce::ValueTree(IDs::CC_LIST);
    for (int i = 0; i < totalPoints; ++i)
    {
        juce::ValueTree pt(IDs::CC_POINT);
        pt.setProperty(IDs::controllerNumber, 74, nullptr);
        // First 512 points at beat 100 (outside); the rest at beat 1.0.
        pt.setProperty(IDs::beat, i < 512 ? 100.0 : 1.0, nullptr);
        pt.setProperty(IDs::value, i == totalPoints - 1 ? 100 : (i < 512 ? 7 : 90), nullptr);
        ccList.addChild(pt, -1, nullptr);
    }
    clip.addChild(ccList, -1, nullptr);
    return clip;
}

TEST(MidiClipProcessor, NoteCachePlaysNotesBeyondLegacy512Limit)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    proc.setClipTree(makeManyNoteClip(600));
    EXPECT_EQ(proc.getNumCachedNotes(), 600);
    proc.setStartTime(0.0);
    proc.setDuration(8.0);
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);

    // 120 BPM -> 2 beats/sec. 0.6 s -> beat 1.2: inside [1.0, 1.5) of the
    // past-512 entries, far away from the first-512 entries (beat 999).
    tm.setCurrentSample(static_cast<int64_t>(0.6 * 44100.0));
    tm.setPlaying(true);

    juce::MidiBuffer midi;
    proc.processBlock(buffer, midi);

    EXPECT_TRUE(hasNoteOn(midi, 114));  // entry 590: pitch 36 + (590 - 512)
    EXPECT_FALSE(hasNoteOn(midi, 30));  // entry 0's pitch: outside the window

    int noteOnCount = 0;
    for (const auto& meta : midi)
        if (meta.getMessage().isNoteOn()) ++noteOnCount;
    EXPECT_EQ(noteOnCount, 88); // entries 512..599 fire exactly once
}

TEST(MidiClipProcessor, CcCacheEmitsPointsBeyondLegacy512Limit)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    proc.setClipTree(makeManyCcClip(600));
    proc.setStartTime(0.0);
    proc.setDuration(8.0);
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);

    // Block window [0.98, ~1.003) beats contains beat 1.0 (past-512 points)
    // but not beat 100 (first-512 points).
    tm.setCurrentSample(static_cast<int64_t>(0.49 * 44100.0));
    tm.setPlaying(true);

    juce::MidiBuffer midi;
    proc.processBlock(buffer, midi);

    EXPECT_TRUE(hasController(midi, 74, 100)); // final point (index 599)
}

TEST(MidiClipProcessor, NoteCacheClampsAtSlotCeiling)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    proc.setClipTree(makeManyNoteClip(HDAW::MidiClipProcessor::MAX_NOTE_SLOTS + 5));
    // Hard safety ceiling: cache count clamps (and the processor logs loudly).
    EXPECT_EQ(proc.getNumCachedNotes(), HDAW::MidiClipProcessor::MAX_NOTE_SLOTS);
}

// Regression (2026-09-02 session): a clip tree containing an out-of-range
// noteNumber (pitch 160, written by a generator bug before the MIDI-addressable
// cap existed) crashed processBlock with MSVC's "array subscript out of range"
// — previousNotePlayed[] indexed raw noteNumber. The render path must clamp
// every pitch-table index and still emit valid (clamped) MIDI.
TEST(MidiClipProcessor, OutOfRangePitchNoteDoesNotCrashRender)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    proc.setClipTree(makeNoteClip(160, 0.8f, 0.0, 4.0));
    proc.setStartTime(0.0);
    proc.setDuration(8.0);
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);
    for (int block = 0; block < 16; ++block)
    {
        tm.setCurrentSample(block * 512);
        juce::MidiBuffer midi;
        proc.processBlock(buffer, midi);   // must not assert / crash
        if (block == 0)
        {
            // The note plays clamped to 127 — a legal MIDI note — never 160.
            EXPECT_TRUE(hasNoteOn(midi, 127));
            EXPECT_FALSE(hasNoteOn(midi, 160));
        }
    }
}
