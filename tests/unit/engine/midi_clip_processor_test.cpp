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
