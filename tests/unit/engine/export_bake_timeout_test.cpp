#include <gtest/gtest.h>
#include <cstdlib>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ExportManager.h"
#include "engine/ProjectSerializer.h"
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
    // Throwaway id mint — the tree is detached; ids only need to be unique
    // within it.
    ProjectModel model;
    juce::ValueTree project(IDs::PROJECT);
    juce::ValueTree trackList(IDs::TRACK_LIST);
    for (int t = 0; t < numTracks; ++t)
    {
        juce::ValueTree track(IDs::TRACK);
        juce::ValueTree clipList(IDs::CLIP_LIST);
        for (int c = 0; c < numClips; ++c)
        {
            clipList.addChild(model.createMidiClipEmpty(
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


// Handoff §3 repro attempt (2026-08-27): the engine aborted (MSVC dialog)
// during the long antinomy_remix exports with 4 enabled automation lanes
// driving internal FX (EQ freq, phaser centre-freq/depth, flanger rate)
// via TrackFXSlot::setAutomationParam -> applyInternalParamToDsp on the
// render path. Faithful first pass: load the SAVED seed (lanes already
// enabled in the file) and render the FULL project duration a few times,
// watching for debug-heap/CRT aborts.
TEST(ExportAutomation, SeedProjectLongRenderDoesNotAbort)
{
    _putenv_s("HDAW_EXPORT_BAKE_TIMEOUT_MS", "");

    const juce::File seed("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme/antinomy_remix_FINAL.hdaw");
    if (!seed.existsAsFile())
        GTEST_SKIP() << "seed project not present";
    const juce::File outBase = juce::File::getSpecialLocation(juce::File::tempDirectory);

    AudioEngine engine;
    engine.initialize();
    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine.getProjectModel(), seed));
    engine.getMainProcessor()->rebuildRoutingGraph();

    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();

    const double dur = HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
    juce::Logger::writeToLog("ExportAutomation: project duration " + juce::String(dur, 1) + " s");

    for (int iter = 1; iter <= 3; ++iter)
    {
        const juce::File out = outBase.getChildFile("hdaw_seed_repro_" + juce::String(iter) + ".wav");
        out.deleteFile();
        ASSERT_TRUE(em.startExport(engine.getProjectModel().getTree(), exportFm,
                                   &engine.getPluginManager(), out,
                                   48000.0, 0.0, dur,
                                   HDAW::ExportManager::WAV, 24))
            << "iter " << iter << " startExport";
        ASSERT_TRUE(waitForExport(em, 900000)) << "iter " << iter << " render timed out";
        EXPECT_FALSE(em.isExporting());
        EXPECT_TRUE(out.existsAsFile()) << "iter " << iter << " no output file";
        EXPECT_GT(out.getSize(), 1000) << "iter " << iter << " output too small";
        juce::Logger::writeToLog("ExportAutomation: iter " + juce::String(iter)
            + " complete, size=" + juce::String(out.getSize()));
        out.deleteFile();
    }
}


// §3 probe A (2026-08-27): session-accumulation stress — the crash followed
// hours of exports, lane toggles and save/load cycles in ONE engine process.
// Repeat that exact loop (toggle the 4 FX lanes off/on, save+load, render a
// window) and watch for the debug-CRT heap abort.
TEST(ExportAutomation, SessionAccumulationStress)
{
    _putenv_s("HDAW_EXPORT_BAKE_TIMEOUT_MS", "");
    const juce::File seed("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme/antinomy_remix_FINAL.hdaw");
    if (!seed.existsAsFile())
        GTEST_SKIP() << "seed project not present";
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

    AudioEngine engine;
    engine.initialize();
    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine.getProjectModel(), seed));
    engine.getMainProcessor()->rebuildRoutingGraph();
    auto& cmds = engine.getProjectCommands();
    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    auto& model = engine.getProjectModel();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();

    auto enabledFXLanes = [&model]() {
        std::vector<juce::ValueTree> lanes;
        auto trackList = model.getTrackListTree();
        for (int t = 0; t < trackList.getNumChildren(); ++t)
        {
            auto al = trackList.getChild(t).getChildWithName(IDs::AUTOMATION_LIST);
            if (!al.isValid()) continue;
            for (auto lane : al)
                if (static_cast<int>(lane.getProperty(IDs::paramID, 0)) >= 100
                    && lane.getProperty(IDs::automationEnabled, false))
                    lanes.push_back(lane);
        }
        return lanes;
    };

    ASSERT_GE(enabledFXLanes().size(), 4u) << "seed must have the 4 enabled FX lanes";

    for (int i = 1; i <= 6; ++i)
    {
        juce::Logger::writeToLog("SessionAccumulation: iter " + juce::String(i) + " start");
        engine.drainPendingRoutingRebuild();

        // Toggle the enabled FX lanes off then on (disable/re-enable pattern).
        for (auto lane : enabledFXLanes())
            lane.setProperty(IDs::automationEnabled, false, &model.getUndoManager());
        for (auto lane : enabledFXLanes())
            lane.setProperty(IDs::automationEnabled, true, &model.getUndoManager());

        // Save + load: full routing rebuild both ways (the engine's save()
        // also cancels+joins any in-flight render).
        const juce::File projFile = tempDir.getChildFile("hdaw_stress_iter" + juce::String(i) + ".hdaw");
        projFile.deleteFile();
        ASSERT_TRUE(cmds.saveProject(projFile.getFullPathName().toStdString()));
        ASSERT_TRUE(cmds.loadProject(projFile.getFullPathName().toStdString()));
        projFile.deleteFile();

        // Render a 60 s window with the lanes live.
        engine.drainPendingRoutingRebuild();
        const juce::File out = tempDir.getChildFile("hdaw_stress_render" + juce::String(i) + ".wav");
        out.deleteFile();
        ASSERT_TRUE(em.startExport(model.getTree(), exportFm, &engine.getPluginManager(),
                                   out, 48000.0, 0.0, 60.0,
                                   HDAW::ExportManager::WAV, 24));
        ASSERT_TRUE(waitForExport(em, 600000));
        EXPECT_GT(out.getSize(), 1000) << "iter " << i << " render empty";
        out.deleteFile();
        juce::Logger::writeToLog("SessionAccumulation: iter " + juce::String(i) + " done");
    }
}

// §3 probe B (2026-08-27): render-vs-live-mutation race — start a long
// export, then hammer structural live-tree mutations while it renders. Each
// pump-side rebuildRoutingGraph drains (cancel+join) the in-flight export,
// so this repeatedly tears down the render graph mid-flight — the teardown
// stress the session accumulated across hours of exports + rebuilds.
TEST(ExportAutomation, LiveMutationsDuringExportStress)
{
    _putenv_s("HDAW_EXPORT_BAKE_TIMEOUT_MS", "");
    const juce::File seed("D:/pdf/roo projects/hdaw3/.tmp_dnb_theme/antinomy_remix_FINAL.hdaw");
    if (!seed.existsAsFile())
        GTEST_SKIP() << "seed project not present";
    const juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);

    AudioEngine engine;
    engine.initialize();
    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine.getProjectModel(), seed));
    engine.getMainProcessor()->rebuildRoutingGraph();
    auto& cmds = engine.getProjectCommands();
    auto* mp = engine.getMainProcessor();
    auto& em = mp->getExportManager();
    auto& model = engine.getProjectModel();
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const double dur = HDAW::ExportManager::calculateProjectDuration(model);

    for (int i = 1; i <= 6; ++i)
    {
        const juce::File out = tempDir.getChildFile("hdaw_race_render" + juce::String(i) + ".wav");
        out.deleteFile();
        ASSERT_TRUE(em.startExport(model.getTree(), exportFm, &engine.getPluginManager(),
                                   out, 48000.0, 0.0, dur,
                                   HDAW::ExportManager::WAV, 24))
            << "iter " << i << " startExport";

        const uint32_t deadline = juce::Time::getMillisecondCounter() + 12000u;
        int mutations = 0;
        while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        {
            const int multi = 0;
            const juce::String name = "StressTmp";
            int nt = cmds.addTrack(name.toStdString(), -1, -1, 0);
            if (nt >= 0)
            {
                engine.drainPendingRoutingRebuild(); // pump applies full rebuild → drains export
                cmds.removeTrack(nt);
                engine.drainPendingRoutingRebuild();
                ++mutations;
            }
        }
        juce::Logger::writeToLog("LiveMutationsDuringExport: iter " + juce::String(i)
            + " applied " + juce::String(mutations) + " mutation cycles");
        // The drain should have cancelled the render; wait for full teardown.
        EXPECT_TRUE(waitForExport(em, 60000));
        out.deleteFile();
    }
}
