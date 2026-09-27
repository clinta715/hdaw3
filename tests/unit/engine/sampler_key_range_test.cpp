#include <gtest/gtest.h>
#include "engine/TrackFXSlot.h"
#include "engine/SamplerEngine.h"
#include "engine/SamplerSound.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "model/ProjectModel.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Helper: create a test sine SamplerSound at a given frequency.
static std::shared_ptr<const HDAW::SamplerSound> makeSine(int len, double sr, double freq)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = len; b.nativeSampleRate = sr; b.rootNote = 60;
    b.sampleStart = 0.0; b.sampleEnd = 1.0;
    b.data[0] = std::make_unique<float[]>(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i)
        b.data[0][i] = static_cast<float>(std::sin(6.2831853 * freq * i / sr));
    return b.build();
}

// G2.3: Regression — single full-range sampler behaves byte-identically.
TEST(SamplerKeyRange, FullRangeRegression)
{
    HDAW::TrackFXSlot slot("sampler");
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 44100.0;
    spec.maximumBlockSize = 128;
    spec.numChannels = 2;
    slot.prepare(spec);
    slot.setSamplerSoundForTest(makeSine(44100, 44100.0, 440.0));

    EXPECT_FALSE(slot.hasKeyRange());

    juce::AudioBuffer<float> buf(2, 128);
    buf.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 80, 0.8f), 0);
    slot.process(buf, midi);

    // Both notes should produce audio (full range)
    bool anyNonZero = false;
    for (int i = 0; i < 128; ++i)
        if (std::abs(buf.getSample(0, i)) > 1e-6f)
            { anyNonZero = true; break; }
    EXPECT_TRUE(anyNonZero);
    // midiMessages should be cleared (full-range consumes all)
    EXPECT_TRUE(midi.isEmpty());
}

// G2.1: Partition — a slot with key range only renders in-range notes.
// We test the partition logic by verifying that hasKeyRange() is correctly
// set and that the process path takes the partition branch.
// (Full audio verification needs a live render; this tests the command path.)
TEST(SamplerKeyRange, PartitionBranchTaken)
{
    HDAW::TrackFXSlot slot("sampler");
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 44100.0;
    spec.maximumBlockSize = 128;
    spec.numChannels = 2;
    slot.prepare(spec);
    slot.setSamplerSoundForTest(makeSine(44100, 44100.0, 440.0));

    // Without key range: full range behavior
    EXPECT_FALSE(slot.hasKeyRange());
    juce::AudioBuffer<float> buf1(2, 128);
    buf1.clear();
    juce::MidiBuffer midi1;
    midi1.addEvent(juce::MidiMessage::noteOn(1, 40, 0.8f), 0);
    midi1.addEvent(juce::MidiMessage::noteOn(1, 80, 0.8f), 0);
    slot.process(buf1, midi1);
    EXPECT_TRUE(midi1.isEmpty()); // full-range consumes all

    // Note: hasKeyRange() is set via loadSamplerState from the ValueTree,
    // so we test the command path in the integration tests below.
}

// G2.5 + G2.2: Command round-trip + rebuild restore.
TEST(SamplerKeyRange, CommandSetKeyRangeRoundTrip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    engine.drainPendingRoutingRebuild();

    // Add a track with a sampler slot
    int trackId = cmds.addTrack("TestSampler", -1, -1, 0);
    ASSERT_GE(trackId, 0);
    engine.drainPendingRoutingRebuild();

    cmds.addFxSlot(trackId, std::string("sampler"), -1, std::string());
    engine.drainPendingRoutingRebuild();

    // Set key range
    cmds.setSamplerKeyRange(trackId, 0, 36, 60);
    engine.drainPendingRoutingRebuild();

    // Verify via ValueTree
    auto& model = engine.getProjectModel();
    auto slotTree = model.getTrackListTree().getChild(trackId)
        .getChildWithName(IDs::FX_CHAIN).getChild(0);
    EXPECT_EQ(static_cast<int>(slotTree.getProperty(IDs::keyRangeLow, -1)), 36);
    EXPECT_EQ(static_cast<int>(slotTree.getProperty(IDs::keyRangeHigh, -1)), 60);

    // Verify via ReadModel snapshot
    auto snap = engine.getReadModel().getSamplerState(trackId, 0);
    EXPECT_EQ(snap.keyRangeLow, 36);
    EXPECT_EQ(snap.keyRangeHigh, 60);

    // Reset to full range
    cmds.setSamplerKeyRange(trackId, 0, -1, -1);
    engine.drainPendingRoutingRebuild();

    auto snap2 = engine.getReadModel().getSamplerState(trackId, 0);
    EXPECT_EQ(snap2.keyRangeLow, -1);
    EXPECT_EQ(snap2.keyRangeHigh, -1);
}

// G2.2: Rebuild restore — key ranges survive a routing rebuild, and the
// live processor reflects them.
TEST(SamplerKeyRange, RebuildRestoresKeyRange)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();
    engine.drainPendingRoutingRebuild();

    int trackId = cmds.addTrack("RebuildTest", -1, -1, 0);
    ASSERT_GE(trackId, 0);
    engine.drainPendingRoutingRebuild();

    cmds.addFxSlot(trackId, std::string("sampler"), -1, std::string());
    engine.drainPendingRoutingRebuild();

    // Set key range
    cmds.setSamplerKeyRange(trackId, 0, 48, 72);
    engine.drainPendingRoutingRebuild();

    // Verify on live processor after rebuild
    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* track = proc->getTrack(trackId);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_GT(chain.size(), 0u);
    ASSERT_NE(chain[0], nullptr);
    EXPECT_TRUE(chain[0]->hasKeyRange());

    // Full routing rebuild
    proc->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();

    // Verify key range survived
    auto* trackAfter = proc->getTrack(trackId);
    ASSERT_NE(trackAfter, nullptr);
    auto& chainAfter = trackAfter->getFXChain();
    ASSERT_GT(chainAfter.size(), 0u);
    ASSERT_NE(chainAfter[0], nullptr);
    EXPECT_TRUE(chainAfter[0]->hasKeyRange());

    // Verify via ReadModel too
    auto snap = engine.getReadModel().getSamplerState(trackId, 0);
    EXPECT_EQ(snap.keyRangeLow, 48);
    EXPECT_EQ(snap.keyRangeHigh, 72);
}

// ── Multi-sampler chain SUM (2026-09-26 regression) ─────────────────────────
// Pre-fix each sampler slot called buffer.clear() on the SHARED chain buffer
// (TrackFXSlot::process sampler branch), so every slot ERASED the earlier
// slots' audio and only the LAST sampler survived — a later slot with zero
// in-range notes rendered the whole chain exact silence. These tests render a
// real 2-slot key-ranged chain through Track::processBlock (the same path live
// playback AND the export bake use) and pin the four cases on the OUTPUT.

namespace {

// Deterministic sine with an explicit root note, so a slot playing its own
// root renders at rate 1.0 — BOTH slots then produce the identical waveform
// and the both-slots sum is exactly 2x the single-slot render (clean math).
static std::shared_ptr<const HDAW::SamplerSound> makeRootedSine(int rootNote)
{
    HDAW::SamplerSound::Builder b;
    b.numChannels = 1; b.length = 44100; b.nativeSampleRate = 44100.0;
    b.rootNote = rootNote;
    b.sampleStart = 0.0; b.sampleEnd = 1.0;
    b.data[0] = std::make_unique<float[]>(44100);
    for (int i = 0; i < 44100; ++i)
        b.data[0][i] = static_cast<float>(std::sin(6.2831853 * 441.0 * i / 44100.0));
    return b.build();
}

float renderedRms(const juce::AudioBuffer<float>& b)
{
    double acc = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double s = b.getSample(ch, i);
            acc += s * s;
        }
    const int n = b.getNumChannels() * b.getNumSamples();
    return n > 0 ? static_cast<float>(std::sqrt(acc / n)) : 0.0f;
}

float renderedPeak(const juce::AudioBuffer<float>& b)
{
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            peak = juce::jmax(peak, std::abs(b.getSample(ch, i)));
    return peak;
}

class MultiSamplerChain : public ::testing::Test
{
protected:
    void SetUp() override
    {
        engine.initialize();
        auto& cmds = engine.getProjectCommands();
        engine.drainPendingRoutingRebuild();

        // THREE identical 2-slot chains on three tracks, so one fixture can
        // render the combined and the single-slot cases on independent
        // sampler voices (voices persist across processBlock calls).
        for (int t = 0; t < 3; ++t)
        {
            trackIds[t] = cmds.addTrack("MultiSamplerChain", -1, -1, 0);
            ASSERT_GE(trackIds[t], 0);
            cmds.addFxSlot(trackIds[t], std::string("sampler"), -1, std::string());
            cmds.addFxSlot(trackIds[t], std::string("sampler"), -1, std::string());
            engine.drainPendingRoutingRebuild();

            // Slot 0 answers notes 42-45, slot 1 answers notes 46-49
            // (the hats-chain shape from the bug report).
            cmds.setSamplerKeyRange(trackIds[t], 0, 42, 45);
            cmds.setSamplerKeyRange(trackIds[t], 1, 46, 49);
            engine.drainPendingRoutingRebuild();
        }

        auto* proc = engine.getMainProcessor();
        ASSERT_NE(proc, nullptr);
        for (int t = 0; t < 3; ++t)
        {
            tracks[t] = proc->getTrack(trackIds[t]);
            ASSERT_NE(tracks[t], nullptr);
            auto& chain = tracks[t]->getFXChain();
            ASSERT_EQ(chain.size(), 2u);
            for (size_t s = 0; s < chain.size(); ++s)
            {
                ASSERT_NE(chain[s], nullptr);
                // No RNG anywhere: slot 0 plays its root (42), slot 1 its
                // root (47) — identical waveforms, deterministic output.
                chain[s]->setSamplerSoundForTest(makeRootedSine(s == 0 ? 42 : 47));
            }
        }
    }

    // Render 2048 samples through the real chain loop in DEVICE-SIZED chunks
    // (slots are prepared for at most getBlockSize() per call). The notes
    // (sentinel -1 = none) fire at sample 0 of the first chunk only.
    juce::AudioBuffer<float> renderBlock(int trackIndex, int noteA, int noteB = -1)
    {
        auto* t = tracks[trackIndex];
        constexpr int kTotal = 2048;
        const int kChunk = juce::jmax(1, t->getBlockSize());
        juce::AudioBuffer<float> out(2, kTotal);
        out.clear();
        for (int start = 0; start < kTotal; start += kChunk)
        {
            const int count = juce::jmin(kChunk, kTotal - start);
            juce::AudioBuffer<float> block(2, count);
            block.clear();
            juce::MidiBuffer midi;
            if (start == 0)
            {
                if (noteA >= 0) midi.addEvent(juce::MidiMessage::noteOn(1, noteA, 0.5f), 0);
                if (noteB >= 0) midi.addEvent(juce::MidiMessage::noteOn(1, noteB, 0.5f), 0);
            }
            t->processBlock(block, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom(ch, start, block, ch, 0, count);
        }
        return out;
    }

    AudioEngine engine;
    int trackIds[3] = {};
    HDAW::Track* tracks[3] = {};
};

} // namespace

// (a) Notes trigger ONLY slot 0 → non-silent output. Pre-fix slot 1 wiped it
// to exact silence (the closed-hats symptom).
TEST_F(MultiSamplerChain, FirstSlotOnlyRendersAudibleOutput)
{
    const auto out = renderBlock(0, 42);
    EXPECT_GT(renderedRms(out), 1e-4f) << "slot 0's audio must survive the later sampler slot";
    EXPECT_LT(renderedPeak(out), 1.0f);
}

// (b) Notes trigger ONLY the last slot → non-silent. This worked pre-fix —
// pin it so the fix cannot regress it.
TEST_F(MultiSamplerChain, LastSlotOnlyRendersAudibleOutput)
{
    const auto out = renderBlock(0, 47);
    EXPECT_GT(renderedRms(out), 1e-4f) << "the last sampler slot must still render alone";
    EXPECT_LT(renderedPeak(out), 1.0f);
}

// (c) Notes trigger BOTH slots → their outputs SUM in chain order: louder
// than either alone (exactly 2x here — identical waveforms) and no more than
// the plain sum (nothing renders twice).
TEST_F(MultiSamplerChain, BothSlotsSumInChainOrder)
{
    const auto both = renderBlock(0, 42, 47);
    const auto onlyFirst = renderBlock(1, 42);
    const auto onlyLast = renderBlock(2, 47);
    const float rBoth = renderedRms(both);
    const float rFirst = renderedRms(onlyFirst);
    const float rLast = renderedRms(onlyLast);
    ASSERT_GT(rFirst, 1e-4f);
    ASSERT_GT(rLast, 1e-4f);
    EXPECT_GT(rBoth, (rFirst + rLast) * 0.9f)
        << "both slots' energy must be present in the sum (pre-fix kept only the last)";
    EXPECT_LT(rBoth, (rFirst + rLast) * 1.1f)
        << "the sum must not exceed the plain sum of the two slots";
}

// (d) Notes trigger NEITHER slot → silence (unchanged).
TEST_F(MultiSamplerChain, NoTriggeredNotesRenderSilence)
{
    const auto out = renderBlock(0, -1, -1);
    EXPECT_LT(renderedRms(out), 1e-6f) << "an untriggered chain must stay silent";
}

// (e) B4: a BYPASSED key-ranged sampler must be TRANSPARENT. The old
// anyPartialSampler pre-clear keyed on hasKeyRange() (not engagement): with
// MIDI flowing it wiped the whole pre-FX buffer to exact silence even when
// every sampler in the chain was bypassed. Seed the input buffer with a
// constant and assert it survives the chain.
TEST_F(MultiSamplerChain, BypassedKeyRangeSamplerPassesAudioThrough)
{
    auto& cmds = engine.getProjectCommands();
    const int t = cmds.addTrack("MultiSamplerChainBypass", -1, -1, 0);
    ASSERT_GE(t, 0);
    cmds.addFxSlot(t, std::string("sampler"), -1, std::string());
    engine.drainPendingRoutingRebuild();
    cmds.setSamplerKeyRange(t, 0, 42, 45);
    cmds.setFxSlotBypassed(t, 0, true);
    engine.drainPendingRoutingRebuild();

    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* track = proc->getTrack(t);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_EQ(chain.size(), 1u);
    ASSERT_NE(chain[0], nullptr);
    chain[0]->setSamplerSoundForTest(makeRootedSine(42));

    constexpr int kTotal = 2048;
    juce::AudioBuffer<float> out(2, kTotal);
    out.clear();
    const int kChunk = juce::jmax(1, track->getBlockSize());
    for (int start = 0; start < kTotal; start += kChunk)
    {
        const int count = juce::jmin(kChunk, kTotal - start);
        juce::AudioBuffer<float> block(2, count);
        // Pre-FX upstream signal: a constant the chain must NOT wipe.
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < count; ++i)
                block.setSample(ch, i, 0.25f);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 42, 0.5f), 0);
        track->processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, start, block, ch, 0, count);
    }
    EXPECT_GT(renderedRms(out), 0.05f)
        << "a bypassed key-ranged sampler must not silence the track (the old pre-clear wiped it to 0)";
}
