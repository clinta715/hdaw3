#include <gtest/gtest.h>
#include "engine/PsyFmOperator.h"
#include "engine/PsyFmModMatrix.h"
#include "engine/PsyFmPatches.h"
#include "engine/PsyFmEngine.h"
#include "engine/PsyFmAlgorithms.h"
#include "engine/PsyFmState.h"
#include "engine/TrackFXSlot.h"
#include <cmath>
#include <thread>
#include <atomic>

using namespace HDAW;

// ── PsyFmOperator tests ──

class PsyFmOperatorTest : public ::testing::Test {
protected:
    void SetUp() override { op.prepare(44100.0); }
    PsyFmOperator op;
};

TEST_F(PsyFmOperatorTest, InitiallyInactive) {
    EXPECT_FALSE(op.isActive());
}

TEST_F(PsyFmOperatorTest, NoteOnActivates) {
    op.setEnvelopeParams({ 0.01f, 0.3f, 0.7f, 0.2f });
    op.noteOn();
    EXPECT_TRUE(op.isActive());
}

TEST_F(PsyFmOperatorTest, RenderProducesNonZeroOutput) {
    op.setEnvelopeParams({ 0.001f, 0.1f, 0.8f, 0.1f });
    op.noteOn();
    op.setBlockParams(1.0f, 0.0f, 440.0f);
    float buf[64];
    op.renderBlock(buf, nullptr, 64);
    float maxVal = 0.0f;
    for (int i = 0; i < 64; i++)
        maxVal = std::max(maxVal, std::abs(buf[i]));
    EXPECT_GT(maxVal, 0.0f);
}

TEST_F(PsyFmOperatorTest, FeedbackAffectsOutput) {
    op.setEnvelopeParams({ 0.001f, 0.5f, 0.9f, 0.3f });
    op.noteOn();
    op.setBlockParams(1.0f, 0.0f, 440.0f);
    float bufNoFb[256];
    op.renderBlock(bufNoFb, nullptr, 256);

    PsyFmOperator op2;
    op2.prepare(44100.0);
    op2.setEnvelopeParams({ 0.001f, 0.5f, 0.9f, 0.3f });
    op2.noteOn();
    op2.setBlockParams(1.0f, 0.8f, 440.0f);
    float bufHighFb[256];
    op2.renderBlock(bufHighFb, nullptr, 256);

    float energyNoFb = 0.0f, energyHighFb = 0.0f;
    for (int i = 0; i < 256; i++) {
        energyNoFb += bufNoFb[i] * bufNoFb[i];
        energyHighFb += bufHighFb[i] * bufHighFb[i];
    }
    // High feedback should produce different (typically higher) energy
    EXPECT_NE(energyNoFb, energyHighFb);
}

TEST_F(PsyFmOperatorTest, ModulationInputAffectsOutput) {
    op.setEnvelopeParams({ 0.001f, 0.5f, 0.9f, 0.3f });
    op.noteOn();
    op.setBlockParams(1.0f, 0.0f, 440.0f);
    float mod[64];
    for (int i = 0; i < 64; i++) mod[i] = 1.0f;
    float bufWithMod[64];
    op.renderBlock(bufWithMod, mod, 64);

    PsyFmOperator op2;
    op2.prepare(44100.0);
    op2.setEnvelopeParams({ 0.001f, 0.5f, 0.9f, 0.3f });
    op2.noteOn();
    op2.setBlockParams(1.0f, 0.0f, 440.0f);
    float bufNoMod[64];
    op2.renderBlock(bufNoMod, nullptr, 64);

    bool different = false;
    for (int i = 0; i < 64; i++) {
        if (std::abs(bufWithMod[i] - bufNoMod[i]) > 1e-6f) { different = true; break; }
    }
    EXPECT_TRUE(different);
}

TEST_F(PsyFmOperatorTest, NoteOffEventuallyStops) {
    op.setEnvelopeParams({ 0.001f, 0.1f, 0.8f, 0.05f });
    op.noteOn();
    op.noteOff();
    // Render enough samples for release to complete
    float buf[4096];
    op.setBlockParams(1.0f, 0.0f, 440.0f);
    for (int i = 0; i < 64; i++)
        op.renderBlock(buf, nullptr, 64);
    EXPECT_FALSE(op.isActive());
}

// ── PsyFmModMatrix tests ──

TEST(PsyFmModMatrixTest, NoRoutesReturnsBaseValues) {
    PsyFmModMatrix m;
    PsyFmModSourcePool src;
    float baseRatios[6] = { 1, 2, 3, 4, 5, 6 };
    float outRatios[6];
    float outFeedback;
    m.apply(src, baseRatios, 0.5f, outRatios, outFeedback);
    for (int i = 0; i < 6; i++)
        EXPECT_FLOAT_EQ(outRatios[i], baseRatios[i]);
    EXPECT_FLOAT_EQ(outFeedback, 0.5f);
}

TEST(PsyFmModMatrixTest, SingleRouteModifiesTarget) {
    PsyFmModMatrix m;
    m.addRoute({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op6Feedback, 0.5f });
    PsyFmModSourcePool src;
    src.modWheelValue = 1.0f;
    float baseRatios[6] = { 1, 1, 1, 1, 1, 1 };
    float outRatios[6];
    float outFeedback;
    m.apply(src, baseRatios, 0.0f, outRatios, outFeedback);
    EXPECT_GT(outFeedback, 0.0f);
}

TEST(PsyFmModMatrixTest, DepthZeroLeavesTargetUnchanged) {
    PsyFmModMatrix m;
    m.addRoute({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op1Ratio, 0.0f });
    PsyFmModSourcePool src;
    src.modWheelValue = 1.0f;
    float baseRatios[6] = { 1, 1, 1, 1, 1, 1 };
    float outRatios[6];
    float outFeedback;
    m.apply(src, baseRatios, 0.0f, outRatios, outFeedback);
    EXPECT_FLOAT_EQ(outRatios[0], 1.0f);
}

TEST(PsyFmModMatrixTest, MultipleRoutesAccumulate) {
    PsyFmModMatrix m;
    m.addRoute({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op1Ratio, 0.3f });
    m.addRoute({ PsyFmModRoute::Source::Velocity, PsyFmModRoute::Dest::Op1Ratio, 0.2f });
    PsyFmModSourcePool src;
    src.modWheelValue = 1.0f;
    src.velocityValue = 1.0f;
    float baseRatios[6] = { 1, 1, 1, 1, 1, 1 };
    float outRatios[6];
    float outFeedback;
    m.apply(src, baseRatios, 0.0f, outRatios, outFeedback);
    EXPECT_NEAR(outRatios[0], 1.5f, 0.01f);
}

TEST(PsyFmModMatrixTest, ClearRoutesResets) {
    PsyFmModMatrix m;
    m.addRoute({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op6Feedback, 0.9f });
    m.clearRoutes();
    PsyFmModSourcePool src;
    src.modWheelValue = 1.0f;
    float baseRatios[6] = { 1, 1, 1, 1, 1, 1 };
    float outRatios[6];
    float outFeedback;
    m.apply(src, baseRatios, 0.0f, outRatios, outFeedback);
    EXPECT_FLOAT_EQ(outFeedback, 0.0f);
}

// ── PsyFmPatches tests ──

TEST(PsyFmPatchesTest, GrowlBassHasRoutes) {
    auto m = PsyFmPatches::makeGrowlBassMatrix();
    EXPECT_GT(m.getRoutes().size(), 0u);
}

TEST(PsyFmPatchesTest, RiserHasRoutes) {
    auto m = PsyFmPatches::makeRiserMatrix();
    EXPECT_GT(m.getRoutes().size(), 0u);
}

TEST(PsyFmPatchesTest, AcidLeadHasRoutes) {
    auto m = PsyFmPatches::makeAcidLeadMatrix();
    EXPECT_GT(m.getRoutes().size(), 0u);
}

TEST(PsyFmPatchesTest, MetallicPluckHasRoutes) {
    auto m = PsyFmPatches::makeMetallicPluckMatrix();
    EXPECT_GT(m.getRoutes().size(), 0u);
}

// ── PsyFmEngine tests ──

class PsyFmEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine.prepare(44100.0, 512);
        engine.setAlgorithm(growlBassAlgorithm);
    }
    PsyFmEngine engine;
};

TEST_F(PsyFmEngineTest, InitialStateNoActiveVoices) {
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(PsyFmEngineTest, NoteOnActivatesVoice) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 512);
    buf.clear();
    engine.render(buf, midi);
    EXPECT_GT(engine.activeVoiceCount(), 0);
}

TEST_F(PsyFmEngineTest, RenderProducesNonZeroOutput) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 512);
    buf.clear();
    engine.render(buf, midi);
    float maxVal = 0.0f;
    for (int i = 0; i < buf.getNumSamples(); i++)
        maxVal = std::max(maxVal, std::abs(buf.getSample(0, i)));
    EXPECT_GT(maxVal, 0.0f);
}

TEST_F(PsyFmEngineTest, NoteOffEventuallyDeactivatesVoice) {
    // Use zero-sustain, zero-release envelope for instant deactivation
    juce::ADSR::Parameters env;
    env.attack = 0.001f; env.decay = 0.01f; env.sustain = 0.0f; env.release = 0.0f;
    for (int op = 0; op < 6; op++) engine.setOpEnvelope(op, env);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 512);
    buf.clear();
    engine.render(buf, midi);
    EXPECT_EQ(engine.activeVoiceCount(), 1);

    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    buf.clear();
    engine.render(buf, midi);

    // With zero release, the voice should deactivate on the next render
    midi.clear();
    buf.clear();
    engine.render(buf, midi);
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(PsyFmEngineTest, AllNotesOffClearsVoices) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 512);
    buf.clear();
    engine.render(buf, midi);

    midi.clear();
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    buf.clear();
    engine.render(buf, midi);
    EXPECT_EQ(engine.activeVoiceCount(), 0);
}

TEST_F(PsyFmEngineTest, StereoOutput) {
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(2, 512);
    buf.clear();
    engine.render(buf, midi);
    float maxL = 0.0f, maxR = 0.0f;
    for (int i = 0; i < buf.getNumSamples(); i++) {
        maxL = std::max(maxL, std::abs(buf.getSample(0, i)));
        maxR = std::max(maxR, std::abs(buf.getSample(1, i)));
    }
    EXPECT_GT(maxL, 0.0f);
    EXPECT_GT(maxR, 0.0f);
}

TEST_F(PsyFmEngineTest, SetBaseRatiosAffectsOutput) {
    float ratios1[6] = { 1, 1, 1, 1, 1, 1 };
    engine.setBaseRatios(ratios1);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf1(1, 512);
    buf1.clear();
    engine.render(buf1, midi);

    // Re-init for second render
    engine.prepare(44100.0, 512);
    engine.setAlgorithm(growlBassAlgorithm);
    float ratios2[6] = { 1, 3, 1, 1, 5, 1 };
    engine.setBaseRatios(ratios2);
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf2(1, 512);
    buf2.clear();
    engine.render(buf2, midi);

    bool different = false;
    for (int i = 0; i < 512; i++) {
        if (std::abs(buf1.getSample(0, i) - buf2.getSample(0, i)) > 1e-6f) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
}

TEST_F(PsyFmEngineTest, ModMatrixIntegration) {
    PsyFmModMatrix m;
    m.addRoute({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op6Feedback, 0.5f });
    engine.setModMatrix(std::move(m));
    engine.getModSourcePool().modWheelValue = 1.0f;

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
    juce::AudioBuffer<float> buf(1, 512);
    buf.clear();
    engine.render(buf, midi);
    float maxVal = 0.0f;
    for (int i = 0; i < buf.getNumSamples(); i++)
        maxVal = std::max(maxVal, std::abs(buf.getSample(0, i)));
    EXPECT_GT(maxVal, 0.0f);
}

TEST_F(PsyFmEngineTest, OnBarBoundaryAdvancesRiserLfo) {
    float initialRate = engine.getModSourcePool().ratioSweepLFORateHz;
    engine.onBarBoundary(8);
    EXPECT_GT(engine.getModSourcePool().ratioSweepLFORateHz, initialRate);
}

TEST_F(PsyFmEngineTest, AlgorithmFunctionsDontCrash) {
    // Test each algorithm function
    auto testAlgo = [&](auto fn, const char* name) {
        PsyFmEngine e;
        e.prepare(44100.0, 512);
        e.setAlgorithm(fn);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        juce::AudioBuffer<float> buf(1, 256);
        buf.clear();
        EXPECT_NO_THROW(e.render(buf, midi)) << "Algorithm " << name << " crashed";
    };
    testAlgo(growlBassAlgorithm, "growlBass");
    testAlgo(acidLeadAlgorithm, "acidLead");
    testAlgo(metallicPluckAlgorithm, "metallicPluck");
    testAlgo(riserAlgorithm, "riser");
}

TEST_F(PsyFmEngineTest, MatrixSwapDuringRenderDoesNotCrash) {
    // Gate 3: concurrent render + setModMatrix must not crash or tear.
    std::atomic<bool> run_ { true };
    engine.prepare(44100.0, 512);
    engine.setAlgorithm(growlBassAlgorithm);

    std::thread audioThread ([&] {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        juce::AudioBuffer<float> buf(1, 512);
        while (run_.load(std::memory_order_relaxed))
        {
            buf.clear();
            engine.render(buf, midi);
        }
    });

    for (int i = 0; i < 200; ++i)
    {
        PsyFmModMatrix m;
        m.addRoute ({ PsyFmModRoute::Source::ModWheel,
                      PsyFmModRoute::Dest::Op6Feedback,
                      static_cast<float>(i) * 0.005f });
        engine.setModMatrix (std::move(m));
    }
    run_.store (false, std::memory_order_relaxed);
    audioThread.join();
    // Reached here without crash = lock is correct.
}

// ── PsyFmState (codec + presets + tree restore) ──

TEST(PsyFmStateTest, EncodeDecodeRoundTrip) {
    std::vector<PsyFmModRoute> routes = {
        { PsyFmModRoute::Source::FeedbackLFO, PsyFmModRoute::Dest::Op6Feedback, 0.4f },
        { PsyFmModRoute::Source::ModWheel,    PsyFmModRoute::Dest::Op1Ratio,   -0.3f },
    };
    std::string encoded = PsyFmState::encodeRoutes (routes);
    auto decoded = PsyFmState::decodeRoutes (encoded);
    ASSERT_EQ (decoded.size(), 2u);
    EXPECT_EQ (decoded[0].source, PsyFmModRoute::Source::FeedbackLFO);
    EXPECT_EQ (decoded[0].dest,   PsyFmModRoute::Dest::Op6Feedback);
    EXPECT_NEAR (decoded[0].depth, 0.4f, 1e-5f);
    EXPECT_EQ (decoded[1].source, PsyFmModRoute::Source::ModWheel);
    EXPECT_EQ (decoded[1].dest,   PsyFmModRoute::Dest::Op1Ratio);
    EXPECT_NEAR (decoded[1].depth, -0.3f, 1e-5f);
}

TEST(PsyFmStateTest, EncodeEmptyMatrixIsBlank) {
    std::string encoded = PsyFmState::encodeRoutes({});
    EXPECT_TRUE (encoded.empty());
}

TEST(PsyFmStateTest, DecodeEmptyOrGarbageIsTolerant) {
    auto r1 = PsyFmState::decodeRoutes("");
    EXPECT_TRUE (r1.empty());
    auto r2 = PsyFmState::decodeRoutes("notARoute;;:::");
    EXPECT_TRUE (r2.empty());
    auto r3 = PsyFmState::decodeRoutes("feedbackLFO:op6Feedback:0.5;bad:route;modWheel:op1Ratio:1.0");
    ASSERT_EQ (r3.size(), 2u);
    EXPECT_EQ (r3[0].source, PsyFmModRoute::Source::FeedbackLFO);
    EXPECT_EQ (r3[1].dest,   PsyFmModRoute::Dest::Op1Ratio);
}

TEST(PsyFmStateTest, PresetTableCompleteness) {
    std::string names[] = { "growlBass", "acidLead", "metallicPluck", "riser" };
    for (const auto& name : names)
    {
        auto* p = PsyFmState::findPreset(name);
        ASSERT_NE(p, nullptr) << "Preset not found: " << name;
        EXPECT_EQ(std::string(p->name), name);
        EXPECT_GE(p->algorithm, 0);
        EXPECT_LE(p->algorithm, 3);
        EXPECT_GE(p->outputLevel, 0.0f);
        EXPECT_LE(p->outputLevel, 1.0f);
        EXPECT_GE(p->feedback, 0.0f);
        EXPECT_LE(p->feedback, 1.0f);
        for (int op = 0; op < 6; ++op)
        {
            EXPECT_GE(p->env[op][0], 0.0f) << name << " op" << op;
            EXPECT_GE(p->env[op][2], 0.0f) << name << " op" << op;
            EXPECT_LE(p->env[op][2], 1.0f) << name << " op" << op;
        }
        auto routes = PsyFmState::decodeRoutes(p->matrix);
        EXPECT_FALSE(routes.empty()) << name << " has no decoded routes";
    }
}

TEST(PsyFmStateTest, PresetLookupReturnsNullptrForUnknown) {
    EXPECT_EQ(PsyFmState::findPreset("nonexistent"), nullptr);
    EXPECT_FALSE(PsyFmState::isKnownPreset("nonexistent"));
}

// ── Gate 1/10: live-matrix restore after TrackFXSlot rebuild ──

TEST(PsyFmStateTest, LiveMatrixRestoreAfterRebuild) {
    juce::ValueTree slotTree("FX_SLOT");
    slotTree.setProperty("fxType", "psy_fm", nullptr);
    slotTree.setProperty("psyFmMatrix",
        juce::String("feedbackLFO:op6Feedback:0.4;modWheel:op1Ratio:0.5"), nullptr);
    slotTree.setProperty("psyFmSweepRate", 0.3, nullptr);

    TrackFXSlot slot("psy_fm");
    juce::dsp::ProcessSpec spec { 44100.0, 512, 1 };
    slot.prepare(spec);
    slot.loadPsyFmStateFromTree(slotTree);

    auto* psyFm = slot.psyFmEngine();
    ASSERT_NE(psyFm, nullptr);
    const auto& routes = psyFm->getModMatrix().getRoutes();
    ASSERT_EQ(routes.size(), 2u);
    EXPECT_EQ(routes[0].source, PsyFmModRoute::Source::FeedbackLFO);
    EXPECT_EQ(routes[0].dest,   PsyFmModRoute::Dest::Op6Feedback);
    EXPECT_NEAR(routes[0].depth, 0.4f, 1e-5f);
    EXPECT_EQ(routes[1].source, PsyFmModRoute::Source::ModWheel);
    EXPECT_EQ(routes[1].dest,   PsyFmModRoute::Dest::Op1Ratio);
    EXPECT_NEAR(routes[1].depth, 0.5f, 1e-5f);
    EXPECT_NEAR(psyFm->getModSourcePool().ratioSweepLFORateHz, 0.3f, 1e-5f);
}

TEST(PsyFmStateTest, AbsentPropertiesLeaveDefaults) {
    juce::ValueTree slotTree("FX_SLOT");
    slotTree.setProperty("fxType", "psy_fm", nullptr);

    TrackFXSlot slot("psy_fm");
    juce::dsp::ProcessSpec spec { 44100.0, 512, 1 };
    slot.prepare(spec);
    slot.loadPsyFmStateFromTree(slotTree);

    auto* psyFm = slot.psyFmEngine();
    ASSERT_NE(psyFm, nullptr);
    // sweep rate stays at engine default when tree has no psyFmSweepRate
    EXPECT_NEAR(psyFm->getModSourcePool().ratioSweepLFORateHz, 0.2f, 1e-5f);
    // no crash, no state corruption � Gate 1/10 sanity
}

TEST(PsyFmStateTest, NonPsyFmSlotIgnoresMatrixRestore) {
    juce::ValueTree slotTree("FX_SLOT");
    slotTree.setProperty("fxType", "eq", nullptr);
    slotTree.setProperty("psyFmMatrix", juce::String("feedbackLFO:op6Feedback:0.4"), nullptr);

    TrackFXSlot slot("eq");
    juce::dsp::ProcessSpec spec { 44100.0, 512, 1 };
    slot.prepare(spec);
    slot.loadPsyFmStateFromTree(slotTree);
    // No crash, no side effect on the non-psy_fm slot.
    EXPECT_EQ(slot.psyFmEngine(), nullptr);
}

TEST(PsyFmModMatrixTest, FeedbackDepthBudgetScalesWhenExceeded) {
    // Bug 5: two routes to Op6Feedback with depth 0.6 each = total 1.2 > 1.0
    // Each should be scaled to 0.6/1.2 = 0.5 of original depth.
    PsyFmModMatrix m;
    m.addRoute({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op6Feedback, 0.6f });
    m.addRoute({ PsyFmModRoute::Source::Velocity, PsyFmModRoute::Dest::Op6Feedback, 0.6f });

    PsyFmModSourcePool sources;
    sources.modWheelValue = 1.0f;
    sources.velocityValue = 1.0f;

    float baseRatios[6] = { 1,1,1,1,1,1 };
    float outRatios[6];
    float outFeedback;

    // With budget: each amount = srcVal * depth * (1.0/1.2) = 1.0 * 0.6 * 0.833 = 0.5
    // Total added = 0.5 + 0.5 = 1.0 → clamped to 1.0
    // Without budget: total = 0.6 + 0.6 = 1.2 → clamped to 1.0 (same result but different mechanism)
    // Better test: base feedback = 0.5, two routes of 0.6 each
    // With budget: each = 0.5 * 0.833 = 0.417, total added = 0.833, result = clamp(0.5+0.833) = 1.0
    // Without budget: each = 0.6, total = 1.2, result = clamp(0.5+1.2) = 1.0
    // Same result here. Let me test with a case where the budget matters:
    // base = 0.0, three routes of 0.4 each = total 1.2 > 1.0
    // With budget: each scaled to 0.4/1.2 = 0.333, total = 1.0 → clamp(0+1.0) = 1.0
    // Without budget: each = 0.4, total = 1.2 → clamp(0+1.2) = 1.0
    // Still same! The budget only makes a difference when sources aren't at max.
    // Test with sources at 0.5: two routes of 0.6 each
    // With budget: amount = 0.5 * 0.6 * (1/1.2) = 0.25 per route, total = 0.5
    // Without budget: amount = 0.5 * 0.6 = 0.3 per route, total = 0.6
    sources.modWheelValue = 0.5f;
    sources.velocityValue = 0.5f;
    m.apply(sources, baseRatios, 0.0f, outRatios, outFeedback);
    // With budget: 0.25 + 0.25 = 0.5
    EXPECT_NEAR(outFeedback, 0.5f, 0.01f);
}

TEST(PsyFmModMatrixTest, SingleRouteUnaffectedByBudget) {
    // Single route with depth 0.4 → no budget scaling (total = 0.4 < 1.0)
    PsyFmModMatrix m;
    m.addRoute({ PsyFmModRoute::Source::ModWheel, PsyFmModRoute::Dest::Op6Feedback, 0.4f });

    PsyFmModSourcePool sources;
    sources.modWheelValue = 1.0f;

    float baseRatios[6] = { 1,1,1,1,1,1 };
    float outRatios[6];
    float outFeedback;
    m.apply(sources, baseRatios, 0.3f, outRatios, outFeedback);
    // 0.3 + (1.0 * 0.4) = 0.7, no scaling
    EXPECT_NEAR(outFeedback, 0.7f, 0.01f);
}
