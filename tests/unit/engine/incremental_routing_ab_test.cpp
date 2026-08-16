#include <gtest/gtest.h>
#include "render_harness.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/RoutingManager.h"
#include "engine/ClipSourceProcessor.h"
#include "engine/ExportManager.h"
#include "model/ProjectModel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Incremental-routing A/B driver. Produces two PLAYABLE WAV files — one from
// the incremental-routing path (HDAW_FORCE_INCREMENTAL_ROUTING=1) and one from
// the pre-existing full rebuildRoutingGraph() path (flag off) — through the
// PRODUCTION export pipeline (ExportManager::startExport, which copies the
// tree and renders on its own worker thread with a message pump already
// running under test_main's MessagePumpThread). The two files are for a human
// critical-listening A/B of audible artifacts (clicks/pops/DC/phase); a
// numeric max-diff closes the loop.
//
// Both engines are driven through an IDENTICAL edit sequence designed to
// exercise the incremental path's crossfade recompute:
//   1. addClips with OVERLAPPING placements (moveClipWithOverlap trims the
//      siblings to adjacency, so crossfades appear),
//   2. moveClips pushing one clip INTO a new overlap (crossfade recompute),
//   3. removeClips of the last-position clip (incremental-safe Remove op).
// After every edit the LIVE graphs are asserted equal (same
// expectSameLiveGraph logic as IncrementalRoutingEngine, including the gain-
// envelope crossfade check via expectEnvelopeEqual from render_harness.h),
// then each engine's FINAL project tree is exported. Since the trees are
// identical, the two WAVs must be audibly indistinguishable; the printed
// max-diff guards the numeric side.
namespace {

constexpr const char* kIncrementalEnv = "HDAW_FORCE_INCREMENTAL_ROUTING";

std::string currentIncrementalEnv()
{
    const char* v = std::getenv(kIncrementalEnv);
    return v != nullptr ? std::string(v) : std::string();
}

// RAII set/restore of the incremental-routing env flag around
// engine.initialize() (the flag is captured at startup).
struct ScopedIncrementalFlag
{
    std::string saved;
    explicit ScopedIncrementalFlag(const char* value)
        : saved(currentIncrementalEnv())
    {
        _putenv_s(kIncrementalEnv, value != nullptr ? value : "");
    }
    ~ScopedIncrementalFlag()
    {
        _putenv_s(kIncrementalEnv, saved.c_str());
    }
};

// Guarantees the engine has a live RoutingManager so the incremental drain can
// actually touch the graph (drainPendingClipOps no-ops when rm == nullptr).
// Returns early when the environment already created one (device present).
bool ensureRoutingGraph(AudioEngine& engine)
{
    auto* proc = engine.getMainProcessor();
    if (proc == nullptr) return false;
    if (proc->getRoutingManager() != nullptr) return true;
    {
        const juce::MessageManagerLock pumpPark;
        proc->prepareToPlay(44100.0, 512);
    }
    engine.drainPendingRoutingRebuild();
    return proc->getRoutingManager() != nullptr;
}

// Settles the OFF (full-rebuild reference) engine to a state reflecting the
// current ValueTree, free of pump-thread races: drain any coalesced async
// rebuild, then force an explicit full rebuild (moves only mutate placement
// properties, which never queue an async rebuild), then drain again so the
// graph-internal bake lands before live processors are read.
void settleOffEngine(AudioEngine& off)
{
    off.drainPendingRoutingRebuild();
    off.getMainProcessor()->rebuildRoutingGraph();
    off.drainPendingRoutingRebuild();
}

// Ensures the AudioProcessorGraph's internal render-sequence bake (which
// recomputes getLatencySamples via Pimpl::handleAsyncUpdate) has landed on the
// pump thread before a graph-latency read. Posts a probe CallbackMessage AFTER
// the graph's own queued async-rebuild messages (FIFO), then waits for it —
// when it fires, every earlier queued message including the bake has been
// processed. Mirrors RenderHarness::waitForBake; only meaningful on the
// device-present path (prepareToPlay bakes asynchronously).
bool waitForGraphBake()
{
    auto bakeLanded = std::make_shared<std::atomic<bool>>(false);
    struct BakeProbe final : public juce::CallbackMessage
    {
        std::shared_ptr<std::atomic<bool>> landed;
        explicit BakeProbe(std::shared_ptr<std::atomic<bool>> f) : landed(std::move(f)) {}
        void messageCallback() override { landed->store(true, std::memory_order_release); }
    };
    auto* probe = new BakeProbe(bakeLanded);
    probe->post();

    constexpr uint32_t kMaxBakeWaitMs = 15000;
    const auto deadline = juce::Time::getMillisecondCounter() + kMaxBakeWaitMs;
    while (!bakeLanded->load(std::memory_order_acquire)
           && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    return bakeLanded->load(std::memory_order_acquire);
}

// Compares the LIVE audio-clip processors of two engines: same map size, same
// (trackIndex, clipIndex) keys, per-audio startTime/duration within SPSC float
// precision, and gain-envelope EXACTLY equal (the critical crossfade check).
void expectSameLiveGraph(AudioEngine& on, AudioEngine& off)
{
    auto* rOn = on.getMainProcessor()->getRoutingManager();
    auto* rOff = off.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rOn, nullptr);
    ASSERT_NE(rOff, nullptr);

    const auto& amOn = rOn->getAudioClipSources();
    const auto& amOff = rOff->getAudioClipSources();
    EXPECT_EQ(amOn.size(), amOff.size());
    for (const auto& kv : amOff)
    {
        auto it = amOn.find(kv.first);
        ASSERT_NE(it, amOn.end())
            << "ON audio map missing key (" << kv.first.first << "," << kv.first.second << ")";
        EXPECT_NEAR(it->second->getStartTime(), kv.second->getStartTime(), 1e-4)
            << "startTime (" << kv.first.first << "," << kv.first.second << ")";
        EXPECT_NEAR(it->second->getDuration(), kv.second->getDuration(), 1e-4)
            << "duration (" << kv.first.first << "," << kv.first.second << ")";
        expectEnvelopeEqual(it->second->getGainEnvelopePoints(),
                            kv.second->getGainEnvelopePoints());
    }

    const auto& mmOn = rOn->getMidiClipSources();
    const auto& mmOff = rOff->getMidiClipSources();
    EXPECT_EQ(mmOn.size(), mmOff.size());
}

// Reference-material stereo WAV: a 440 Hz tone in the left channel and 220 Hz
// in the right. Two tones expose intermodulation/phase artifacts a single sine
// masks. 16-bit, matching the canonical writer pattern.
juce::File writeStereoSineWav(int lengthSamples, double sr)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_ab_source.wav");
    f.deleteFile();
    juce::AudioBuffer<float> buf(2, lengthSamples);
    for (int s = 0; s < lengthSamples; ++s)
    {
        buf.setSample(0, s, static_cast<float>(std::sin(2.0 * 3.14159 * 440.0 * s / sr)));
        buf.setSample(1, s, static_cast<float>(std::sin(2.0 * 3.14159 * 220.0 * s / sr)));
    }
    {
        std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(out.get(), sr, 2, 16, {}, 0));
        out.release();
        writer->writeFromAudioSampleBuffer(buf, 0, lengthSamples);
    }
    return f;
}

// 6-clip OVERLAPPING layout in BEATS (addClips converts to seconds via 60/bpm;
// at the default 120 bpm factor = 0.5): starts {0,0.3,0.6,0.9,1.2,1.5} beat,
// durations 0.8 beat -> seconds {0,0.15,0.3,0.45,0.6,0.75}, dur 0.4. Every
// placement overlaps the previously-added clip, so moveClipWithOverlap trims
// them to adjacency and crossfades appear — exercising the incremental
// crossfade recompute.
void makeAbClipParams(const juce::String& path,
                      std::vector<double>& starts,
                      std::vector<double>& durs,
                      std::vector<std::string>& names,
                      std::vector<std::string>& files)
{
    starts = { 0.0, 0.3, 0.6, 0.9, 1.2, 1.5 };   // beats -> seconds *0.5
    durs   = { 0.8, 0.8, 0.8, 0.8, 0.8, 0.8 };   // beats -> seconds *0.5
    names  = { "A", "B", "C", "D", "E", "F" };
    files.assign(6, path.toStdString());
}

// Bounded spin on an async export's completion (isExporting() flips false at
// the end of renderThreadFunc, after the writer is closed and the file is
// complete). Mirrors the MCP export caller's doneFuture wait.
bool waitForExport(HDAW::ExportManager& em)
{
    constexpr uint32_t kMaxExportWaitMs = 60000;
    const auto deadline = juce::Time::getMillisecondCounter() + kMaxExportWaitMs;
    while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    return !em.isExporting();
}

} // namespace

TEST(IncrementalRoutingAB, ProduceWavPair)
{
    // 1) Reference-material stereo WAV: 44100 Hz, ~4.0 s, 440 Hz left / 220 Hz right.
    const int kSourceSamples = static_cast<int>(44100.0 * 4.0);
    juce::File sourceWav = writeStereoSineWav(kSourceSamples, 44100.0);
    ASSERT_TRUE(sourceWav.existsAsFile());

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File fileIncr = tempDir.getChildFile("hdaw_ab_incremental.wav");
    juce::File fileFull = tempDir.getChildFile("hdaw_ab_fullrebuild.wav");
    fileIncr.deleteFile();
    fileFull.deleteFile();

    // 2) Two engines: incremental path vs full-rebuild path.
    AudioEngine on;
    {
        ScopedIncrementalFlag flag("1");
        on.initialize();
    }
    ASSERT_TRUE(on.isIncrementalRoutingEnabled());
    ASSERT_TRUE(ensureRoutingGraph(on));

    AudioEngine off;
    {
        ScopedIncrementalFlag flag("0");
        off.initialize();
    }
    ASSERT_FALSE(off.isIncrementalRoutingEnabled());
    ASSERT_TRUE(ensureRoutingGraph(off));

    // 3) IDENTICAL edit sequence on both engines, pump parked per command.
    std::vector<double> starts, durs;
    std::vector<std::string> names, files;
    makeAbClipParams(sourceWav.getFullPathName(), starts, durs, names, files);

    std::vector<int> onIds, offIds;
    {
        const juce::MessageManagerLock pumpPark;
        onIds = on.getProjectCommands().addClips(0, starts, durs, names, files);
        ASSERT_EQ(onIds.size(), 6u);
        offIds = off.getProjectCommands().addClips(0, starts, durs, names, files);
        ASSERT_EQ(offIds.size(), 6u);
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    expectSameLiveGraph(on, off);

    // --- Move clip F (index 5) INTO a NEW overlap with clip E: same-track, no
    // reorder, only startTime/duration property changes -> Place ops,
    // incremental-safe. New start 1.3 beats = 0.65s puts F at [0.65, 1.05]
    // overlapping E [0.6, 0.75] (E is trimmed to [0.6, 0.65]) — crossfade
    // recompute. F remains the last-position clip.
    const uint64_t opsBeforeMove = on.debugIncrementalOpsApplied();
    const uint64_t rebuildsBeforeMove = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        on.getProjectCommands().moveClips({ onIds[5] }, { 1.3 }, { 0 });
        EXPECT_GT(on.debugPendingClipOpCount(), 0);
        EXPECT_FALSE(on.debugForceFullRebuildFlag());
        off.getProjectCommands().moveClips({ offIds[5] }, { 1.3 }, { 0 });
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_GT(on.debugIncrementalOpsApplied(), opsBeforeMove);
    EXPECT_EQ(on.debugFullRebuilds(), rebuildsBeforeMove);
    expectSameLiveGraph(on, off);

    // --- Remove clip F (now the last clip): last-position removal is
    // incremental-safe -> single Remove op, no full rebuild.
    const uint64_t opsBeforeRem = on.debugIncrementalOpsApplied();
    const uint64_t rebuildsBeforeRem = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        on.getProjectCommands().removeClips({ onIds[5] });
        EXPECT_GT(on.debugPendingClipOpCount(), 0);
        EXPECT_FALSE(on.debugForceFullRebuildFlag());
        off.getProjectCommands().removeClips({ offIds[5] });
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_GT(on.debugIncrementalOpsApplied(), opsBeforeRem);
    EXPECT_EQ(on.debugFullRebuilds(), rebuildsBeforeRem);
    expectSameLiveGraph(on, off);

    // 4) Render each engine's FINAL project tree through the PRODUCTION export
    // pipeline (pluginManager = nullptr: this project has no plugins). The tree
    // passed must be the project ROOT (getTree()), matching the canonical
    // production caller McpExportTool.cpp:77 — renderThreadFunc rebuilds a
    // ProjectModel by re-parenting the passed tree's children onto the root, so
    // a bare TRACK_LIST yields no TRACK_LIST child and renders silence. Duration
    // covers all clips via calculateProjectDuration (>= 4.0 s).
    juce::AudioFormatManager exportFm;
    exportFm.registerBasicFormats();
    const double duration = HDAW::ExportManager::calculateProjectDuration(on.getProjectModel());

    auto& emOn = on.getMainProcessor()->getExportManager();
    ASSERT_TRUE(emOn.startExport(on.getProjectModel().getTree(), exportFm, nullptr,
                                 fileIncr, 44100.0, 0.0, duration,
                                 HDAW::ExportManager::WAV, 24));
    EXPECT_TRUE(waitForExport(emOn));
    EXPECT_FALSE(emOn.isExporting());
    EXPECT_TRUE(fileIncr.existsAsFile());
    EXPECT_GT(fileIncr.getSize(), 0);

    auto& emOff = off.getMainProcessor()->getExportManager();
    ASSERT_TRUE(emOff.startExport(off.getProjectModel().getTree(), exportFm, nullptr,
                                  fileFull, 44100.0, 0.0, duration,
                                  HDAW::ExportManager::WAV, 24));
    EXPECT_TRUE(waitForExport(emOff));
    EXPECT_FALSE(emOff.isExporting());
    EXPECT_TRUE(fileFull.existsAsFile());
    EXPECT_GT(fileFull.getSize(), 0);

    // 5) Decode both WAVs back and compute the max absolute sample difference
    // across the (identical) length. Both are 24-bit; the render pipeline is
    // identical except for the routing path, so the tolerance is generous to
    // quantization robustness — this is a human A/B artifact check, not the
    // tight equivalence proof (that lives in the IncrementalRouting suites).
    juce::AudioFormatManager readFm;
    readFm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rIncr(readFm.createReaderFor(fileIncr));
    std::unique_ptr<juce::AudioFormatReader> rFull(readFm.createReaderFor(fileFull));
    ASSERT_NE(rIncr, nullptr);
    ASSERT_NE(rFull, nullptr);
    const int64_t numSamples = (std::min)(rIncr->lengthInSamples, rFull->lengthInSamples);
    juce::AudioBuffer<float> bufIncr(2, static_cast<int>(numSamples));
    juce::AudioBuffer<float> bufFull(2, static_cast<int>(numSamples));
    rIncr->read(&bufIncr, 0, static_cast<int>(numSamples), 0, true, true);
    rFull->read(&bufFull, 0, static_cast<int>(numSamples), 0, true, true);

    float maxDiff = 0.0f;
    float peakIncr = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int s = 0; s < static_cast<int>(numSamples); ++s)
        {
            maxDiff = (std::max)(maxDiff,
                                 std::abs(bufIncr.getSample(ch, s) - bufFull.getSample(ch, s)));
            peakIncr = (std::max)(peakIncr, std::abs(bufIncr.getSample(ch, s)));
        }
    }
    std::cout << "[IncrementalRoutingAB] max diff = " << maxDiff << std::endl;
    ASSERT_LT(maxDiff, 1e-4f);
    EXPECT_GT(peakIncr, 0.01f) << "incremental export rendered silence";

    // 6) Print the two absolute output paths for the orchestrator.
    std::cout << "[IncrementalRoutingAB] incremental = " << fileIncr.getFullPathName()
              << std::endl;
    std::cout << "[IncrementalRoutingAB] fullrebuild = " << fileFull.getFullPathName()
              << std::endl;

    // 7) Clean up the source material WAV; leave the two output WAVs in place.
    sourceWav.deleteFile();
}

// T4-G1 — real-device latency probe. On a machine with an audio device, a
// burst of 128 incremental clip adds must NOT shift the device I/O latency,
// the routing graph's total latency (AGENTS.md lesson 7), or the active
// output-channel count, and the OFF (full-rebuild) engine's identical burst
// must report the SAME graph latency as the incremental path's baseline. Skips
// when no audio device is present (device-dependent test discipline).
TEST(IncrementalRoutingAB, RealDeviceLatencyStable)
{
    // 1) ON engine (incremental path) — must have a live device + routing graph.
    AudioEngine on;
    {
        ScopedIncrementalFlag flag("1");
        on.initialize();
    }
    ASSERT_TRUE(on.isIncrementalRoutingEnabled());
    auto* devOn = on.getDeviceManager().getCurrentAudioDevice();
    if (devOn == nullptr)
        GTEST_SKIP() << "no audio device present — skipping real-device latency probe";
    ASSERT_TRUE(ensureRoutingGraph(on));
    ASSERT_NE(on.getMainProcessor()->getRoutingManager(), nullptr);
    ASSERT_TRUE(waitForGraphBake()) << "initial graph bake timed out";

    // 2) Baseline latency triple on the ON engine, printed for the orchestrator.
    const int devLatencyBefore = devOn->getOutputLatencyInSamples();
    const int graphLatencyBefore = on.getMainProcessor()->getRoutingGraphLatencySamples();
    const int numOutBefore = devOn->getOutputChannelNames().size();
    std::cout << "[IncrementalRoutingAB] T4-G1 baseline: deviceLatency=" << devLatencyBefore
              << " graphLatency=" << graphLatencyBefore
              << " outputChannels=" << numOutBefore << std::endl;

    // 3) Small sine WAV the burst clips reference (0.2 s at 44.1 kHz).
    juce::File sourceWav = writeSineWav(static_cast<int>(44100.0 * 0.2), 44100.0);
    ASSERT_TRUE(sourceWav.existsAsFile());

    // 4) Burst of incremental adds — tiny non-overlapping clips in beats
    //    (addClips converts via 60/bpm = 0.5 at 120 bpm → seconds; 0.02 beat
    //    spacing vs 0.01 beat duration guarantees no overlap, so the burst is
    //    append-only incremental). ONE batched addClips command under a single
    //    parked pump → one drain → one message-loop tick. Deliberately BELOW
    //    the lesson-6 cliff (128+ clips → ~30s+ per full bake): each
    //    incremental op still bakes the whole graph synchronously on the
    //    message thread, so N per-op commands would stall for minutes. The
    //    latency-equivalence claim needs a real burst, not a stall.
    constexpr int kNumClips = 32;
    std::vector<double> starts, durs;
    std::vector<std::string> names, files;
    for (int i = 0; i < kNumClips; ++i)
    {
        starts.push_back(static_cast<double>(i) * 0.02);
        durs.push_back(0.01);
        names.push_back("lat" + std::to_string(i));
        files.push_back(sourceWav.getFullPathName().toStdString());
    }
    {
        const juce::MessageManagerLock pumpPark;
        auto ids = on.getProjectCommands().addClips(0, starts, durs, names, files);
        ASSERT_EQ(ids.size(), static_cast<size_t>(kNumClips));
    }
    on.drainPendingRoutingRebuild();
    ASSERT_TRUE(waitForGraphBake()) << "post-burst graph bake timed out";

    // 5) Re-read the same triple on the ON engine.
    auto* devOnAfter = on.getDeviceManager().getCurrentAudioDevice();
    ASSERT_NE(devOnAfter, nullptr);
    const int devLatencyAfter = devOnAfter->getOutputLatencyInSamples();
    const int graphLatencyAfter = on.getMainProcessor()->getRoutingGraphLatencySamples();
    const int numOutAfter = devOnAfter->getOutputChannelNames().size();

    // 6) Core lesson-7 assertions: device I/O latency, graph topology latency,
    //    and output-channel count must not shift; the burst must have landed.
    EXPECT_EQ(devLatencyBefore, devLatencyAfter)
        << "device I/O latency shifted across the incremental burst";
    EXPECT_EQ(graphLatencyBefore, graphLatencyAfter)
        << "routing graph latency shifted across the incremental burst (lesson 7)";
    EXPECT_EQ(numOutBefore, numOutAfter) << "output channel count changed";
    ASSERT_NE(on.getMainProcessor()->getRoutingManager(), nullptr);
    EXPECT_FALSE(on.getMainProcessor()->getRoutingManager()->getAudioClipSources().empty())
        << "the burst did not land on the live graph";

    // 7) Cross-check vs the full-rebuild path: same burst on an OFF
    //    engine, settled via the explicit full rebuild, must report the SAME
    //    graph latency as the incremental baseline.
    AudioEngine off;
    {
        ScopedIncrementalFlag flag("0");
        off.initialize();
    }
    ASSERT_FALSE(off.isIncrementalRoutingEnabled());
    auto* devOff = off.getDeviceManager().getCurrentAudioDevice();
    if (devOff == nullptr)
        GTEST_SKIP() << "no audio device present — skipping real-device latency probe (OFF)";
    ASSERT_TRUE(ensureRoutingGraph(off));
    {
        const juce::MessageManagerLock pumpPark;
        auto ids = off.getProjectCommands().addClips(0, starts, durs, names, files);
        ASSERT_EQ(ids.size(), static_cast<size_t>(kNumClips));
    }
    settleOffEngine(off);
    ASSERT_TRUE(waitForGraphBake()) << "OFF graph bake timed out";
    const int graphLatencyOff = off.getMainProcessor()->getRoutingGraphLatencySamples();
    EXPECT_EQ(graphLatencyOff, graphLatencyBefore)
        << "full-rebuild path graph latency diverged from the incremental baseline";

    // 8) Final summary line for the orchestrator.
    std::cout << "[IncrementalRoutingAB] T4-G1 latency: device before=" << devLatencyBefore
              << " after=" << devLatencyAfter
              << " graph before=" << graphLatencyBefore
              << " after=" << graphLatencyAfter
              << " outputChannels=" << numOutAfter
              << " offGraphLatency=" << graphLatencyOff << std::endl;

    // 9) Clean up the source WAV.
    sourceWav.deleteFile();
}