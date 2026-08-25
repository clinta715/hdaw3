#include <gtest/gtest.h>
#include <string>
#include <cstdlib>
#include <chrono>
#include <thread>

#include "engine/AudioEngine.h"
#include "model/ProjectModel.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ExportManager.h"
#include "common/ProjectCommands.h"
#include <juce_audio_formats/juce_audio_formats.h>

// RAII guard for process-wide env vars — gtest runs in a single process.
struct EnvGuard {
    std::string key;
    std::string old;
    bool had = false;
    EnvGuard(const char* k, const std::string& v) : key(k) {
        const char* cur = std::getenv(k);
        had = (cur != nullptr);
        if (had) old = cur;
        _putenv((key + "=" + v).c_str());
    }
    ~EnvGuard() {
        if (had) _putenv((key + "=" + old).c_str());
        else _putenv((key + "=").c_str());
    }
};

// Poll helper: returns true when predicate becomes true within deadline, false otherwise.
template<typename F>
bool poll_until(F pred, int deadlineMs, int intervalMs = 50)
{
    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (ms >= deadlineMs) return false;
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
}

// B1/F1 (handoff 2026-08-24): an MCP-triggered offline render (verify/audition/
// autoGain windowed render via renderTrackWindow → the shared ExportManager) is
// not cancelled+joined when the handler times out. The orphaned render thread
// then races any full rebuildRoutingGraph (save/load/structural) or engine
// teardown → the silent-death use-after-free family (lessons 12/14, postmortem §6).
//
// Fix contract:
//  * renderTrackWindow's timeout path joins the render (cancelAndJoin) before
//    returning, so no render outlives its handler.
//  * rebuildRoutingGraph and the save funnels cancel+join any in-flight render
//    before mutating the live graph / reading the live processors.

TEST(OfflineRenderExclusion, WindowedRenderTimeoutJoinsBeforeReturning)
{
    // Handler wait forced to 300ms; the render of a long window (180s of audio)
    // cannot complete in 300ms, so verifyPart hits the timeout path. The render
    // thread's bake budget is 60s so it would keep the flag alive for a long
    // time if it were orphaned.
    EnvGuard g1("HDAW_EXPORT_BAKE_TIMEOUT_MS", "60000");
    EnvGuard g2("HDAW_RENDER_WINDOW_WAIT_MS", "300");

    AudioEngine engine;
    engine.initialize();

    ProjectCommands::InstrumentPartParams params;
    params.trackName = "DrainT1";
    params.style = "Standard";
    params.lengthBeats = 4.0;
    params.seed = 7;
    auto res = engine.getProjectCommands().addInstrumentPart(params);
    ASSERT_TRUE(res.error.empty()) << res.error;
    ASSERT_GE(res.trackIndex, 0) << "addInstrumentPart failed";

    // Long window (180s of audio) exceeds the 300ms handler wait → timeout.
    auto v = engine.getProjectCommands().verifyPart(res.trackIndex, 180.0);
    EXPECT_FALSE(v.ok) << "verifyPart should have timed out, err=[" << v.error << "]";
    EXPECT_NE(v.error.find("render timed out"), std::string::npos)
        << "expected 'render timed out', got: " << v.error;

    // Post-fix: the handler cancelAndJoin'd the render thread before returning,
    // so isExporting is false immediately (the thread was joined, not orphaned).
    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto& em = proc->getExportManager();
    EXPECT_FALSE(em.isExporting())
        << "isExporting true after a timed-out verifyPart — the render thread "
           "was not joined (orphaned render races the next rebuild/save)";
}

TEST(OfflineRenderExclusion, RebuildCancelsAndJoinsActiveRender)
{
    EnvGuard g("HDAW_EXPORT_BAKE_TIMEOUT_MS", "60000");
    (void)g;

    AudioEngine engine;
    engine.initialize();

    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);

    auto& em = proc->getExportManager();

    // Start a windowed render directly (mirrors renderTrackWindow internals).
    juce::ValueTree treeCopy = engine.getProjectModel().getTree().createCopy();
    auto& fm = engine.getProjectPool().getFormatManager();
    juce::File tempWav = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_test_drain_t2.wav");
    tempWav.deleteFile();

    ASSERT_FALSE(em.isExporting()) << "export already in progress (unexpected)";
    // Long duration (600s) guarantees the render is still actively processing
    // when rebuildRoutingGraph runs below — it cannot complete in the few ms
    // between startExport and the rebuild.
    ASSERT_TRUE(em.startExport(treeCopy, fm, &engine.getPluginManager(), tempWav,
                                48000.0, 0.0, 600.0, HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(em.isExporting()) << "startExport should have set isExporting true";

    // Now trigger a full routing-graph rebuild — the crash sequence:
    // verify-timeout → save/rebuild. Post-fix, cancelAndJoin runs first.
    // Pre-fix: the render is NOT cancelled, so isExporting stays true here.
    proc->rebuildRoutingGraph();

    // Render must have been cancelled+joined; isExporting false.
    EXPECT_FALSE(em.isExporting()) << "isExporting still true after rebuild — "
        "cancelAndJoin did not drain the render thread";

    // Then engine-side saveProject succeeds because the render is drained.
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File saveProj = tempDir.getChildFile("hdaw_test_drain_saveProject.hdawproj");
    saveProj.deleteFile();
    bool saveOk = engine.getProjectCommands().saveProject(saveProj.getFullPathName().toRawUTF8());
    EXPECT_TRUE(saveOk) << "saveProject should succeed after render is drained";
    EXPECT_FALSE(em.isExporting()) << "isExporting true after saveProject";

    // Cleanup
    tempWav.deleteFile();
    saveProj.deleteFile();
}

TEST(OfflineRenderExclusion, SaveProjectDrainsOrphanedRender)
{
    EnvGuard g("HDAW_EXPORT_BAKE_TIMEOUT_MS", "60000");
    (void)g;

    AudioEngine engine;
    engine.initialize();

    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto& em = proc->getExportManager();

    // Start a direct render.
    juce::ValueTree treeCopy = engine.getProjectModel().getTree().createCopy();
    auto& fm = engine.getProjectPool().getFormatManager();
    juce::File tempWav = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_test_drain_t3.wav");
    tempWav.deleteFile();

    ASSERT_FALSE(em.isExporting()) << "export already in progress";
    // Long duration so the render is still active when saveProject runs below.
    ASSERT_TRUE(em.startExport(treeCopy, fm, &engine.getPluginManager(), tempWav,
                                48000.0, 0.0, 600.0, HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(em.isExporting()) << "startExport should have set isExporting true";

    // saveProject should drain the render first and succeed.
    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File saveProj = tempDir.getChildFile("hdaw_test_drain_save_t3.hdawproj");
    saveProj.deleteFile();
    bool saveOk = engine.getProjectCommands().saveProject(saveProj.getFullPathName().toRawUTF8());
    EXPECT_TRUE(saveOk) << "saveProject should succeed after render is drained";
    EXPECT_FALSE(em.isExporting()) << "isExporting true after saveProject";

    // Cleanup
    tempWav.deleteFile();
    saveProj.deleteFile();
}