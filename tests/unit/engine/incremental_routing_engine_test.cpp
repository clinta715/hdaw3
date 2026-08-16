#include <gtest/gtest.h>
#include "render_harness.h"
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/RoutingManager.h"
#include "engine/ClipSourceProcessor.h"
#include "engine/MidiClipProcessor.h"
#include "model/ProjectModel.h"

#include <cstdlib>
#include <string>
#include <vector>

// Task 3 incremental-routing tests: the AudioEngine coalescing seam
// (HDAW_FORCE_INCREMENTAL_ROUTING flag, pending-op queue captured at
// ValueTree-listener time, drained under one graphLock hold in
// AudioEngine::drainPendingClipOps). Gates T3-G1 (add), T3-G2 (place),
// T3-G3 (remove + undo/redo), T3-G4 (flag plumbing). Every equivalence test
// runs the SAME commands through two engines — one with the flag ON (the
// incremental drain path) and one with the flag OFF (the pre-existing full
// rebuildRoutingGraph path, forced to a settled full rebuild per step) — and
// compares the LIVE processors (startTime/duration/gain-envelope) plus a
// RenderHarness render of the resulting track-0 CLIP_LIST.
//
// The env flag is read ONCE in AudioEngine::initialize(); ScopedIncrementalFlag
// sets it around engine construction + initialize() and restores the previous
// value. Each engine also needs a live RoutingManager: if the environment has
// no audio device, ensureRoutingGraph bootstraps one via
// MainAudioProcessor::prepareToPlay under a parked pump + bake-wait (the
// RenderHarness pattern); if a device IS present, prepareToPlay already ran on
// the device callback and the routing graph exists. Either way both engines
// end with a comparable live graph, so the tests are environment-independent.
//
// FlagOnCrossTrackMoveEquivalent extends T3-G2 to a cross-track move of a
// track's LAST clip (Remove on source + append-Add on dest + Place): the
// structural gates only inspect last-position/append, never track identity, so
// the move rides the incremental path — proven equivalent to a full rebuild on
// both the source and destination tracks.
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
    // Settle the graph-internal render-sequence bake via the engine's
    // exactly-once message-thread drain (the AudioEngine analogue of
    // RenderHarness::waitForBake).
    engine.drainPendingRoutingRebuild();
    return proc->getRoutingManager() != nullptr;
}

// Settles the OFF (full-rebuild reference) engine to a state reflecting the
// current ValueTree, free of pump-thread races: drain any coalesced async
// rebuild that the commands queued (add/remove fire triggerAsyncUpdate), then
// force an explicit full rebuild (moves only mutate placement properties, which
// never queue an async rebuild — see AudioEngine::valueTreePropertyChanged),
// then drain again so the graph-internal bake lands before live processors are
// read. Mirrors the audio_pool_dedup pattern (rebuildRoutingGraph + drain).
void settleOffEngine(AudioEngine& off)
{
    off.drainPendingRoutingRebuild();
    off.getMainProcessor()->rebuildRoutingGraph();
    off.drainPendingRoutingRebuild();
}

// Compares the LIVE audio-clip processors of two engines: same map size, same
// (trackIndex, clipIndex) keys, per-audio startTime/duration within SPSC float
// precision, and gain-envelope EXACTLY equal. The envelope equality is the
// critical crossfade check — a missed crossfade recompute on either side shows
// up here. startTime/duration use a 1e-4 tolerance (not EXPECT_DOUBLE_EQ)
// because when a live audio device is present, the audio thread's SPSC pop
// (ParamUpdate::value is float) can overwrite the drain's exact double after
// the Place op — a pre-existing float-precision trait of the SPSC path, not a
// Task 3 regression; the render comparison below is the strict equivalence
// proof.
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

// Renders each engine's CLIP_LIST for the given track (default 0) through an
// independent RenderHarness (identical build/render path) and asserts the two
// renders agree to 1e-6. RenderHarness::init copies the given CLIP_LIST into
// the harness's track 0, so passing a destination-track CLIP_LIST compares the
// moved clip's target track correctly.
void expectSameRender(AudioEngine& on, AudioEngine& off, int trackIndex = 0)
{
    auto onClips = on.getProjectModel().getTrackListTree()
                       .getChild(trackIndex).getChildWithName(IDs::CLIP_LIST);
    auto offClips = off.getProjectModel().getTrackListTree()
                        .getChild(trackIndex).getChildWithName(IDs::CLIP_LIST);
    ASSERT_TRUE(onClips.isValid());
    ASSERT_TRUE(offClips.isValid());

    RenderHarness hOn;
    hOn.init(onClips);
    hOn.build();
    RenderHarness hOff;
    hOff.init(offClips);
    hOff.build();

    const int numBlocks = blocksFor(7.0);
    juce::AudioBuffer<float> outOn(2, numBlocks * RenderHarness::kBlockSize);
    juce::AudioBuffer<float> outOff(2, numBlocks * RenderHarness::kBlockSize);
    float maxDiff = maxAbsDiff(hOn, hOff, numBlocks, outOn, outOff);
    EXPECT_LT(maxDiff, 1e-6f) << "ON vs OFF engine renders diverged";

    hOn.shutdown();
    hOff.shutdown();
}

// Builds a 6-clip overlapping layout in BEATS (addClips converts to seconds via
// 60/bpm; at the default 120 bpm factor = 0.5). Seconds are the same layout as
// the Task 2 remove/move tests: {0, 0.9, 2.5, 3.4, 5.0, 5.4}, dur 1.0. The
// pairs (0,1), (2,3), (4,5) overlap so addClips' moveClipWithOverlap trims them
// to adjacency and crossfades appear — exercising the incremental crossfade
// recompute.
void makeSixClipParams(const juce::String& path,
                       std::vector<double>& starts,
                       std::vector<double>& durs,
                       std::vector<std::string>& names,
                       std::vector<std::string>& files)
{
    starts = { 0.0, 1.8, 5.0, 6.8, 10.0, 10.8 };   // seconds /0.5
    durs   = { 2.0, 2.0, 2.0, 2.0, 2.0, 2.0 };     // seconds /0.5 -> 1.0
    names  = { "A", "B", "C", "D", "E", "F" };
    files.assign(6, path.toStdString());
}

} // namespace

// T3-G4 — the flag is read once at initialize(): "1"/"true" enable the
// incremental path, "0"/"false"/"" leave it off (default).
TEST(IncrementalRoutingEngine, FlagPlumbingReadOnceAtStartup)
{
    {
        ScopedIncrementalFlag flag("1");
        AudioEngine engine;
        engine.initialize();
        EXPECT_TRUE(engine.isIncrementalRoutingEnabled());
    }
    {
        ScopedIncrementalFlag flag("true");
        AudioEngine engine;
        engine.initialize();
        EXPECT_TRUE(engine.isIncrementalRoutingEnabled());
    }
    {
        ScopedIncrementalFlag flag("0");
        AudioEngine engine;
        engine.initialize();
        EXPECT_FALSE(engine.isIncrementalRoutingEnabled());
    }
    {
        ScopedIncrementalFlag flag("false");
        AudioEngine engine;
        engine.initialize();
        EXPECT_FALSE(engine.isIncrementalRoutingEnabled());
    }
    {
        ScopedIncrementalFlag flag("FALSE");
        AudioEngine engine;
        engine.initialize();
        EXPECT_FALSE(engine.isIncrementalRoutingEnabled());
    }
    {
        ScopedIncrementalFlag flag("");
        AudioEngine engine;
        engine.initialize();
        EXPECT_FALSE(engine.isIncrementalRoutingEnabled());
    }
}

// Flag OFF → the incremental machinery is a zero-change no-op: the queue is
// never touched, the counters never move, and the pre-existing full-rebuild
// path (handleAsyncUpdate → rebuildRoutingGraph) still lands all 16 clips on
// the live graph. An explicit rebuildRoutingGraph stays idempotent.
TEST(IncrementalRoutingEngine, FlagOffPathIsUnchangedFullRebuild)
{
    ScopedIncrementalFlag flag("0");
    AudioEngine engine;
    engine.initialize();
    ASSERT_FALSE(engine.isIncrementalRoutingEnabled());
    ASSERT_TRUE(ensureRoutingGraph(engine));

    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    std::vector<double> starts, durs;
    std::vector<std::string> names, files;
    for (int i = 0; i < 16; ++i)
    {
        starts.push_back(static_cast<double>(i) * 1.0);  // i beats -> i*0.5s
        durs.push_back(0.8);                              // 0.4s
        names.push_back("c" + std::to_string(i));
        files.push_back(file.getFullPathName().toStdString());
    }

    auto ids = engine.getProjectCommands().addClips(0, starts, durs, names, files);
    ASSERT_EQ(ids.size(), 16u);

    engine.drainPendingRoutingRebuild();

    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    EXPECT_EQ(rm->getAudioClipSources().size(), 16u);

    // Zero-change proof: none of the incremental machinery ran.
    EXPECT_EQ(engine.debugPendingClipOpCount(), 0);
    EXPECT_FALSE(engine.debugForceFullRebuildFlag());
    EXPECT_EQ(engine.debugIncrementalOpsApplied(), 0u);
    EXPECT_EQ(engine.debugFullRebuilds(), 0u);

    // The OFF path is still a plain full rebuild, idempotent on repeat.
    engine.getMainProcessor()->rebuildRoutingGraph();
    auto* rm2 = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm2, nullptr);
    EXPECT_EQ(rm2->getAudioClipSources().size(), 16u);

    file.deleteFile();
}

// T3-G1 — flag ON: a batched addClips (6 overlapping clips, which also fires
// placement/trim Place ops for the trimmed siblings) drains INCREMENTALLY
// (queue > 0 before the drain, full-rebuild counter flat, ops counter == the
// queued count) and lands the same live graph + render as the OFF engine's
// settled full rebuild.
TEST(IncrementalRoutingEngine, FlagOnBatchAddEquivalent)
{
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

    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    std::vector<double> starts, durs;
    std::vector<std::string> names, files;
    makeSixClipParams(file.getFullPathName(), starts, durs, names, files);

    int queuedOn = 0;
    {
        const juce::MessageManagerLock pumpPark;
        auto onIds = on.getProjectCommands().addClips(0, starts, durs, names, files);
        ASSERT_EQ(onIds.size(), 6u);
        // Pump parked -> the async drain cannot run yet: the queue must hold
        // the captured ops and the batch must NOT have forced a full rebuild.
        queuedOn = on.debugPendingClipOpCount();
        EXPECT_GT(queuedOn, 0) << "incremental add must enqueue ops";
        EXPECT_FALSE(on.debugForceFullRebuildFlag());
    }
    on.drainPendingRoutingRebuild();
    EXPECT_EQ(on.debugPendingClipOpCount(), 0);
    EXPECT_EQ(on.debugIncrementalOpsApplied(),
              static_cast<uint64_t>(queuedOn))
        << "incremental drain must apply exactly the queued ops";
    EXPECT_EQ(on.debugFullRebuilds(), 0u);

    {
        const juce::MessageManagerLock pumpPark;
        auto offIds = off.getProjectCommands().addClips(0, starts, durs, names, files);
        ASSERT_EQ(offIds.size(), 6u);
    }
    settleOffEngine(off);

    expectSameLiveGraph(on, off);
    expectSameRender(on, off);

    file.deleteFile();
}

// T3-G2 + T3-G3 — flag ON: a same-track move into a NEW overlap (Place ops
// only, incremental-safe), a middle remove (structural → full-rebuild
// fallback), and a last-position remove (incremental-safe) each land the same
// live graph + render as the OFF engine's settled full rebuild, with the debug
// seams proving the right branch was taken at each step.
TEST(IncrementalRoutingEngine, FlagOnRemoveMoveEquivalent)
{
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

    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    std::vector<double> starts, durs;
    std::vector<std::string> names, files;
    makeSixClipParams(file.getFullPathName(), starts, durs, names, files);

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

    // --- Move clip F (index 5) into a NEW overlap with clip B (index 1):
    // same-track, no reorder, only startTime/duration property changes →
    // Place ops, incremental-safe. New start 2.8 beats = 1.4s puts F at
    // [1.4, 2.4] overlapping B [0.9, 1.9] (B is trimmed to [0.9, 1.4]).
    const uint64_t opsBeforeMove = on.debugIncrementalOpsApplied();
    const uint64_t rebuildsBeforeMove = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        on.getProjectCommands().moveClips({ onIds[5] }, { 2.8 }, { 0 });
        EXPECT_GT(on.debugPendingClipOpCount(), 0);
        EXPECT_FALSE(on.debugForceFullRebuildFlag());
        off.getProjectCommands().moveClips({ offIds[5] }, { 2.8 }, { 0 });
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_GT(on.debugIncrementalOpsApplied(), opsBeforeMove);
    EXPECT_EQ(on.debugFullRebuilds(), rebuildsBeforeMove);
    expectSameLiveGraph(on, off);
    expectSameRender(on, off);

    // --- Remove clip C (index 2): NOT the last position → structural → the
    // drain must fall back to a full rebuildRoutingGraph (queue discarded).
    const uint64_t opsBeforeRemC = on.debugIncrementalOpsApplied();
    const uint64_t rebuildsBeforeRemC = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        on.getProjectCommands().removeClips({ onIds[2] });
        EXPECT_TRUE(on.debugForceFullRebuildFlag())
            << "middle remove must be flagged structural";
        off.getProjectCommands().removeClips({ offIds[2] });
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_EQ(on.debugIncrementalOpsApplied(), opsBeforeRemC)
        << "structural drain must discard the queue (no incremental ops)";
    EXPECT_GT(on.debugFullRebuilds(), rebuildsBeforeRemC);
    EXPECT_FALSE(on.debugForceFullRebuildFlag());
    expectSameLiveGraph(on, off);
    expectSameRender(on, off);

    // --- Remove clip F (now the last clip): last-position removal is
    // incremental-safe → single Remove op, no full rebuild.
    const uint64_t opsBeforeRemF = on.debugIncrementalOpsApplied();
    const uint64_t rebuildsBeforeRemF = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        on.getProjectCommands().removeClips({ onIds[5] });
        EXPECT_GT(on.debugPendingClipOpCount(), 0);
        EXPECT_FALSE(on.debugForceFullRebuildFlag());
        off.getProjectCommands().removeClips({ offIds[5] });
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_GT(on.debugIncrementalOpsApplied(), opsBeforeRemF);
    EXPECT_EQ(on.debugFullRebuilds(), rebuildsBeforeRemF);
    expectSameLiveGraph(on, off);
    expectSameRender(on, off);

    file.deleteFile();
}

// T3-G3 — flag ON: undo of a batch add (removes clips last-first, each removal
// at the last position → incremental-safe) and redo (re-appends → safe) land
// the same live graph + render as the OFF engine's settled full rebuild. The
// full round-trip never forces a structural rebuild.
TEST(IncrementalRoutingEngine, FlagOnUndoRedoEquivalent)
{
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

    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    std::vector<double> starts, durs;
    std::vector<std::string> names, files;
    makeSixClipParams(file.getFullPathName(), starts, durs, names, files);

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

    // Undo the whole addClips transaction (removes last-first → each removal is
    // at the last position → incremental-safe).
    const uint64_t rebuildsBeforeUndo = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        while (on.getProjectCommands().canUndo())
            on.getProjectCommands().undo();
        EXPECT_FALSE(on.debugForceFullRebuildFlag());
        while (off.getProjectCommands().canUndo())
            off.getProjectCommands().undo();
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_EQ(on.debugFullRebuilds(), rebuildsBeforeUndo)
        << "undo must be fully incremental (no structural fallback)";
    expectSameLiveGraph(on, off);
    expectSameRender(on, off);

    // Redo (re-appends all clips → incremental-safe).
    const uint64_t rebuildsBeforeRedo = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        while (on.getProjectCommands().canRedo())
            on.getProjectCommands().redo();
        EXPECT_FALSE(on.debugForceFullRebuildFlag());
        while (off.getProjectCommands().canRedo())
            off.getProjectCommands().redo();
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_EQ(on.debugFullRebuilds(), rebuildsBeforeRedo)
        << "redo must be fully incremental (no structural fallback)";
    expectSameLiveGraph(on, off);
    expectSameRender(on, off);

    file.deleteFile();
}

// Track add/remove are structural by definition — the listener sets the
// forceFull flag and the drain falls back to rebuildRoutingGraph (queue
// discarded). This proves the structural-detection path, and that a
// track-level op is never routed through the incremental branch.
TEST(IncrementalRoutingEngine, FlagOnStructuralForcesFullRebuild)
{
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

    // Track add → forceFull.
    const uint64_t opsBeforeAdd = on.debugIncrementalOpsApplied();
    const uint64_t rebuildsBeforeAdd = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        int tOn = on.getProjectCommands().addTrack("T2", 0, -1, 0);
        EXPECT_GE(tOn, 0);
        EXPECT_TRUE(on.debugForceFullRebuildFlag())
            << "track add must be flagged structural";
        int tOff = off.getProjectCommands().addTrack("T2", 0, -1, 0);
        EXPECT_GE(tOff, 0);
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_EQ(on.debugIncrementalOpsApplied(), opsBeforeAdd)
        << "track add must not use the incremental queue";
    EXPECT_GT(on.debugFullRebuilds(), rebuildsBeforeAdd);
    EXPECT_EQ(on.getMainProcessor()->getRoutingManager()->getAudioClipSources().size(),
              off.getMainProcessor()->getRoutingManager()->getAudioClipSources().size());

    // Track remove → forceFull.
    const uint64_t opsBeforeRem = on.debugIncrementalOpsApplied();
    const uint64_t rebuildsBeforeRem = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        on.getProjectCommands().removeTrack(0);
        EXPECT_TRUE(on.debugForceFullRebuildFlag())
            << "track remove must be flagged structural";
        off.getProjectCommands().removeTrack(0);
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_EQ(on.debugIncrementalOpsApplied(), opsBeforeRem)
        << "track remove must not use the incremental queue";
    EXPECT_GT(on.debugFullRebuilds(), rebuildsBeforeRem);
    EXPECT_EQ(on.getMainProcessor()->getRoutingManager()->getAudioClipSources().size(),
              off.getMainProcessor()->getRoutingManager()->getAudioClipSources().size());
}

// T3-G2 extension — a cross-track move of a track's LAST clip rides the
// incremental path: the source removal is a last-position remove (not
// structural), the dest add is an append (not structural), and the startTime
// set is a Place op. The structural gates in AudioEngine::valueTreeChildAdded/
// Removed only inspect last-position/append, never track identity, so nothing
// flags the move. This asserts exactly that — and proves the incremental drain
// lands the same live graph + renders as the OFF engine's settled full rebuild
// on BOTH the source track (A-E remain, crossfade with F recomputed away) and
// the destination track (F alone at the new start).
TEST(IncrementalRoutingEngine, FlagOnCrossTrackMoveEquivalent)
{
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

    auto file = writeSineWav(static_cast<int>(2.0 * 44100.0), 44100.0);

    // Set-up: an empty destination track for F to land on. The track add is
    // structural on the ON engine (forceFull) — drain it before the
    // incremental assertions below so the counters start clean.
    int destTrack = -1;
    {
        const juce::MessageManagerLock pumpPark;
        destTrack = on.getProjectCommands().addTrack("Dest", 0, -1, 0);
        ASSERT_GE(destTrack, 0);
        int destOff = off.getProjectCommands().addTrack("Dest", 0, -1, 0);
        ASSERT_GE(destOff, 0);
        EXPECT_EQ(destOff, destTrack) << "both engines must agree on dest index";
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);

    std::vector<double> starts, durs;
    std::vector<std::string> names, files;
    makeSixClipParams(file.getFullPathName(), starts, durs, names, files);

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

    // --- Cross-track move: clip F (index 5, the LAST clip on track 0) moves
    // onto the empty dest track at beat 0.0 (any start works — nothing to
    // overlap there). The plan doc claimed cross-track moves route to a full
    // rebuild; the structural gates below prove the code does NOT flag it.
    const uint64_t opsBeforeMove = on.debugIncrementalOpsApplied();
    const uint64_t rebuildsBeforeMove = on.debugFullRebuilds();
    {
        const juce::MessageManagerLock pumpPark;
        on.getProjectCommands().moveClips({ onIds[5] }, { 0.0 }, { destTrack });
        EXPECT_GT(on.debugPendingClipOpCount(), 0);
        EXPECT_FALSE(on.debugForceFullRebuildFlag())
            << "last-position cross-track move must NOT be flagged structural";
        off.getProjectCommands().moveClips({ offIds[5] }, { 0.0 }, { destTrack });
    }
    on.drainPendingRoutingRebuild();
    settleOffEngine(off);
    EXPECT_GT(on.debugIncrementalOpsApplied(), opsBeforeMove)
        << "cross-track move must ride the incremental drain";
    EXPECT_EQ(on.debugFullRebuilds(), rebuildsBeforeMove)
        << "cross-track move must not force a structural fallback";
    expectSameLiveGraph(on, off);
    expectSameRender(on, off);
    expectSameRender(on, off, destTrack);

    file.deleteFile();
}