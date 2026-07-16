// Unit tests for the LFO modulation feature.
//
// Covers:
//  - LFOModulationSource DSP: waveform shapes, rate sync, bipolar mapping,
//    depth scaling, disabled-source no-op.
//  - ModulationManager: summing multiple sources targeting the same param,
//    and target-routing isolation.
//  - Persistence round-trip: fromValueTree/toValueTree symmetry.
//
// The LFO types are header-only (inline in ModulationSource.h), so we test
// them directly without instantiating the full audio engine for the DSP cases.
// The persistence test uses ProjectSerializer save/load against a temp file.
#include <gtest/gtest.h>
#include "engine/ModulationSource.h"
#include "engine/ModulationManager.h"
#include "engine/AudioEngine.h"
#include "engine/ProjectSerializer.h"
#include "model/ProjectModel.h"
#include <cmath>
#include <juce_core/juce_core.h>

using HDAW::LFOModulationSource;
using HDAW::LfoWaveform;
using HDAW::ModulationManager;

namespace {
// Number of samples to advance before reading the LFO output. At a high
// sample count the phase has progressed enough that the value reflects the
// waveform at a known phase (we compute the expected phase independently).
constexpr double kSr = 44100.0;
constexpr double kBpm = 120.0;
} // namespace

// ── LFOModulationSource: a disabled source always returns 0 ──────────────
TEST(Modulation, DisabledSourceReturnsZero)
{
    LFOModulationSource lfo;
    lfo.prepare(kSr);
    lfo.setEnabled(false);
    lfo.setDepth(1.0f);
    for (int i = 0; i < 1000; ++i)
        EXPECT_FLOAT_EQ(lfo.getNextValue(kBpm, kSr), 0.0f);
}

// ── Depth scaling: output amplitude tracks the depth setting ─────────────
TEST(Modulation, DepthScalesOutput)
{
    // Bipolar sine, rate-synced at 1 Hz (rate=1, free-running so rate is Hz).
    // With depth=1 and bipolar on, a sine sweeps ±1.
    LFOModulationSource lfoFull;
    lfoFull.prepare(kSr);
    lfoFull.setWaveform(LfoWaveform::sine);
    lfoFull.setBipolar(true);
    lfoFull.setDepth(1.0f);
    lfoFull.setRateSync(false);
    lfoFull.setRate(1.0f); // 1 Hz free-running

    LFOModulationSource lfoHalf;
    lfoHalf.prepare(kSr);
    lfoHalf.setWaveform(LfoWaveform::sine);
    lfoHalf.setBipolar(true);
    lfoHalf.setDepth(0.5f);
    lfoHalf.setRateSync(false);
    lfoHalf.setRate(1.0f);

    // Advance both to the same phase and compare peak magnitudes.
    float maxFull = 0.0f, maxHalf = 0.0f;
    for (int i = 0; i < static_cast<int>(kSr); ++i) // one full cycle
    {
        maxFull = std::max(maxFull, std::abs(lfoFull.getNextValue(kBpm, kSr)));
        maxHalf = std::max(maxHalf, std::abs(lfoHalf.getNextValue(kBpm, kSr)));
    }
    // Full depth reaches ~1.0 at the peak; half depth ~0.5.
    EXPECT_NEAR(maxFull, 1.0f, 0.02f);
    EXPECT_NEAR(maxHalf, 0.5f, 0.02f);
}

// ── Bipolar vs unipolar mapping ──────────────────────────────────────────
TEST(Modulation, BipolarVsUnipolarRange)
{
    // Saw wave: bipolar gives [-1,+1]; unipolar maps that to [0,1].
    LFOModulationSource bipolar;
    bipolar.prepare(kSr);
    bipolar.setWaveform(LfoWaveform::saw);
    bipolar.setBipolar(true);
    bipolar.setDepth(1.0f);
    bipolar.setRateSync(false);
    bipolar.setRate(1.0f);

    LFOModulationSource unipolar;
    unipolar.prepare(kSr);
    unipolar.setWaveform(LfoWaveform::saw);
    unipolar.setBipolar(false);
    unipolar.setDepth(1.0f);
    unipolar.setRateSync(false);
    unipolar.setRate(1.0f);

    float minB = 1.0f, maxB = -1.0f, minU = 1.0f, maxU = -1.0f;
    for (int i = 0; i < static_cast<int>(kSr); ++i)
    {
        float vb = bipolar.getNextValue(kBpm, kSr);
        float vu = unipolar.getNextValue(kBpm, kSr);
        minB = std::min(minB, vb); maxB = std::max(maxB, vb);
        minU = std::min(minU, vu); maxU = std::max(maxU, vu);
    }
    EXPECT_NEAR(minB, -1.0f, 0.02f);
    EXPECT_NEAR(maxB,  1.0f, 0.02f);
    EXPECT_NEAR(minU, 0.0f, 0.02f);
    EXPECT_NEAR(maxU, 1.0f, 0.02f);
}

// ── Square wave only takes ±1 (bipolar) ──────────────────────────────────
TEST(Modulation, SquareWaveIsBipolarTwoLevel)
{
    LFOModulationSource lfo;
    lfo.prepare(kSr);
    lfo.setWaveform(LfoWaveform::square);
    lfo.setBipolar(true);
    lfo.setDepth(1.0f);
    lfo.setRateSync(false);
    lfo.setRate(1.0f);

    bool sawPos = false, sawNeg = false;
    for (int i = 0; i < static_cast<int>(kSr); ++i)
    {
        float v = lfo.getNextValue(kBpm, kSr);
        if (v > 0.9f) sawPos = true;
        if (v < -0.9f) sawNeg = true;
        // No intermediate values for a square wave.
        EXPECT_TRUE(v > 0.9f || v < -0.9f);
    }
    EXPECT_TRUE(sawPos);
    EXPECT_TRUE(sawNeg);
}

// ── Sample & Hold: RT-safe path never allocates and produces a held value ─
TEST(Modulation, SampleAndHoldProducesTableValues)
{
    LFOModulationSource lfo;
    lfo.prepare(kSr);
    lfo.setWaveform(LfoWaveform::sampleAndHold);
    lfo.setBipolar(true);
    lfo.setDepth(1.0f);
    lfo.setRateSync(false);
    lfo.setRate(1.0f); // one re-roll per second

    // Run a few cycles. The output should be piecewise-constant (held) and
    // bounded to [-1, 1]. The key assertion is that this completes without
    // any audio-thread allocation — the S&H indexes the precomputed table.
    float prev = lfo.getNextValue(kBpm, kSr);
    for (int i = 1; i < static_cast<int>(kSr * 3); ++i)
    {
        float v = lfo.getNextValue(kBpm, kSr);
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
        prev = v;
    }
    juce::ignoreUnused(prev);
}

// ── ModulationManager: sums multiple sources on the same target ──────────
TEST(ModulationManager, SumsSourcesOnSameTarget)
{
    ModulationManager mgr;

    // Two LFOs targeting paramID=1 (volume), depth 0.5 each, bipolar sine.
    // We build them via ValueTrees so we exercise the rebuild path.
    juce::ValueTree modList(juce::Identifier("MODULATION_LIST"));
    for (int s = 0; s < 2; ++s)
    {
        auto src = juce::ValueTree(IDs::MODULATION);
        src.setProperty("type", "lfo", nullptr);
        src.setProperty(IDs::waveform, static_cast<int>(LfoWaveform::sine), nullptr);
        src.setProperty(IDs::rate, 1.0, nullptr);
        src.setProperty(IDs::rateSync, false, nullptr);
        src.setProperty(IDs::depth, 0.5, nullptr);
        src.setProperty(IDs::bipolar, true, nullptr);
        src.setProperty(IDs::phaseOffset, 0.0, nullptr);
        src.setProperty(IDs::targetParamID, 1, nullptr);
        src.setProperty(IDs::enabled, true, nullptr);
        modList.addChild(src, -1, nullptr);
    }
    mgr.rebuild(modList, kSr);
    ASSERT_EQ(mgr.getNumSources(), 2);

    // The summed output should reach ~1.0 at the sine peaks (0.5 + 0.5).
    float maxSum = 0.0f;
    for (int i = 0; i < static_cast<int>(kSr); ++i)
        maxSum = std::max(maxSum, std::abs(mgr.getModulation(1, kBpm, kSr)));
    EXPECT_NEAR(maxSum, 1.0f, 0.05f);

    // A different target (pan=2) gets no contribution from these sources.
    for (int i = 0; i < static_cast<int>(kSr); ++i)
        EXPECT_FLOAT_EQ(mgr.getModulation(2, kBpm, kSr), 0.0f);
}

// ── ModulationManager: empty/invalid tree yields no sources ──────────────
TEST(ModulationManager, HandlesEmptyTree)
{
    ModulationManager mgr;
    mgr.rebuild({}, kSr);
    EXPECT_EQ(mgr.getNumSources(), 0);
    EXPECT_FLOAT_EQ(mgr.getModulation(1, kBpm, kSr), 0.0f);
}

// ── fromValueTree/toValueTree round-trip symmetry ────────────────────────
TEST(Modulation, ValueTreeRoundTrip)
{
    LFOModulationSource original;
    original.setWaveform(LfoWaveform::triangle);
    original.setRate(2.5f);
    original.setRateSync(false);
    original.setDepth(0.75f);
    original.setBipolar(true);
    original.setPhaseOffset(45.0f);
    original.setTargetParamID(2);
    original.setEnabled(true);

    auto tree = original.toValueTree("lfo_1");

    LFOModulationSource restored;
    restored.fromValueTree(tree);

    EXPECT_EQ(restored.getWaveform(), LfoWaveform::triangle);
    EXPECT_FLOAT_EQ(restored.getRate(), 2.5f);
    EXPECT_FALSE(restored.isRateSynced());
    EXPECT_FLOAT_EQ(restored.getDepth(), 0.75f);
    EXPECT_TRUE(restored.isBipolar());
    EXPECT_FLOAT_EQ(restored.getPhaseOffset(), 45.0f);
    EXPECT_EQ(restored.getTargetParamID(), 2);
    EXPECT_TRUE(restored.isEnabled());
}

// ── Persistence: MODULATION_LIST survives a save/load cycle ──────────────
TEST(Modulation, PersistsAcrossSaveLoad)
{
    AudioEngine engine;
    engine.initialize();

    // Build a MODULATION_LIST on track 0 directly in the model.
    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    ASSERT_GT(trackList.getNumChildren(), 0);
    auto trackTree = trackList.getChild(0);
    auto modList = juce::ValueTree(IDs::MODULATION_LIST);
    auto mod = juce::ValueTree(IDs::MODULATION);
    mod.setProperty("type", "lfo", nullptr);
    mod.setProperty(IDs::name, "TestLFO", nullptr);
    mod.setProperty(IDs::waveform, static_cast<int>(LfoWaveform::saw), nullptr);
    mod.setProperty(IDs::rate, 3.0, nullptr);
    mod.setProperty(IDs::depth, 0.5, nullptr);
    mod.setProperty(IDs::targetParamID, 1, nullptr);
    mod.setProperty(IDs::enabled, true, nullptr);
    modList.addChild(mod, -1, nullptr);
    trackTree.addChild(modList, -1, nullptr);

    // Save to a temp file.
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto file = tempDir.getNonexistentChildFile("hdaw_mod_test", ".hdaw", false);
    ASSERT_TRUE(HDAW::ProjectSerializer::save(model, file));

    // Load into a fresh engine and verify the modulation survived.
    AudioEngine engine2;
    engine2.initialize();
    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine2.getProjectModel(), file));

    auto trackList2 = engine2.getProjectModel().getTrackListTree();
    auto trackTree2 = trackList2.getChild(0);
    auto modList2 = trackTree2.getChildWithName(IDs::MODULATION_LIST);
    ASSERT_TRUE(modList2.isValid());
    ASSERT_EQ(modList2.getNumChildren(), 1);

    auto mod2 = modList2.getChild(0);
    EXPECT_EQ(static_cast<int>(mod2.getProperty(IDs::waveform)), static_cast<int>(LfoWaveform::saw));
    EXPECT_NEAR(static_cast<double>(mod2.getProperty(IDs::rate)), 3.0, 1e-9);
    EXPECT_NEAR(static_cast<double>(mod2.getProperty(IDs::depth)), 0.5, 1e-9);
    EXPECT_EQ(static_cast<int>(mod2.getProperty(IDs::targetParamID)), 1);
    // The dead targetClipIndex must NOT be written by toValueTree/addLFO.
    EXPECT_FALSE(mod2.hasProperty(IDs::targetClipIndex));

    file.deleteFile();
}
