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

TEST(Arpeggiator, SustainedNotesAcrossBlocks)
{
    Arpeggiator arp;
    arp.rate = 0.25; arp.pattern = 0; arp.octaves = 1; arp.gate = 0.9;

    const double bpm = 174.0;
    const double sampleRate = 44100.0;
    const int numSamples = 1024;
    const double beatsPerBlock = numSamples * bpm / 60.0 / sampleRate;

    // Block 0: send note-ons at beat 0
    auto buf0 = holdChord();
    auto pos0 = makePos(0.0, bpm);
    arp.process(buf0, &pos0, sampleRate, numSamples);
    auto notes0 = collectNoteOns(buf0);
    EXPECT_GE(notes0.size(), 1u) << "Block 0: no notes produced";

    // Process 20 more blocks with no new MIDI (sustained notes).
    // At rate=0.25 and beatsPerBlock~0.067, a step fires roughly every
    // 3.7 blocks. Over 20 blocks we expect at least 4 blocks with notes.
    int blocksWithNotes = 0;
    for (int i = 1; i <= 20; ++i)
    {
        juce::MidiBuffer buf;
        auto pos = makePos(i * beatsPerBlock, bpm);
        arp.process(buf, &pos, sampleRate, numSamples);
        if (!collectNoteOns(buf).empty())
            ++blocksWithNotes;
    }
    EXPECT_GE(blocksWithNotes, 4) << "Arpeggiator produced notes in too few blocks with sustained input";
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

// ═══════════════════════════════════════════════════════════════════════
// AcidStep — 16-step acid sequencer MIDI FX (plan 2026-10-04-acid-step-midi-fx)
// 48 kHz / 120 BPM => one beat = 24000 samples, so 1/16 = 6000, 1/8 = 12000,
// 1/4 = 24000: every onset is an exact integer sample.
// ═══════════════════════════════════════════════════════════════════════
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "common/ReadModel.h"
#include <cstdint>
#include <set>
#include <string>
#include <tuple>

namespace {

struct AEv
{
    long long s = 0;
    char k = '?';   // N note-on, F note-off, A poly aftertouch
    int note = 0;
    int val = 0;    // velocity / aftertouch amount
    std::string raw;
    bool operator==(const AEv& o) const
    { return std::tie(s, k, note, val, raw) == std::tie(o.s, o.k, o.note, o.val, o.raw); }
    bool operator!=(const AEv& o) const { return !(*this == o); }
};

std::vector<AEv> collectAcid(const juce::MidiBuffer& buf, long long base)
{
    std::vector<AEv> out;
    for (const auto meta : buf)
    {
        const auto msg = meta.getMessage();
        AEv e;
        e.s = base + meta.samplePosition;
        e.raw.assign(reinterpret_cast<const char*>(meta.data), static_cast<size_t>(meta.numBytes));
        if (msg.isNoteOn())          { e.k = 'N'; e.note = msg.getNoteNumber(); e.val = msg.getVelocity(); }
        else if (msg.isNoteOff())    { e.k = 'F'; e.note = msg.getNoteNumber(); }
        else if (msg.isAftertouch()) { e.k = 'A'; e.note = msg.getNoteNumber(); e.val = msg.getAfterTouchValue(); }
        out.push_back(e);
    }
    return out;
}

// One block at an explicit ppq / transport state. Event samples are made absolute with base.
std::vector<AEv> acidBlock(MidiEffect& fx, double ppq, bool playing, int n, long long base,
                           double bpm = 120.0, double sr = 48000.0)
{
    juce::MidiBuffer b;
    auto pos = makePos(ppq, bpm);
    pos.setIsPlaying(playing);
    fx.process(b, &pos, sr, n);
    return collectAcid(b, base);
}

std::vector<AEv> acidRun(MidiEffect& fx, int numBlocks, int bs = 512,
                         double bpm = 120.0, double sr = 48000.0)
{
    std::vector<AEv> all;
    for (int b = 0; b < numBlocks; ++b)
    {
        const double ppq = static_cast<double>(b) * bs * bpm / 60.0 / sr;
        auto ev = acidBlock(fx, ppq, true, bs, static_cast<long long>(b) * bs, bpm, sr);
        all.insert(all.end(), ev.begin(), ev.end());
    }
    return all;
}

std::vector<AEv> ofKind(const std::vector<AEv>& v, char k)
{
    std::vector<AEv> o;
    for (const auto& e : v) if (e.k == k) o.push_back(e);
    return o;
}

// note-on count minus note-off count for one pitch
int acidBalance(const std::vector<AEv>& v, int note)
{
    int b = 0;
    for (const auto& e : v)
    {
        if (e.note != note) continue;
        if (e.k == 'N') ++b;
        if (e.k == 'F') --b;
    }
    return b;
}

void setReal(MidiFxSlot& slot, int idx, float real)
{
    const auto defs = getMidiFxParamDefs("acid_step");
    const auto& d = defs[static_cast<size_t>(idx)];
    slot.setAutomationParam(idx, (real - d.minValue) / (d.maxValue - d.minValue));
}

constexpr int kAcidSlotBlocks = 192;  // 192 * 512 = 98304 samples = 16.38 steps at 1/16
constexpr int kStepNote(int n) { return 12 + (n - 1) * 4; }

} // namespace

TEST(AcidStep, OnsetsAreSampleAccurateAtEachRate)
{
    const struct { double rate; int interval; } cases[] = { {0.25, 6000}, {0.5, 12000}, {1.0, 24000} };
    for (const auto& c : cases)
    {
        AcidStep a;
        a.rate = c.rate;
        const int blocks = 94; // 48128 samples
        const auto ev = acidRun(a, blocks);
        const auto ons = ofKind(ev, 'N');
        std::vector<long long> expected;
        for (long long s = 0; s < blocks * 512; s += c.interval) expected.push_back(s);
        ASSERT_EQ(ons.size(), expected.size()) << "rate " << c.rate;
        for (size_t i = 0; i < ons.size(); ++i)
        {
            EXPECT_EQ(ons[i].s, expected[i]) << "rate " << c.rate << " onset " << i;
            EXPECT_EQ(ons[i].note, 36);
            EXPECT_EQ(ons[i].val, 96);
        }
        // gate 0.5: each note-off lands half a step after its on
        const auto offs = ofKind(ev, 'F');
        ASSERT_FALSE(offs.empty());
        EXPECT_EQ(offs[0].s, c.interval / 2) << "rate " << c.rate;
    }
}

TEST(AcidStep, OnsetsAreIndependentOfBlockSize)
{
    AcidStep a1, a2;
    for (auto* a : { &a1, &a2 }) { a->stepData[2].note = 5; a->swing = 0.25; }
    const auto e1 = acidRun(a1, 192, 512);   // 98304 samples
    const auto e2 = acidRun(a2, 384, 256);   // same span
    ASSERT_EQ(e1.size(), e2.size());
    for (size_t i = 0; i < e1.size(); ++i)
        EXPECT_TRUE(e1[i].s == e2[i].s && e1[i].k == e2[i].k && e1[i].note == e2[i].note)
            << "event " << i << " " << e1[i].s << " vs " << e2[i].s;
}

TEST(AcidStep, NoteOctaveAndBaseNoteApplied)
{
    AcidStep a;
    a.baseNote = 40; a.octave = 1;
    a.stepData[0].note = 3;
    a.stepData[1].note = -5;
    a.stepData[2].note = 0;
    const auto ons = ofKind(acidRun(a, 40), 'N');
    ASSERT_GE(ons.size(), 3u);
    EXPECT_EQ(ons[0].note, 40 + 12 + 3);
    EXPECT_EQ(ons[1].note, 40 + 12 - 5);
    EXPECT_EQ(ons[2].note, 40 + 12);

    AcidStep lo; lo.baseNote = 36; lo.octave = -2; lo.stepData[0].note = -24;
    const auto lons = ofKind(acidRun(lo, 20), 'N');
    ASSERT_FALSE(lons.empty());
    EXPECT_EQ(lons[0].note, 0); // 36 - 24 - 24 = -12 clamped
}

TEST(AcidStep, RestStepEmitsNothing)
{
    AcidStep a;
    a.stepData[1].rest = 1.0f;
    const auto ev = acidRun(a, 40); // ~4 steps
    for (const auto& e : ev)
        EXPECT_FALSE(e.s >= 6000 && e.s < 12000) << "event during the rest step: " << e.k << "@" << e.s;
    const auto ons = ofKind(ev, 'N');
    ASSERT_GE(ons.size(), 3u);
    EXPECT_EQ(ons[0].s, 0);
    EXPECT_EQ(ons[1].s, 12000); // step 3, not step 2
}

TEST(AcidStep, PatternWrapsAfterStepsParam)
{
    AcidStep a;
    a.steps = 3;
    a.stepData[0].note = 0; a.stepData[1].note = 1; a.stepData[2].note = 2;
    a.stepData[3].note = 9; // beyond the pattern length: must never sound
    const auto ons = ofKind(acidRun(a, 94), 'N');
    ASSERT_GE(ons.size(), 8u);
    const int expected[] = { 36, 37, 38, 36, 37, 38, 36, 37 };
    for (int i = 0; i < 8; ++i) EXPECT_EQ(ons[i].note, expected[i]) << "onset " << i;
    for (const auto& o : ons) EXPECT_NE(o.note, 45);
}

TEST(AcidStep, DirectionReverseAndPingPong)
{
    AcidStep r;
    r.steps = 4; r.direction = 1;
    for (int i = 0; i < 4; ++i) r.stepData[i].note = i;
    auto ons = ofKind(acidRun(r, 94), 'N');
    ASSERT_GE(ons.size(), 5u);
    const int rev[] = { 39, 38, 37, 36, 39 };
    for (int i = 0; i < 5; ++i) EXPECT_EQ(ons[i].note, rev[i]);

    AcidStep p;
    p.steps = 4; p.direction = 2;
    for (int i = 0; i < 4; ++i) p.stepData[i].note = i;
    ons = ofKind(acidRun(p, 94), 'N');
    ASSERT_GE(ons.size(), 8u);
    const int pp[] = { 36, 37, 38, 39, 38, 37, 36, 37 };
    for (int i = 0; i < 8; ++i) EXPECT_EQ(ons[i].note, pp[i]) << "onset " << i;
}

TEST(AcidStep, AccentEmitsAccentVelocityAndAftertouchAtSameSample)
{
    AcidStep a;
    a.stepData[1].accent = 1.0f;
    a.stepData[1].note = 7;
    a.accentVelocity = 120;
    a.accentAmount = 77;
    const auto ev = acidRun(a, 40);

    // step 1 (not accented): plain velocity, no aftertouch at 0
    for (const auto& e : ev) if (e.s == 0) { EXPECT_NE(e.k, 'A'); }
    // step 2 (accented): note-on(vel 120) immediately followed by aftertouch(note, 77) at 6000
    int nIdx = -1;
    for (size_t i = 0; i < ev.size(); ++i)
        if (ev[i].k == 'N' && ev[i].s == 6000) nIdx = static_cast<int>(i);
    ASSERT_GE(nIdx, 0);
    EXPECT_EQ(ev[nIdx].val, 120);
    EXPECT_EQ(ev[nIdx].note, 43);
    ASSERT_LT(static_cast<size_t>(nIdx + 1), ev.size());
    EXPECT_EQ(ev[nIdx + 1].k, 'A');
    EXPECT_EQ(ev[nIdx + 1].s, 6000);
    EXPECT_EQ(ev[nIdx + 1].note, 43);
    EXPECT_EQ(ev[nIdx + 1].val, 77);
    EXPECT_EQ(ofKind(ev, 'A').size(), 1u);
    // the non-accented note-ons keep the normal velocity
    for (const auto& e : ofKind(ev, 'N'))
        if (e.s != 6000) EXPECT_EQ(e.val, 96);
}

TEST(AcidStep, SlideIsLegatoOffThenOnAtSameSample)
{
    AcidStep a;
    a.stepData[0].slide = 1.0f;
    a.stepData[1].note = 7;
    const auto ev = acidRun(a, 40);

    // No note-off at the normal gate point (3000): the note is held.
    for (const auto& e : ev) EXPECT_FALSE(e.k == 'F' && e.s == 3000);
    // At 6000: off(36) then on(43), same sample, in that order.
    std::vector<AEv> at6000;
    for (const auto& e : ev) if (e.s == 6000) at6000.push_back(e);
    ASSERT_EQ(at6000.size(), 2u);
    EXPECT_EQ(at6000[0].k, 'F'); EXPECT_EQ(at6000[0].note, 36);
    EXPECT_EQ(at6000[1].k, 'N'); EXPECT_EQ(at6000[1].note, 43);
    // The unslid step 2 note ends at its own gate (9000).
    bool off43 = false;
    for (const auto& e : ev) if (e.k == 'F' && e.note == 43) { EXPECT_EQ(e.s, 9000); off43 = true; }
    EXPECT_TRUE(off43);
}

TEST(AcidStep, TieModeOverlapsInsteadOfLegato)
{
    AcidStep a;
    a.slideMode = 1;
    a.stepData[0].slide = 1.0f;
    a.stepData[1].note = 7;
    const auto ev = acidRun(a, 40);

    long long on43 = -1, off36 = -1;
    size_t on43Idx = 0, off36Idx = 0;
    for (size_t i = 0; i < ev.size(); ++i)
    {
        if (ev[i].k == 'N' && ev[i].note == 43) { on43 = ev[i].s; on43Idx = i; }
        if (off36 < 0 && ev[i].k == 'F' && ev[i].note == 36 && ev[i].s >= 3000) { off36 = ev[i].s; off36Idx = i; }
    }
    EXPECT_EQ(on43, 6000);
    ASSERT_GE(off36, 0);
    EXPECT_GT(off36, on43) << "tie: previous note-off must come AFTER the next note-on (overlap)";
    EXPECT_GT(off36Idx, on43Idx);
    EXPECT_EQ(off36, 7500); // onset + 0.5 * gate * rate = 6000 + 1500
}

TEST(AcidStep, SlideIntoRestEndsAtTheRest)
{
    AcidStep a;
    a.stepData[0].slide = 1.0f;
    a.stepData[1].rest = 1.0f;
    const auto ev = acidRun(a, 40);
    bool off = false;
    for (const auto& e : ev) if (e.k == 'F' && e.note == 36 && e.s < 12000) { EXPECT_EQ(e.s, 6000); off = true; }
    EXPECT_TRUE(off);
}

TEST(AcidStep, TransportStopFlushesPendingNoteOffExactlyOnce)
{
    AcidStep a;
    auto ev = acidBlock(a, 0.0, true, 512, 0);
    ASSERT_EQ(ofKind(ev, 'N').size(), 1u);
    EXPECT_EQ(acidBalance(ev, 36), 1);
    const double bps = 120.0 / 60.0 / 48000.0;

    auto stopped = acidBlock(a, 512 * bps, false, 512, 512);
    ASSERT_EQ(stopped.size(), 1u);
    EXPECT_EQ(stopped[0].k, 'F'); EXPECT_EQ(stopped[0].note, 36); EXPECT_EQ(stopped[0].s, 512);
    EXPECT_TRUE(acidBlock(a, 1024 * bps, false, 512, 1024).empty());
    EXPECT_TRUE(acidBlock(a, 1536 * bps, false, 512, 1536).empty());

    // A null position (no playhead) is also "stopped".
    AcidStep b;
    (void) acidBlock(b, 0.0, true, 512, 0);
    juce::MidiBuffer nb;
    b.process(nb, nullptr, 48000.0, 512);
    EXPECT_EQ(acidBalance(collectAcid(nb, 0), 36), -1);
    nb.clear(); b.process(nb, nullptr, 48000.0, 512);
    EXPECT_TRUE(collectAcid(nb, 0).empty());

    // Restart plays from the top again.
    auto restart = acidBlock(a, 0.0, true, 512, 0);
    ASSERT_EQ(ofKind(restart, 'N').size(), 1u);
}

TEST(AcidStep, StepsChangeMidNoteEmitsPendingOffOnce)
{
    AcidStep a;
    const double bps = 120.0 / 60.0 / 48000.0;
    auto ev = acidBlock(a, 0.0, true, 512, 0);
    ASSERT_EQ(acidBalance(ev, 36), 1);
    a.steps = 8;
    for (int b = 1; b < 12; ++b) // up to 6144 samples
    {
        auto e = acidBlock(a, b * 512 * bps, true, 512, b * 512);
        ev.insert(ev.end(), e.begin(), e.end());
    }
    int offsBefore6000 = 0;
    for (const auto& e : ev) if (e.k == 'F' && e.s < 6000) { ++offsBefore6000; EXPECT_EQ(e.s, 512); }
    EXPECT_EQ(offsBefore6000, 1);
}

TEST(AcidStep, RateChangeMidNoteEmitsPendingOffOnce)
{
    AcidStep a;
    const double bps = 120.0 / 60.0 / 48000.0;
    auto ev = acidBlock(a, 0.0, true, 512, 0);
    ASSERT_EQ(acidBalance(ev, 36), 1);
    a.rate = 0.5;
    for (int b = 1; b < 24; ++b) // up to 12288 samples (new step at 12000)
    {
        auto e = acidBlock(a, b * 512 * bps, true, 512, b * 512);
        ev.insert(ev.end(), e.begin(), e.end());
    }
    int offs = 0, onsBetween = 0;
    for (const auto& e : ev)
    {
        if (e.k == 'F' && e.s < 12000) { ++offs; EXPECT_EQ(e.s, 512); }
        if (e.k == 'N' && e.s > 0 && e.s < 12000) ++onsBetween;
    }
    EXPECT_EQ(offs, 1);
    EXPECT_EQ(onsBetween, 0);
}

TEST(AcidStep, BackwardsPlayheadJumpResetsStepAndStrandsNothing)
{
    AcidStep a;
    a.slideMode = 0;
    a.stepData[0].slide = 1.0f; // held note: would be stranded without the flush
    a.stepData[1].note = 7;
    const double bps = 120.0 / 60.0 / 48000.0;
    // Play to mid step 2 (so step 1 has fired), then jump back to ppq 0.
    std::vector<AEv> ev;
    for (int b = 0; b < 4; ++b)
    {
        auto e = acidBlock(a, b * 512 * bps, true, 512, b * 512);
        ev.insert(ev.end(), e.begin(), e.end());
    }
    EXPECT_EQ(acidBalance(ev, 36), 1); // held (slide) at the jump

    auto jump = acidBlock(a, 0.0, true, 512, 100000);
    ASSERT_GE(jump.size(), 2u);
    EXPECT_EQ(jump[0].k, 'F'); EXPECT_EQ(jump[0].note, 36); EXPECT_EQ(jump[0].s, 100000);
    EXPECT_EQ(jump[1].k, 'N'); EXPECT_EQ(jump[1].note, 36); EXPECT_EQ(jump[1].s, 100000); // step 1 fires again
    ev.insert(ev.end(), jump.begin(), jump.end());

    // keep playing from there, then stop: every note is closed exactly once.
    for (int b = 1; b < 30; ++b)
    {
        auto e = acidBlock(a, b * 512 * bps, true, 512, 100000 + b * 512);
        ev.insert(ev.end(), e.begin(), e.end());
    }
    auto stop = acidBlock(a, 30 * 512 * bps, false, 512, 100000 + 30 * 512);
    ev.insert(ev.end(), stop.begin(), stop.end());
    for (int n = 0; n < 128; ++n) EXPECT_EQ(acidBalance(ev, n), 0) << "stranded/duplicated note " << n;
}

TEST(AcidStep, IdenticalPlayheadRunsAreByteIdentical)
{
    auto configure = [](AcidStep& a) {
        a.steps = 12; a.direction = 2; a.swing = 0.3; a.slideMode = 1; a.octave = 1;
        for (int i = 0; i < 12; ++i)
        {
            a.stepData[i].note = (i * 5) % 17 - 6;
            a.stepData[i].accent = (i % 3 == 0) ? 1.0f : 0.0f;
            a.stepData[i].slide = (i % 4 == 1) ? 1.0f : 0.0f;
            a.stepData[i].rest = (i % 7 == 6) ? 1.0f : 0.0f;
        }
    };
    AcidStep a, b;
    configure(a); configure(b);
    const auto ea = acidRun(a, kAcidSlotBlocks);
    const auto eb = acidRun(b, kAcidSlotBlocks);
    ASSERT_FALSE(ea.empty());
    EXPECT_EQ(ea, eb); // includes the raw message bytes + sample positions

    // reset() really clears state: a used-then-reset instance reproduces the run.
    a.reset();
    const auto ec = acidRun(a, kAcidSlotBlocks);
    EXPECT_EQ(ea, ec);
}

TEST(AcidStep, ResetClearsPendingNoteAndStepState)
{
    AcidStep a;
    (void) acidBlock(a, 0.0, true, 512, 0);
    a.reset();
    // After reset a continuing block must not emit a stale note-off ...
    const double bps = 120.0 / 60.0 / 48000.0;
    auto e = acidBlock(a, 512 * bps, true, 512, 512);
    EXPECT_TRUE(ofKind(e, 'F').empty());
    // ... and a fresh start at ppq 0 re-fires step 1.
    a.reset();
    EXPECT_EQ(ofKind(acidBlock(a, 0.0, true, 512, 0), 'N').size(), 1u);
}

TEST(AcidStep, InputNotesAreConsumedOtherEventsPassThrough)
{
    AcidStep a;
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 99, (juce::uint8)100), 3);
    buf.addEvent(juce::MidiMessage::noteOff(1, 99), 5);
    buf.addEvent(juce::MidiMessage::controllerEvent(1, 74, 55), 7);
    auto pos = makePos(0.0, 120.0);
    a.process(buf, &pos, 48000.0, 512);
    bool cc = false;
    for (const auto meta : buf)
    {
        const auto m = meta.getMessage();
        EXPECT_NE(m.getNoteNumber(), 99) << "input note leaked";
        if (m.isController() && m.getControllerNumber() == 74) cc = true;
    }
    EXPECT_TRUE(cc);
}

TEST(AcidStep, LatchWaitsForFirstInputNote)
{
    AcidStep a;
    a.latch = 1;
    const double bps = 120.0 / 60.0 / 48000.0;
    EXPECT_TRUE(ofKind(acidBlock(a, 0.0, true, 512, 0), 'N').empty());
    juce::MidiBuffer in;
    in.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    auto pos = makePos(512 * bps, 120.0);
    a.process(in, &pos, 48000.0, 512);
    // armed: the next onset (6000) is produced
    std::vector<AEv> ev;
    for (int b = 2; b < 14; ++b)
    {
        auto e = acidBlock(a, b * 512 * bps, true, 512, b * 512);
        ev.insert(ev.end(), e.begin(), e.end());
    }
    EXPECT_FALSE(ofKind(ev, 'N').empty());
}

// ── "Honoured, not just parsed" (lessons 34/38): every one of the 76 params, written
// through the slot's automation path, must change the emitted buffer. ──

TEST(AcidStep, EveryStepFlagAndNoteChangesTheBuffer)
{
    auto baseSlot = [] {
        return MidiFxSlot(std::make_unique<AcidStep>(), "acid_step");
    };
    auto render = [&](const std::vector<std::pair<int, float>>& writes) {
        auto slot = baseSlot();
        for (const auto& w : writes) setReal(slot, w.first, w.second);
        slot.applyAutomation();
        std::vector<AEv> all;
        for (int b = 0; b < kAcidSlotBlocks; ++b)
        {
            juce::MidiBuffer buf;
            auto pos = makePos(static_cast<double>(b) * 512 * 120.0 / 60.0 / 48000.0, 120.0);
            slot.process(buf, &pos, 48000.0, 512);
            auto e = collectAcid(buf, static_cast<long long>(b) * 512);
            all.insert(all.end(), e.begin(), e.end());
        }
        return all;
    };
    const auto baseline = render({});
    ASSERT_FALSE(baseline.empty());
    EXPECT_EQ(render({}), baseline); // control: unchanged params reproduce the baseline

    for (int n = 1; n <= 16; ++n)
    {
        const int base = kStepNote(n);
        EXPECT_NE(render({{ base + 0, 5.0f }}), baseline) << "step" << n << "Note had no effect";
        EXPECT_NE(render({{ base + 1, 1.0f }}), baseline) << "step" << n << "Accent had no effect";
        EXPECT_NE(render({{ base + 2, 1.0f }}), baseline) << "step" << n << "Slide had no effect";
        EXPECT_NE(render({{ base + 3, 1.0f }}), baseline) << "step" << n << "Rest had no effect";
    }
}

TEST(AcidStep, EveryGlobalParamChangesTheBuffer)
{
    // Scenario in which every global is observable: 4 steps, an accent, a slide, distinct notes.
    const std::vector<std::pair<int, float>> scenario = {
        {1, 4.0f},
        {kStepNote(1) + 0, 0.0f}, {kStepNote(2) + 0, 3.0f}, {kStepNote(3) + 0, 5.0f}, {kStepNote(4) + 0, 7.0f},
        {kStepNote(1) + 1, 1.0f},               // step 1 accent
        {kStepNote(2) + 2, 1.0f},               // step 2 slide
    };
    auto render = [&](std::pair<int, float> extra) {
        MidiFxSlot slot(std::make_unique<AcidStep>(), "acid_step");
        for (const auto& w : scenario) setReal(slot, w.first, w.second);
        if (extra.first >= 0) setReal(slot, extra.first, extra.second);
        slot.applyAutomation();
        std::vector<AEv> all;
        for (int b = 0; b < kAcidSlotBlocks; ++b)
        {
            juce::MidiBuffer buf;
            auto pos = makePos(static_cast<double>(b) * 512 * 120.0 / 60.0 / 48000.0, 120.0);
            // an input note arriving AFTER the first onset (sample 0) so latch=1 is observable:
            // it arms mid-run, so the onset at step 0 is skipped vs the free-running baseline.
            // The note is consumed, never emitted.
            if (b == 2) buf.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
            slot.process(buf, &pos, 48000.0, 512);
            auto e = collectAcid(buf, static_cast<long long>(b) * 512);
            all.insert(all.end(), e.begin(), e.end());
        }
        return all;
    };
    const auto baseline = render({-1, 0.0f});
    ASSERT_FALSE(baseline.empty());
    const std::pair<int, float> variants[] = {
        {0, 0.5f}, {1, 3.0f}, {2, 1.0f}, {3, 0.8f}, {4, 50.0f}, {5, 90.0f}, {6, 40.0f},
        {7, 0.3f}, {8, 1.0f}, {9, 2.0f}, {10, 1.0f}, {11, 48.0f} };
    const char* names[] = { "rate","steps","octave","gate","velocity","accentVelocity","accentAmount",
                            "swing","slideMode","direction","latch","baseNote" };
    for (int i = 0; i < 12; ++i)
        EXPECT_NE(render(variants[i]), baseline) << "global " << names[i] << " had no effect";
}

// ── Registration / dispatch sites ───────────────────────────────────────

TEST(AcidStep, ParamTableIsTheContract)
{
    const auto defs = getMidiFxParamDefs("acid_step");
    ASSERT_EQ(defs.size(), 76u);
    EXPECT_EQ(getMidiFxParamCount("acid_step"), 76);
    std::set<std::string> names;
    for (size_t i = 0; i < defs.size(); ++i)
    {
        EXPECT_EQ(defs[i].index, static_cast<int>(i));
        EXPECT_LE(defs[i].index, 99);
        EXPECT_LT(defs[i].minValue, defs[i].maxValue);
        EXPECT_GE(defs[i].defaultValue, defs[i].minValue);
        EXPECT_LE(defs[i].defaultValue, defs[i].maxValue);
        EXPECT_TRUE(names.insert(defs[i].name).second) << "duplicate name " << defs[i].name;
    }
    const char* globals[] = { "rate","steps","octave","gate","velocity","accentVelocity","accentAmount",
                              "swing","slideMode","direction","latch","baseNote" };
    for (int i = 0; i < 12; ++i) EXPECT_STREQ(defs[static_cast<size_t>(i)].name, globals[i]);
    for (int n = 1; n <= 16; ++n)
    {
        const auto base = static_cast<size_t>(12 + (n - 1) * 4);
        EXPECT_EQ(std::string(defs[base].name),     "step" + std::to_string(n) + "Note");
        EXPECT_EQ(std::string(defs[base + 1].name), "step" + std::to_string(n) + "Accent");
        EXPECT_EQ(std::string(defs[base + 2].name), "step" + std::to_string(n) + "Slide");
        EXPECT_EQ(std::string(defs[base + 3].name), "step" + std::to_string(n) + "Rest");
    }
}

TEST(AcidStep, RebuildMidiFxChainReadsParamsFromTreeByDefName)
{
    HDAW::Track track;
    TestPlayHead playhead;
    track.setPlayHead(&playhead);
    track.prepareToPlay(44100.0, 22050);

    juce::ValueTree chain(IDs::MIDI_FX_CHAIN);
    juce::ValueTree slot(IDs::MIDI_FX_SLOT);
    slot.setProperty(IDs::fxType, "acid_step", nullptr);
    slot.setProperty(IDs::bypassed, false, nullptr);
    slot.setProperty("step2Rest", 1.0, nullptr);
    slot.setProperty("step1Accent", 1.0, nullptr);
    slot.setProperty("step1Note", 12.0, nullptr);
    slot.setProperty("steps", 8.0, nullptr);
    chain.addChild(slot, -1, nullptr);
    track.rebuildMidiFXChain(chain);
    ASSERT_EQ(track.getNumMidiFxSlots(), 1);

    auto* acid = dynamic_cast<AcidStep*>(track.getMidiFxChain()[0]->getEffect());
    ASSERT_NE(acid, nullptr);
    EXPECT_EQ(acid->steps, 8);
    EXPECT_EQ(acid->stepData[0].note, 12);
    EXPECT_FLOAT_EQ(acid->stepData[0].accent, 1.0f);
    EXPECT_FLOAT_EQ(acid->stepData[1].rest, 1.0f);
    EXPECT_FLOAT_EQ(acid->stepData[2].rest, 0.0f);
    EXPECT_EQ(acid->baseNote, 36); // untouched global keeps its default

    // And the live render honours it: 44100/22050 @120 => onsets 0, 5512, (rest), 16537
    juce::AudioBuffer<float> audio(2, 22050);
    juce::MidiBuffer midi;
    track.processBlock(audio, midi);
    const auto ev = collectAcid(midi, 0);
    const auto ons = ofKind(ev, 'N');
    ASSERT_EQ(ons.size(), 3u);
    EXPECT_EQ(ons[0].note, 48);
    EXPECT_EQ(ons[0].val, 118);
}

// G6: a downstream consumer sees the aftertouch event (the chain forwards it).
TEST(AcidStep, AftertouchReachesDownstreamChainConsumers)
{
    // (a) a recording MidiEffect behind the acid slot, run the way Track runs the chain
    struct Recorder : MidiEffect
    {
        std::vector<AEv> seen;
        void process(juce::MidiBuffer& b, const juce::AudioPlayHead::PositionInfo*, double, int) override
        {
            auto e = collectAcid(b, 0);
            seen.insert(seen.end(), e.begin(), e.end());
        }
    };
    auto acid = std::make_unique<AcidStep>();
    acid->stepData[0].accent = 1.0f;
    acid->accentAmount = 99;
    auto rec = std::make_unique<Recorder>();
    auto* recPtr = rec.get();
    MidiFxSlot s0(std::move(acid), "acid_step");
    MidiFxSlot s1(std::move(rec), "recorder");
    juce::MidiBuffer buf;
    auto pos = makePos(0.0, 120.0);
    s0.process(buf, &pos, 48000.0, 512);
    s1.process(buf, &pos, 48000.0, 512);
    const auto at = ofKind(recPtr->seen, 'A');
    ASSERT_EQ(at.size(), 1u);
    EXPECT_EQ(at[0].note, 36);
    EXPECT_EQ(at[0].val, 99);
    EXPECT_EQ(at[0].s, 0);

    // (b) through Track::processBlock with a real downstream MIDI FX (transpose) in the chain:
    // the buffer handed to the instrument slot still carries the aftertouch.
    HDAW::Track track;
    TestPlayHead playhead;
    track.setPlayHead(&playhead);
    track.prepareToPlay(44100.0, 22050);
    juce::ValueTree chain(IDs::MIDI_FX_CHAIN);
    juce::ValueTree a(IDs::MIDI_FX_SLOT);
    a.setProperty(IDs::fxType, "acid_step", nullptr);
    a.setProperty(IDs::bypassed, false, nullptr);
    a.setProperty("step1Accent", 1.0, nullptr);
    a.setProperty("accentAmount", 88.0, nullptr);
    chain.addChild(a, -1, nullptr);
    juce::ValueTree t(IDs::MIDI_FX_SLOT);
    t.setProperty(IDs::fxType, "transpose", nullptr);
    t.setProperty(IDs::semitones, 2, nullptr);
    t.setProperty(IDs::bypassed, false, nullptr);
    chain.addChild(t, -1, nullptr);
    track.rebuildMidiFXChain(chain);
    ASSERT_EQ(track.getNumMidiFxSlots(), 2);
    juce::AudioBuffer<float> audio(2, 22050);
    juce::MidiBuffer midi;
    track.processBlock(audio, midi);
    const auto ev = collectAcid(midi, 0);
    const auto ats = ofKind(ev, 'A');
    ASSERT_EQ(ats.size(), 1u);
    EXPECT_EQ(ats[0].val, 88);
    EXPECT_EQ(ofKind(ev, 'N')[0].note, 38); // the downstream transpose really ran
}

// G5: fxType "acid_step" through the real command path produces a slot with 76
// automatable entries, pids 1000 + slot*100 + index, all inside the slot's 100-wide window.
TEST(AcidStep, AddMidiFxSlotCommandProducesSeventySixAutomatableParams)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(cmds.addTrack("Track 0"), 0);
    engine.drainPendingRoutingRebuild();
    cmds.addMidiFxSlot(0, "acid_step", -1);
    cmds.addMidiFxSlot(0, "acid_step", -1);
    engine.drainPendingRoutingRebuild();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->getMidiFxChain().size(), 2u);
    for (int si = 0; si < 2; ++si)
    {
        ASSERT_NE(dynamic_cast<AcidStep*>(track->getMidiFxChain()[static_cast<size_t>(si)]->getEffect()), nullptr);
        EXPECT_EQ(track->getMidiFxChain()[static_cast<size_t>(si)]->getAutomatableParams().size(), 76u);
    }

    const auto params = engine.getReadModel().getAutomatableParams(0);
    int perSlot[2] = { 0, 0 };
    for (const auto& p : params)
    {
        if (p.name.rfind("acid_step.", 0) != 0) continue;
        ASSERT_TRUE(p.slotIndex == 0 || p.slotIndex == 1);
        const int idx = p.paramIndex - (1000 + p.slotIndex * 100);
        EXPECT_GE(idx, 0);
        EXPECT_LE(idx, 75);
        EXPECT_LT(p.paramIndex, 1000 + (p.slotIndex + 1) * 100);
        if (p.slotIndex == 0) EXPECT_LT(p.paramIndex, 1100);
        EXPECT_TRUE(p.automatable);
        ++perSlot[p.slotIndex];
    }
    EXPECT_EQ(perSlot[0], 76);
    EXPECT_EQ(perSlot[1], 76);

    // list_midi_fx_params is the ReadModel snapshot + the def table: 76 params, defaults written.
    const auto slotSnaps = engine.getReadModel().getMidiFxSlots(0);
    ASSERT_EQ(slotSnaps.size(), 2u);
    EXPECT_EQ(slotSnaps[0].fxType, "acid_step");
    EXPECT_EQ(slotSnaps[0].params.size(), 76u);
    const auto defs = getMidiFxParamDefs("acid_step");
    for (const auto& d : defs)
    {
        const auto it = slotSnaps[0].params.find(d.name);
        ASSERT_NE(it, slotSnaps[0].params.end()) << "param " << d.name << " not written by addMidiFxSlot";
        EXPECT_NEAR(std::get<double>(it->second), static_cast<double>(d.defaultValue), 1e-6) << d.name;
    }

    // Observable effect of setMidiFxSlotParam on the LIVE effect (not the tree).
    const auto wrRest = cmds.setMidiFxSlotParam(0, 0, "step3Rest", 1.0);
    ASSERT_TRUE(wrRest.ok) << wrRest.error;
    const auto wrBase = cmds.setMidiFxSlotParam(0, 0, "baseNote", 48.0);
    ASSERT_TRUE(wrBase.ok) << wrBase.error;
    auto* live = track->getMidiFxChain()[0].get();
    live->applyAutomation();
    auto* acid = dynamic_cast<AcidStep*>(live->getEffect());
    ASSERT_NE(acid, nullptr);
    EXPECT_FLOAT_EQ(acid->stepData[2].rest, 1.0f);
    EXPECT_EQ(acid->baseNote, 48);
}

// ── MIDI-FX param contract (2026-10-04) ──────────────────────────────────────
// Defect class fixed here: a param write that is ACCEPTED but has no effect.
//  (a) an unknown fxType created a slot whose factory produced no effect;
//  (b) an unknown param name wrote a tree key nothing ever reads;
//  (c) six legacy types stored prefixed ids (arpRate, velFactor, scaleRoot,
//      lengthFactor, keyFilterRoot/Scale, delayFeedback/Mix), so even a correct
//      def-name write was ignored by the loader/factory and reverted on load.

TEST(MidiFxParamContract, UnknownTypeIsRefusedAndCreatesNoSlot)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(cmds.addTrack("T"), 0);
    engine.drainPendingRoutingRebuild();

    const auto r = cmds.addMidiFxSlot(0, "acid_stepp", -1);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("unknown midi fx type"), std::string::npos);
    EXPECT_NE(r.error.find("acid_step"), std::string::npos) << "the refusal names the valid set";

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->getNumMidiFxSlots(), 0) << "a refused add must not create a slot";
}

TEST(MidiFxParamContract, UnknownParamNameIsRefusedAndWritesNothing)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(cmds.addTrack("T"), 0);
    engine.drainPendingRoutingRebuild();
    ASSERT_TRUE(cmds.addMidiFxSlot(0, "arpeggiator", -1).ok);

    auto slotTree = engine.getProjectModel().getTrackListTree().getChild(0)
                        .getChildWithName(IDs::MIDI_FX_CHAIN).getChild(0);
    ASSERT_TRUE(slotTree.isValid());

    const auto r = cmds.setMidiFxSlotParam(0, 0, "rat", 0.5);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("unknown param"), std::string::npos);
    EXPECT_NE(r.error.find("rate"), std::string::npos) << "lists the available names";
    EXPECT_FALSE(slotTree.hasProperty("rat")) << "nothing may be written for a refused name";
}

TEST(MidiFxParamContract, EveryDefNameMapsToAStoredKey)
{
    const char* types[] = { "arpeggiator", "velocity", "chord", "scale", "notelength",
                            "transpose", "keyfilter", "velocitycurve", "notechance",
                            "mididelay", "humanize", "strum", "acid_step" };
    for (const char* t : types)
    {
        const juce::String type(t);
        ASSERT_TRUE(isKnownMidiFxType(type)) << t;
        const auto defs = getMidiFxParamDefs(type);
        EXPECT_FALSE(defs.empty()) << t;
        for (const auto& d : defs)
            EXPECT_FALSE(midiFxTreeKeyForParam(type, d.name).isNull()) << t << "." << d.name;
    }
    EXPECT_TRUE(isKnownMidiFxType("multinote"));
    EXPECT_TRUE(midiFxTreeKeyForParam("multinote", "anything").isNull());

    // The legacy types must still resolve to the id their loader reads.
    EXPECT_EQ(midiFxTreeKeyForParam("arpeggiator", "rate").toString(), juce::String("arpRate"));
    EXPECT_EQ(midiFxTreeKeyForParam("velocity", "factor").toString(), juce::String("velFactor"));
    EXPECT_EQ(midiFxTreeKeyForParam("scale", "root").toString(), juce::String("scaleRoot"));
    EXPECT_EQ(midiFxTreeKeyForParam("notelength", "factor").toString(), juce::String("lengthFactor"));
    EXPECT_EQ(midiFxTreeKeyForParam("keyfilter", "root").toString(), juce::String("keyFilterRoot"));
    EXPECT_EQ(midiFxTreeKeyForParam("keyfilter", "scaleType").toString(), juce::String("keyFilterScale"));
    EXPECT_EQ(midiFxTreeKeyForParam("mididelay", "feedback").toString(), juce::String("delayFeedback"));
    EXPECT_EQ(midiFxTreeKeyForParam("mididelay", "mix").toString(), juce::String("delayMix"));
    EXPECT_EQ(midiFxTreeKeyForParam("transpose", "semitones").toString(), juce::String("semitones"));
}

TEST(MidiFxParamContract, DefNameWriteSurvivesARebuildFromTheTree)
{
    // The end-to-end durability proof for the legacy types: write by DEF NAME,
    // rebuild the live chain from the tree (what save/load does), and assert the
    // effect actually sees it. Before the fix this reverted to the default.
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    ASSERT_GE(cmds.addTrack("T"), 0);
    engine.drainPendingRoutingRebuild();
    ASSERT_TRUE(cmds.addMidiFxSlot(0, "arpeggiator", -1).ok);

    const auto wrRate = cmds.setMidiFxSlotParam(0, 0, "rate", 0.75);
    ASSERT_TRUE(wrRate.ok) << wrRate.error;
    const auto wrVel = cmds.setMidiFxSlotParam(0, 0, "velocity", 64);
    ASSERT_TRUE(wrVel.ok) << wrVel.error;

    auto chainTree = engine.getProjectModel().getTrackListTree().getChild(0)
                         .getChildWithName(IDs::MIDI_FX_CHAIN);
    ASSERT_TRUE(chainTree.isValid());
    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);

    track->rebuildMidiFXChain(chainTree);   // == the save/load path
    ASSERT_EQ(track->getNumMidiFxSlots(), 1);
    auto* arp = dynamic_cast<Arpeggiator*>(track->getMidiFxChain()[0]->getEffect());
    ASSERT_NE(arp, nullptr);
    EXPECT_DOUBLE_EQ(arp->rate, 0.75) << "def-name write must survive a rebuild";
    EXPECT_EQ(arp->velocity, 64) << "arp velocity was not read back by the factory at all";
}
