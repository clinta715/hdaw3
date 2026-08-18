#include <gtest/gtest.h>
#include "engine/MidiFx.h"
#include "engine/Track.h"
#include <juce_audio_processors/juce_audio_processors.h>

using namespace HDAW;

namespace {

juce::AudioPlayHead::PositionInfo makePos(double ppq, double bpm)
{
    juce::AudioPlayHead::PositionInfo pos;
    pos.setIsPlaying(true);
    pos.setPpqPosition(ppq);
    pos.setBpm(bpm);
    return pos;
}

std::vector<int> collectNoteOns(juce::MidiBuffer& buf)
{
    std::vector<std::pair<int, int>> events; // (sample, note)
    for (const auto meta : buf)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            events.push_back({ meta.samplePosition, msg.getNoteNumber() });
    }
    std::sort(events.begin(), events.end());
    std::vector<int> notes;
    for (const auto& e : events) notes.push_back(e.second);
    return notes;
}

juce::MidiBuffer holdChord()
{
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 0);
    return buf;
}

} // namespace

TEST(Arpeggiator, UpPattern)
{
    Arpeggiator arp;
    arp.rate = 0.25; arp.pattern = 0; arp.octaves = 1; arp.gate = 0.5;

    auto buf = holdChord();
    auto pos = makePos(0.0, 120.0);
    arp.process(buf, &pos, 44100.0, 22050); // one beat -> four 1/16 steps

    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 64);
    EXPECT_EQ(notes[2], 67);
    EXPECT_EQ(notes[3], 60);
}

TEST(Arpeggiator, DownPattern)
{
    Arpeggiator arp;
    arp.rate = 0.25; arp.pattern = 1; arp.octaves = 1; arp.gate = 0.5;

    auto buf = holdChord();
    auto pos = makePos(0.0, 120.0);
    arp.process(buf, &pos, 44100.0, 22050);

    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_EQ(notes[0], 67);
    EXPECT_EQ(notes[1], 64);
    EXPECT_EQ(notes[2], 60);
    EXPECT_EQ(notes[3], 67);
}

TEST(Arpeggiator, OctavesStack)
{
    Arpeggiator arp;
    arp.rate = 0.25; arp.pattern = 0; arp.octaves = 2; arp.gate = 0.5;

    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    auto pos = makePos(0.0, 120.0);
    arp.process(buf, &pos, 44100.0, 22050);

    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 72);
    EXPECT_EQ(notes[2], 60);
    EXPECT_EQ(notes[3], 72);
}

TEST(Arpeggiator, NoHeldNotesNoOutput)
{
    Arpeggiator arp;
    juce::MidiBuffer buf;
    auto pos = makePos(0.0, 120.0);
    arp.process(buf, &pos, 44100.0, 512);
    EXPECT_EQ(collectNoteOns(buf).size(), 0u);
}

TEST(Arpeggiator, ReleaseStopsNote)
{
    Arpeggiator arp;
    arp.rate = 0.25; arp.pattern = 0; arp.octaves = 1; arp.gate = 0.5;

    auto buf = holdChord();
    auto pos = makePos(0.0, 120.0);
    arp.process(buf, &pos, 44100.0, 22050);
    EXPECT_FALSE(collectNoteOns(buf).empty());

    // Release all held notes; the arp should emit a note-off and go silent.
    juce::MidiBuffer release;
    release.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    release.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
    release.addEvent(juce::MidiMessage::noteOff(1, 67), 0);
    auto pos2 = makePos(1.0, 120.0);
    arp.process(release, &pos2, 44100.0, 22050);
    EXPECT_EQ(collectNoteOns(release).size(), 0u);
}

namespace {
struct TestPlayHead : juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(true);
        info.setBpm(120.0);
        info.setPpqPosition(0.0);
        info.setTimeInSeconds(0.0);
        return info;
    }
};
} // namespace

TEST(TrackMidiFx, ArpeggiatorInProcessBlock)
{
    HDAW::Track track;
    TestPlayHead playhead;
    track.setPlayHead(&playhead);
    track.prepareToPlay(44100.0, 22050);

    juce::ValueTree chain(IDs::MIDI_FX_CHAIN);
    juce::ValueTree slot(IDs::MIDI_FX_SLOT);
    slot.setProperty(IDs::fxType, "arpeggiator", nullptr);
    slot.setProperty(IDs::arpRate, 0.25, nullptr);
    slot.setProperty(IDs::arpPattern, 0, nullptr);
    slot.setProperty(IDs::arpOctaves, 1, nullptr);
    slot.setProperty(IDs::arpGate, 0.5, nullptr);
    slot.setProperty(IDs::bypassed, false, nullptr);
    chain.addChild(slot, -1, nullptr);

    track.rebuildMidiFXChain(chain);
    ASSERT_EQ(track.getNumMidiFxSlots(), 1);

    juce::AudioBuffer<float> audio(2, 22050);
    juce::MidiBuffer midi = holdChord();
    track.processBlock(audio, midi);

    auto notes = collectNoteOns(midi);
    ASSERT_EQ(notes.size(), 4u);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 64);
    EXPECT_EQ(notes[2], 67);
    EXPECT_EQ(notes[3], 60);
}

TEST(VelocityScaler, ScalesVelocity)
{
    VelocityScaler vs;
    vs.factor = 2.0;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)64), 0);
    vs.process(buf, nullptr, 44100.0, 512);
    int vel = -1;
    for (const auto meta : buf)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) vel = msg.getVelocity();
    }
    EXPECT_EQ(vel, 127); // 64 * 2 = 128 clamped to 127
}

TEST(Chorder, MajorTriad)
{
    Chorder ch;
    ch.chordType = 0;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    ch.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 64);
    EXPECT_EQ(notes[2], 67);
}

TEST(ScaleQuantize, SnapsToMajor)
{
    ScaleQuantize sq;
    sq.root = 0;
    sq.scaleType = 0; // C major
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), 0); // C#
    sq.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], 60); // snapped to C
}

TEST(NoteLengthScaler, HalvesDuration)
{
    NoteLengthScaler nl;
    nl.factor = 0.5;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0); // beat 0
    buf.addEvent(juce::MidiMessage::noteOff(1, 60), 22050);              // beat 1.0
    auto pos = makePos(0.0, 120.0);
    nl.process(buf, &pos, 44100.0, 44100); // two beats
    int offSample = -1;
    for (const auto meta : buf)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOff()) offSample = meta.samplePosition;
    }
    EXPECT_EQ(offSample, 11025); // beat 0.5
}

TEST(Transpose, ShiftsUp)
{
    Transpose tr;
    tr.semitones = 7;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    tr.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], 67);
}

TEST(Transpose, ClampsTo127)
{
    Transpose tr;
    tr.semitones = 24;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 120, (juce::uint8)100), 0);
    tr.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], 127);
}

TEST(Transpose, ClampsTo0)
{
    Transpose tr;
    tr.semitones = -24;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 10, (juce::uint8)100), 0);
    tr.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], 0);
}

TEST(KeyFilter, DropsOutOfKey)
{
    KeyFilter kf;
    kf.root = 0;
    kf.scaleType = 0; // C major: C D E F G A B
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0); // C  -> in key
    buf.addEvent(juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), 0); // C# -> out
    buf.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 0); // D  -> in key
    kf.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 2u);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 62);
}

TEST(KeyFilter, PassesInKey)
{
    KeyFilter kf;
    kf.root = 0;
    kf.scaleType = 0; // C major
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0); // C
    buf.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 0); // D
    buf.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0); // E
    kf.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 62);
    EXPECT_EQ(notes[2], 64);
}

TEST(MultiNote, AddsOctaveAndFifth)
{
    MultiNote mn;
    mn.intervals = { 0, 7, 12 };
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    mn.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 3u);
    EXPECT_EQ(notes[0], 60);
    EXPECT_EQ(notes[1], 67);
    EXPECT_EQ(notes[2], 72);
}

TEST(MultiNote, SingleInterval)
{
    MultiNote mn;
    mn.intervals = { 0 };
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    mn.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], 60);
}

TEST(VelocityCurve, Compress)
{
    VelocityCurve vc;
    vc.curveType = 1; // compress
    vc.curveAmount = 1.0;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)127), 0);
    vc.process(buf, nullptr, 44100.0, 512);
    int vel = -1;
    for (const auto meta : buf)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) vel = msg.getVelocity();
    }
    // compress: result = norm + (0.5 - norm) * 1.0 = 0.5 → 64
    EXPECT_EQ(vel, 64);
}

TEST(VelocityCurve, Fixed)
{
    VelocityCurve vc;
    vc.curveType = 4; // fixed
    vc.curveAmount = 0.5;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)20), 0);
    vc.process(buf, nullptr, 44100.0, 512);
    std::vector<int> vels;
    for (const auto meta : buf)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) vels.push_back(msg.getVelocity());
    }
    ASSERT_EQ(vels.size(), 2u);
    EXPECT_EQ(vels[0], 64);
    EXPECT_EQ(vels[1], 64);
}

TEST(NoteChance, AlwaysPassAtOne)
{
    NoteChance nc;
    nc.noteChance = 1.0;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    nc.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], 60);
}

TEST(MidiDelay, DoesNotCrash)
{
    MidiDelay md;
    md.delayBeats = 0.25;
    md.feedback = 0.0;
    md.mix = 0.5;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    auto pos = makePos(0.0, 120.0);
    md.process(buf, &pos, 44100.0, 512);
    // With mix > 0 the original note passes through
    auto notes = collectNoteOns(buf);
    EXPECT_GE(notes.size(), 1u);
}

TEST(MidiDelay, FeedbackRescheduleDoesNotCorrupt)
{
    MidiDelay md;
    md.delayBeats = 0.25;
    md.feedback = 0.5;
    md.mix = 0.5;
    bool emitted = false;
    for (int iter = 0; iter < 40; ++iter)
    {
        juce::MidiBuffer buf;
        buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
        buf.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)90), 0);
        buf.addEvent(juce::MidiMessage::noteOff(1, 64), 64);
        buf.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)80), 128);
        auto pos = makePos(iter * 0.5, 120.0);
        md.process(buf, &pos, 44100.0, 512);
        if (!collectNoteOns(buf).empty())
            emitted = true;
    }
    EXPECT_TRUE(emitted);
}

TEST(Humanize, DoesNotCrash)
{
    Humanize hu;
    hu.humanizeTiming = 0.5;
    hu.humanizeVelocity = 0.5;
    hu.humanizePitch = 0.5;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0);
    hu.process(buf, nullptr, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    EXPECT_EQ(notes.size(), 2u);
}

TEST(Strum, DoesNotCrash)
{
    Strum st;
    st.strumTime = 0.02;
    st.strumDirection = 0;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0);
    buf.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 0);
    auto pos = makePos(0.0, 120.0);
    st.process(buf, &pos, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    EXPECT_EQ(notes.size(), 3u);
}

TEST(Strum, SingleNotePassthrough)
{
    Strum st;
    st.strumTime = 0.02;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    auto pos = makePos(0.0, 120.0);
    st.process(buf, &pos, 44100.0, 512);
    auto notes = collectNoteOns(buf);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes[0], 60);
}
