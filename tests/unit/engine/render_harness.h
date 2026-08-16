#pragma once
#include <gtest/gtest.h>
// Shared standalone-graph render harness for the incremental-routing tests
// (IncrementalRoutingSpike, IncrementalRoutingRemoveMove). Extracted from the
// Task 1 spike test so both suites exercise the identical build/render path.
//
// Threading contract (see the spike recommendation, pitfalls 1-2):
//  * Every graph mutation (addNode/addConnection/removeNode/removeConnection,
//    rebuild, clear) must park the JUCE message pump via MessageManagerLock
//    when the calling thread is not the pump thread, exactly like
//    MainAudioProcessor::rebuildRoutingGraph (needsPark, MainAudioProcessor.cpp).
//  * MessageManagerLock does NOT drain the pump — the graph's render-sequence
//    bake is serviced asynchronously by the pump thread. Call waitForBake()
//    after any topology mutation before the first processBlock, and at
//    shutdown().
//  * shutdown() must be called explicitly: parked teardown (routing.reset() +
//    graph.clear() under the lock, then bake-wait). Without it, a queued
//    graph-internal rebuild message can iterate freed nodes on the pump thread
//    and the heap corruption surfaces in LATER tests.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "engine/RoutingManager.h"
#include "engine/TransportManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

// Writes a stereo sine .wav to a temp file and returns its path.
juce::File writeSineWav(int lengthSamples, double sr)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_incremental_routing_spike.wav");
    f.deleteFile();
    juce::AudioBuffer<float> buf(2, lengthSamples);
    for (int s = 0; s < lengthSamples; ++s)
    {
        float v = static_cast<float>(std::sin(2.0 * 3.14159 * 440.0 * s / sr));
        buf.setSample(0, s, v);
        buf.setSample(1, s, v);
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

// Standalone-graph harness mirroring ExportManager's render pipeline
// (ExportManager.cpp:158-291). The graph is NOT connected to any audio
// device; processBlock is driven manually block-by-block with an explicit
// transport advance, in non-realtime mode.
//
// Threading: tests run on the MAIN thread; the JUCE message pump runs on a
// SEPARATE thread (MessagePumpThread). Every graph mutation
// (addNode/addConnection with UpdateKind::sync) therefore degrades to an
// async rebuild serviced by the pump thread, so a bake-wait probe is required
// after any topology change BEFORE the first processBlock (the same contract
// ExportManager enforces).
struct RenderHarness
{
    juce::AudioFormatManager formatManager;
    ProjectModel model;
    HDAW::TransportManager transport;
    HDAW::InternalPlayHead playHead;
    juce::AudioProcessorGraph graph;
    std::unique_ptr<HDAW::RoutingManager> routing;

    static constexpr double kSampleRate = 44100.0;
    static constexpr int kBlockSize = 512;

    RenderHarness()
        : playHead(transport)
    {
        formatManager.registerBasicFormats();
    }

    // Teardown must park the pump, exactly like the mutations. The graph's
    // internal LockingAsyncUpdater dispatches topology changes on the message
    // thread; destroying RoutingManager's node refs and then the graph on the
    // test thread while a queued graph-internal rebuild message is still
    // pending frees nodes under the pump's dispatch (lesson 12). So: clear the
    // graph under a MessageManagerLock, then drain the pump to an idle state
    // BEFORE the members go out of scope. All tests end by calling this.
    void shutdown()
    {
        if (routing == nullptr)
            return;
        {
            const juce::MessageManagerLock pumpPark;
            routing.reset();
            graph.clear();
        }
        waitForBake();
    }

    // Populates the harness against the given track-0 clip layout (all other
    // project state comes from the default project: 3 tracks, master bus).
    void init(juce::ValueTree track0Clips)
    {
        model.createDefaultProject();
        auto trackList = model.getTrackListTree();
        auto track0 = trackList.getChild(0);
        auto clipList = track0.getChildWithName(IDs::CLIP_LIST);
        clipList.removeAllChildren(nullptr);
        for (int i = 0; i < track0Clips.getNumChildren(); ++i)
            clipList.addChild(track0Clips.getChild(i).createCopy(), -1, nullptr);

        transport.setSampleRate(kSampleRate);
        transport.setBPM(model.getTree().getProperty(IDs::tempo, 120.0));
        transport.setPlaying(true);
        transport.setCurrentSample(0);

        graph.setPlayHead(&playHead);

        // Propagate a stereo bus layout to the graph BEFORE rebuilding: the
        // graph's audioOutputNode reads its input-channel count from the
        // graph's own output bus. Must run before rebuildFromValueTree so the
        // IO node is created with the correct channel count.
        {
            juce::AudioProcessorGraph::BusesLayout renderLayout;
            renderLayout.inputBuses.add(juce::AudioChannelSet::stereo());
            renderLayout.outputBuses.add(juce::AudioChannelSet::stereo());
            graph.setBusesLayout(renderLayout);
        }

        routing = std::make_unique<HDAW::RoutingManager>(graph, model, formatManager,
                                                         transport, nullptr, nullptr,
                                                         nullptr, nullptr);
        routing->setPlaybackInfo(kSampleRate, kBlockSize);
    }

    // Full-rebuild path: rebuildFromValueTree + reconnect + prepareToPlay +
    // non-realtime, then a bounded bake-wait. Mirrors the production mutation
    // surface (MainAudioProcessor::rebuildRoutingGraph under graphLock) plus
    // the export-only non-realtime setup.
    //
    // CRITICAL: every graph mutation here (and addClip / rebuild calls in the
    // tests) must park the pump. The pump thread concurrently dispatches
    // AudioProcessorGraph's internal async-rebuild messages
    // (Pimpl::handleAsyncUpdate iterates the LIVE node list); a clear()+re-add
    // on this thread while a queued graph-internal message is mid-flight frees
    // nodes under it -> use-after-free. Production rebuildRoutingGraph does the
    // same (MessageManagerLock, needsPark, MainAudioProcessor.cpp:620-635);
    // tests run on the main thread (not the pump thread), so taking the lock
    // here is legal and self-deadlock-free.
    void build()
    {
        {
            const juce::MessageManagerLock pumpPark;
            routing->rebuildFromValueTree();
            routing->reconnectMasterToOutput();
            graph.prepareToPlay(kSampleRate, kBlockSize);
        }
        graph.setNonRealtime(true);
        routing->setClipSourcesNonRealtime(true);
        EXPECT_TRUE(waitForBake()) << "render graph bake timed out";
    }

    // Incremental mutation on a live prepared graph — parks the pump (same
    // reason as build()), matching where production addClip will be called.
    void addClipParked(int trackIndex, int clipIndex, const juce::ValueTree& clipTree)
    {
        const juce::MessageManagerLock pumpPark;
        routing->addClip(trackIndex, clipIndex, clipTree);
    }

    // Timed full rebuild — the graphLock-hold surface in production is
    // MessageManagerLock + rebuildFromValueTree + prepareToPlay + reconnect.
    // Time ONLY the mutation call, not the pump-park round-trip (that is a
    // fixed per-op overhead of the message loop, identical for both paths, and
    // would otherwise dominate and swamp the comparison).
    double timedFullRebuild()
    {
        using Clock = std::chrono::steady_clock;
        using MS = std::chrono::duration<double, std::milli>;
        const juce::MessageManagerLock pumpPark;
        auto t0 = Clock::now();
        routing->rebuildFromValueTree();
        return std::chrono::duration_cast<MS>(Clock::now() - t0).count();
    }

    // Timed incremental addClip under the same park — same convention.
    double timedAddClip(int trackIndex, int clipIndex, const juce::ValueTree& clipTree)
    {
        using Clock = std::chrono::steady_clock;
        using MS = std::chrono::duration<double, std::milli>;
        const juce::MessageManagerLock pumpPark;
        auto t0 = Clock::now();
        routing->addClip(trackIndex, clipIndex, clipTree);
        return std::chrono::duration_cast<MS>(Clock::now() - t0).count();
    }

    // After a topology mutation that happened while this thread is NOT the
    // message thread, the graph's render-sequence rebuild is serviced
    // asynchronously by the pump thread. Post a probe callback after the
    // graph's own queued updater messages (FIFO), then wait a bounded
    // deadline for it to fire; when it fires, every earlier queued message —
    // including the render-sequence bake — has been processed.
    bool waitForBake()
    {
        auto bakeLanded = std::make_shared<std::atomic<bool>>(false);
        struct BakeProbeMessage final : public juce::CallbackMessage {
            std::shared_ptr<std::atomic<bool>> landed;
            explicit BakeProbeMessage(std::shared_ptr<std::atomic<bool>> f)
                : landed(std::move(f)) {}
            void messageCallback() override
            {
                landed->store(true, std::memory_order_release);
            }
        };
        auto* probe = new BakeProbeMessage(bakeLanded);
        probe->post();

        constexpr uint32_t kMaxBakeWaitMs = 15000;
        const auto deadline = juce::Time::getMillisecondCounter() + kMaxBakeWaitMs;
        while (!bakeLanded->load(std::memory_order_acquire)
               && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep(10);
        return bakeLanded->load(std::memory_order_acquire);
    }

    // Renders `numBlocks` blocks from transport position 0 into `out`
    // (2 x numBlocks*kBlockSize), advancing the transport per block. The
    // graph was prepared with kBlockSize, so processBlock must receive
    // exactly one block at a time (passing the whole output buffer would make
    // the graph process numBlocks*kBlockSize samples per call while the
    // transport advances only kBlockSize — a desync that also reads far past
    // short clips).
    void render(int numBlocks, juce::AudioBuffer<float>& out)
    {
        transport.setCurrentSample(0);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> blockBuf(2, kBlockSize);
        for (int b = 0; b < numBlocks; ++b)
        {
            blockBuf.clear();
            midi.clear();
            graph.processBlock(blockBuf, midi);
            out.copyFrom(0, b * kBlockSize, blockBuf, 0, 0, kBlockSize);
            out.copyFrom(1, b * kBlockSize, blockBuf, 1, 0, kBlockSize);
            transport.advance(kBlockSize);
        }
    }
};

// Builds a track-0 CLIP_LIST ValueTree holding `numClips` audio clips that
// reference `file`. `startAt[i]`/`dur[i]` give per-clip placement; when empty,
// clips are laid out at start = i*0.5, duration 0.4 (non-overlapping).
juce::ValueTree makeClipList(const juce::File& file, int numClips,
                             const std::vector<double>& startAt = {},
                             const std::vector<double>& durations = {})
{
    juce::ValueTree list(IDs::CLIP_LIST);
    for (int i = 0; i < numClips; ++i)
    {
        double start = startAt.empty() ? i * 0.5 : startAt[static_cast<size_t>(i)];
        double dur = durations.empty() ? 0.4 : durations[static_cast<size_t>(i)];
        auto clip = ProjectModel::createAudioClip(
            "c" + juce::String(i), start, dur, file.getFullPathName());
        list.addChild(clip, -1, nullptr);
    }
    return list;
}

juce::ValueTree makeDefaultClipList()
{
    return juce::ValueTree(IDs::CLIP_LIST);
}

// Maximum absolute sample difference between two renders of the same layout,
// returned via outA/outB (both 2 x totalSamples).
float maxAbsDiff(RenderHarness& a, RenderHarness& b, int numBlocks,
                 juce::AudioBuffer<float>& outA, juce::AudioBuffer<float>& outB)
{
    a.render(numBlocks, outA);
    b.render(numBlocks, outB);
    float maxDiff = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < numBlocks * RenderHarness::kBlockSize; ++s)
            maxDiff = (std::max)(maxDiff,
                                 std::abs(outA.getSample(ch, s) - outB.getSample(ch, s)));
    return maxDiff;
}

// Number of blocks needed to cover `totalSeconds` at the harness rate.
int blocksFor(double totalSeconds)
{
    return static_cast<int>(std::ceil(totalSeconds * RenderHarness::kSampleRate
                                      / RenderHarness::kBlockSize));
}

// Compares two GainPoint vectors element-for-element (times to 1e-6, gains
// exactly). Used by the remove/move tests to assert sibling crossfade
// equivalence on LIVE processors.
void expectEnvelopeEqual(const std::vector<HDAW::ClipSourceProcessor::GainPoint>& a,
                         const std::vector<HDAW::ClipSourceProcessor::GainPoint>& b)
{
    EXPECT_EQ(a.size(), b.size());
    for (size_t i = 0; i < b.size(); ++i)
    {
        EXPECT_NEAR(a[i].time, b[i].time, 1e-6) << "point " << i;
        EXPECT_FLOAT_EQ(a[i].gain, b[i].gain) << "point " << i;
    }
}

} // namespace