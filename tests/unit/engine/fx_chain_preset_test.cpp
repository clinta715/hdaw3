#include <gtest/gtest.h>
#include "engine/ChainLibrary.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"

// Qt defines `slots` as a keyword macro (signals/slots); once Qt headers are
// pulled in (via AudioEngine.h) it textually blanks ChainPreset::slots. This
// TU uses no Qt signals/slots keywords, so undef it. Task 1's
// chain_library_test.cpp dodges this by including no Qt headers at all.
#ifdef slots
#undef slots
#endif

// Task 2 (plans/2026-09-02-fx-chain-presets.md): apply a chain and assert the
// LIVE processor (lesson-10 seam), rebuild, assert again, then round-trip
// through exportFxChain.
TEST(FxChainPreset, ApplySurvivesRebuildLive)
{
    AudioEngine engine;
    engine.initialize();
    auto& commands = engine.getAudioEngineCommands();

    HDAW::ChainPreset p;
    p.name = "T";
    HDAW::ChainPreset::Slot a;
    a.fxType = "compressor";
    a.params = { { "param_0", -12.0 } };
    HDAW::ChainPreset::Slot b;
    b.fxType = "filter";
    b.params = { { "param_0", 800.0 } };
    b.bypassed = true;
    p.slots = { a, b };

    juce::String error;
    ASSERT_TRUE(commands.applyFxChain(0, p, &error)) << error.toStdString();

    auto* track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->getFXChain().size(), 2u);
    EXPECT_EQ(track->getFXChain()[0]->getType().toStdString(), "compressor");

    commands.rebuildTrackFX(0);

    track = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->getFXChain().size(), 2u);
    ASSERT_GE(track->getFXChain()[0]->getInternalParamValues().size(), 1u);
    EXPECT_DOUBLE_EQ(track->getFXChain()[0]->getInternalParamValues()[0], -12.0);
    EXPECT_TRUE(track->getFXChain()[1]->isBypassed());

    // Round-trip: export must equal what was applied.
    auto exported = commands.exportFxChain(0);
    ASSERT_EQ(exported.slots.size(), 2u);
    EXPECT_EQ(exported.slots[0].fxType.toStdString(), "compressor");
    ASSERT_TRUE(exported.slots[0].params.count("param_0") > 0);
    EXPECT_DOUBLE_EQ(exported.slots[0].params.at("param_0"), -12.0);
    EXPECT_EQ(exported.slots[1].fxType.toStdString(), "filter");
    EXPECT_TRUE(exported.slots[1].bypassed);
    ASSERT_TRUE(exported.slots[1].params.count("param_0") > 0);
    EXPECT_DOUBLE_EQ(exported.slots[1].params.at("param_0"), 800.0);
}
