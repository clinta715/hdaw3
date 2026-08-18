#include <gtest/gtest.h>
#include <cstdlib>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ExportManager.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>

// Regression coverage for the spurious
// "Render graph bake timed out after 15000ms - export aborted." failure on
// large projects. The bake wait in ExportManager::renderThreadFunc used a
// FIXED 15s default, but a 13-track/771-clip graph legitimately takes
// ~17-21s to bake on the JUCE message thread. The default now scales with
// clip count via ExportManager::computeBakeWaitMs (floor 15s, 50ms/clip,
// cap 120s); HDAW_EXPORT_BAKE_TIMEOUT_MS still overrides it.

namespace {

juce::ValueTree makeProjectWithClips(int numClips, int numTracks = 1)
{
    juce::ValueTree project(IDs::PROJECT);
    juce::ValueTree trackList(IDs::TRACK_LIST);
    for (int t = 0; t < numTracks; ++t)
    {
        juce::ValueTree track(IDs::TRACK);
        juce::ValueTree clipList(IDs::CLIP_LIST);
        for (int c = 0; c < numClips; ++c)
        {
            clipList.addChild(ProjectModel::createMidiClipEmpty(
                                  "c" + juce::String(t * numClips + c),
                                  c * 0.5, 0.4),
                              -1, nullptr);
        }
        track.addChild(clipList, -1, nullptr);
        trackList.addChild(track, -1, nullptr);
    }
    project.addChild(trackList, -1, nullptr);
    return project;
}

bool waitForExport(HDAW::ExportManager& em, int timeoutMs = 60000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + timeoutMs;
    while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    return !em.isExporting();
}

} // namespace

TEST(ExportBakeTimeout, ComputeBakeWaitMsFormula)
{
    // Empty project (no TRACK_LIST): the guard keeps totalClips at 0 -> floor.
    const juce::ValueTree empty(IDs::PROJECT);
    EXPECT_EQ(HDAW::ExportManager::computeBakeWaitMs(empty), 15000u);

    // A few clips: 10000 + 50*5 = 10250, clamped up to the 15s floor.
    const juce::ValueTree few = makeProjectWithClips(5);
    EXPECT_EQ(HDAW::ExportManager::computeBakeWaitMs(few), 15000u);

    // 800 clips: 10000 + 800*50 = 50000 -> scales above the floor, under the cap.
    const juce::ValueTree many = makeProjectWithClips(800);
    const uint32_t manyMs = HDAW::ExportManager::computeBakeWaitMs(many);
    EXPECT_GT(manyMs, 15000u);
    EXPECT_LE(manyMs, 120000u);
    EXPECT_EQ(manyMs, 50000u);

    // 5000 clips: 10000 + 5000*50 = 260000 -> capped at 120s.
    const juce::ValueTree huge = makeProjectWithClips(5000);
    EXPECT_EQ(HDAW::ExportManager::computeBakeWaitMs(huge), 120000u);
}

TEST(ExportBakeTimeout, LargeProjectExportsWithDefaultTimeout)
{
    // Guarantee the DEFAULT (env-free) bake-timeout path is exercised: a prior
    // test may have left HDAW_EXPORT_BAKE_TIMEOUT_MS set (e.g. 120000), which
    // would mask the regression this test guards against.
    _putenv_s("HDAW_EXPORT_BAKE_TIMEOUT_MS", "");

    // ~800 MIDI clips across 4 tracks (200 each), staggered starts.
    const juce::ValueTree project = makeProjectWithClips(200, 4);

    AudioEngine engine;
    engine.initialize();
    auto* mp = engine.getMainProcessor();
    ASSERT_NE(mp, nullptr);

    auto& em = mp->getExportManager();

    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();

    const juce::File outFile =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("hdaw_bake_timeout_export.wav");
    outFile.deleteFile();

    ASSERT_TRUE(em.startExport(project, exportFm, &engine.getPluginManager(),
                               outFile, 44100.0, 0.0, 2.0,
                               HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));
    EXPECT_FALSE(em.isExporting());

    EXPECT_TRUE(outFile.existsAsFile());
    EXPECT_GT(outFile.getSize(), 1000);
}
