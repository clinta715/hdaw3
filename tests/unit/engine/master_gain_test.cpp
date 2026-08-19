#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/RoutingManager.h"
#include "engine/ExportManager.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>

// Master-bus gain (Task A): persisted root property, live listener path,
// restore on routing-graph rebuild (Gate 1/10), save/load round-trip, and
// audible render attenuation through the export/tree-copy path.

namespace {

bool waitForExport(HDAW::ExportManager& em, int timeoutMs = 180000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + timeoutMs;
    while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    return !em.isExporting();
}

float peakOf(const juce::File& wav)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(wav));
    if (!r) return -1.0f;
    juce::AudioBuffer<float> buf(2, static_cast<int>(r->lengthInSamples));
    r->read(&buf, 0, static_cast<int>(r->lengthInSamples), 0, true, true);
    float peak = 0.0f;
    for (int c = 0; c < 2; ++c)
        for (int s = 0; s < buf.getNumSamples(); ++s)
            peak = std::max(peak, std::abs(buf.getSample(c, s)));
    return peak;
}

} // namespace

TEST(MasterGain, DefaultAndCommand)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    EXPECT_NEAR(engine.getReadModel().snapshot().masterGain, 1.0f, 1e-6f);
    EXPECT_NEAR(engine.getProjectModel().getMasterGain(), 1.0f, 1e-6f);

    cmds.setMasterGain(0.5f);
    EXPECT_NEAR(engine.getReadModel().snapshot().masterGain, 0.5f, 1e-6f);

    // Live processor follows via the root-property listener (A4). In the
    // environmental no-audio-device case routingManager is null (lesson 17);
    // SurvivesRebuild below is the dedicated live-processor gate test.
    if (auto* rm = engine.getMainProcessor()->getRoutingManager())
    {
        auto* mb = rm->getMasterBus();
        ASSERT_NE(mb, nullptr);
        EXPECT_FLOAT_EQ(mb->getGain(), 0.5f);
    }

    cmds.undo();
    EXPECT_NEAR(engine.getProjectModel().getMasterGain(), 1.0f, 1e-6f);
    EXPECT_NEAR(engine.getReadModel().snapshot().masterGain, 1.0f, 1e-6f);
    if (auto* rm = engine.getMainProcessor()->getRoutingManager())
        if (auto* mb = rm->getMasterBus())
            EXPECT_FLOAT_EQ(mb->getGain(), 1.0f);
}

// Gate 1/10: mutate -> rebuild -> assert the LIVE processor, not the ReadModel.
TEST(MasterGain, SurvivesRebuild)
{
    AudioEngine engine;
    engine.initialize();

    engine.getProjectCommands().setMasterGain(0.5f);

    engine.getMainProcessor()->rebuildRoutingGraph();

    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    auto* mb = rm->getMasterBus();
    ASSERT_NE(mb, nullptr);
    EXPECT_FLOAT_EQ(mb->getGain(), 0.5f);
}

TEST(MasterGain, SaveLoadRoundTrip)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    cmds.setMasterGain(0.25f);

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_master_gain_roundtrip.hdaw");
    tmp.deleteFile();

    ASSERT_TRUE(cmds.saveProject(tmp.getFullPathName().toStdString()));

    cmds.newProject();
    EXPECT_NEAR(engine.getProjectModel().getMasterGain(), 1.0f, 1e-6f);

    ASSERT_TRUE(cmds.loadProject(tmp.getFullPathName().toStdString()));
    EXPECT_NEAR(engine.getProjectModel().getMasterGain(), 0.25f, 1e-6f);

    tmp.deleteFile();
}

// End-to-end audible proof: the export renders from a tree COPY whose root
// masterGain property travels with it; the export's RoutingManager rebuild
// restores the gain on its fresh MasterBusProcessor (A3/A7).
TEST(MasterGain, RenderAttenuation)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Deterministic content: internal fm_synth part, fixed seed (no plugins).
    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Lead";
    params.style = "Lead";
    params.lengthBeats = 4.0;
    params.placement = "region";
    params.count = 1;
    params.seed = 42;
    auto res = cmds.addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    ASSERT_GE(res.trackIndex, 0);

    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    auto& em = engine.getMainProcessor()->getExportManager();
    const double duration = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const juce::File outUnity = tempDir.getChildFile("hdaw_master_gain_unity.wav");
    const juce::File outHalf  = tempDir.getChildFile("hdaw_master_gain_half.wav");
    outUnity.deleteFile();
    outHalf.deleteFile();

    // Export 1: master at unity (default).
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outUnity, 44100.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));

    // Export 2: same tree window at master 0.5.
    cmds.setMasterGain(0.5f);
    ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm, nullptr,
                               outHalf, 44100.0, 0.0, duration,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));

    const float peakUnity = peakOf(outUnity);
    const float peakHalf  = peakOf(outHalf);
    std::cout << "[MasterGain] peak master=1.0: " << peakUnity
              << ", peak master=0.5: " << peakHalf << std::endl;

    ASSERT_GT(peakUnity, 0.01f) << "fm_synth render is silent";
    EXPECT_NEAR(peakHalf, 0.5f * peakUnity, 0.1f * peakUnity)
        << "master gain 0.5 must halve the rendered peak";

    outUnity.deleteFile();
    outHalf.deleteFile();
}
