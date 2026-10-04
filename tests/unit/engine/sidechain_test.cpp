// ─────────────────────────────────────────────────────────────────────────────
// Sidechain suites — TRACK-FX-SLOT COMPRESSOR SIDECHAIN v1 engine core.
//
//   Sidechain         (SharedEngineSuite): DSP-equivalence, live-state after
//                      rebuild (Gate 1/10), command validation refusals.
//   SidechainRender   (RenderHarness): observable ducking + the silence-edge
//                      bit-exactness proof.
//
// Test idioms mirror send_test.cpp (live-graph assertions on the drained
// engine) and the render_harness.h offline graph (deviceless, parked pump).
// ─────────────────────────────────────────────────────────────────────────────

#include <gtest/gtest.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "engine/MainAudioProcessor.h"
#include "engine/RoutingManager.h"
#include "engine/MasterBusProcessor.h"
#include "engine/SidechainBus.h"
#include "engine/SidechainCompressor.h"
#include "engine/Track.h"
#include "engine/TrackFXSlot.h"
#include "model/ProjectModel.h"

#include "shared_engine_fixture.h"
#include "render_harness.h"

#include <cmath>
#include <string>
#include <vector>

class Sidechain : public hdaw_test::SharedEngineSuite {};
class SidechainRender : public ::testing::Test {};

namespace {

float rmsOf(const juce::AudioBuffer<float>& buffer)
{
    double sum = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            const double v = buffer.getSample(ch, s);
            sum += v * v;
            ++count;
        }
    return static_cast<float>(std::sqrt(sum / (double) std::max(1, count)));
}

// Adds a compressor FX_SLOT (with the given param_N doubles) to a track's
// FX_CHAIN, creating the chain when absent.
void addCompressorSlot(juce::ValueTree trackTree,
                       std::initializer_list<std::pair<const char*, double>> params)
{
    auto fxChain = trackTree.getChildWithName(IDs::FX_CHAIN);
    if (!fxChain.isValid())
    {
        fxChain = juce::ValueTree(IDs::FX_CHAIN);
        trackTree.addChild(fxChain, -1, nullptr);
    }
    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String("compressor"), nullptr);
    slot.setProperty(IDs::bypassed, false, nullptr);
    for (const auto& [name, value] : params)
        slot.setProperty(juce::Identifier(name), value, nullptr);
    fxChain.addChild(slot, -1, nullptr);
}

// ── Fileless render fixtures ────────────────────────────────────────────────
// The sandbox denies the spawned test process ALL filesystem writes (probe
// verified: %TEMP%, <repo>/.tmp_tests, the DSH spill dir and the repo root all
// fail for the test exe), so WAV fixtures can never work here. The two render
// gates instead inject deterministic test-only nodes into the harness graph.
// Track::processBlock passes input it did not generate straight through
// (buffer.clear() happens only when muted), so a tone node feeding a clip-less
// track IS the track's signal.

// Deterministic stereo sine at 220 Hz / amplitude 0.5. prepareToPlay and
// resetPhase() both zero the phase so two identical runs are bit-identical
// (the silence-edge gate compares float-exactly).
struct ToneGenerator : public juce::AudioProcessor
{
    ToneGenerator()
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {}
    void resetPhase() { phase = 0.0; }
    void prepareToPlay(double sr, int) override { sampleRate = sr; phase = 0.0; }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double inc = twoPi * 220.0 / sampleRate;
        for (int s = 0; s < n; ++s)
        {
            // Full-scale tone: the compressor's ballistics envelope reaches
            // only ~0.26x a sine's peak, so a half-scale stimulus never clears
            // a practical threshold (measured: env 0.09 at peak 0.35).
            const float v = (float) std::sin(phase);
            phase += inc;
            if (phase >= twoPi) phase -= twoPi;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, s, v);
        }
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "ToneGenerator"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
private:
    double sampleRate = 44100.0, phase = 0.0;
};

// Scales its stereo input by `gain`. gain 0.2 → the dest compressor's carrier
// sits below threshold (dest-only ducking source is the sidechain); gain 0.0 →
// an exact-silence payload that still creates the graph ORDER edge (Track
// volume is applied AFTER the FX chain, so it cannot serve either purpose).
struct GainNode : public juce::AudioProcessor
{
    explicit GainNode(float g)
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          gain(g)
    {}
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();
        const int nch = juce::jmin(2, buffer.getNumChannels());
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* w = buffer.getWritePointer(ch);
            for (int s = 0; s < n; ++s) w[s] *= gain;
        }
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "GainNode"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
private:
    float gain = 1.0f;
};

// NodeID of a harness track's graph node (0 when absent).
juce::AudioProcessorGraph::NodeID harnessTrackNodeID(RenderHarness& h, int trackIndex)
{
    for (int i = 0; i < h.graph.getNumNodes(); ++i)
        if (auto n = h.graph.getNode(i))
            if (n->getProcessor() == h.routing->getTrackNode(trackIndex))
                return n->nodeID;
    return {};
}

juce::AudioProcessorGraph::NodeID harnessMasterNodeID(RenderHarness& h)
{
    // Resolve the master through the routing manager's own pointer: a
    // dynamic_cast scan can miss (the master may be reported through a base
    // class), and a missed lookup silently leaves the source connected.
    if (auto* mb = h.routing->getMasterBus())
        for (int i = 0; i < h.graph.getNumNodes(); ++i)
            if (auto n = h.graph.getNode(i))
                if (n->getProcessor() == mb)
                    return n->nodeID;
    return {};
}

// Injects the fileless stimulus/gain nodes and wires them:
//   tone ──┬─> track0 (source, sidechain tap source)
//          └─> carrier ──> track1 (dest compressor)
// The tap's own silence edge (added by RoutingManager on the dest) supplies the
// ordering edge, so no test silence node is needed. `quietDest` picks the
// carrier gain (0.2 for the ducking gate, 1.0 for the bit-exact gate). MUST be
// re-run after every build/rebuild: rebuildFromValueTree calls graph.clear(),
// wiping these nodes. prepareToPlay inside the lock resets the tone phase so
// repeated renders are deterministic.
bool attachFilelessGraph(RenderHarness& h, bool quietDest)
{
    bool ok = false;
    {
        const juce::MessageManagerLock pumpPark;
        auto nTone = h.graph.addNode(std::make_unique<ToneGenerator>());
        auto nCarrier = h.graph.addNode(std::make_unique<GainNode>(quietDest ? 0.04f : 1.0f));
        if (nTone != nullptr && nCarrier != nullptr)
        {
            const auto t0 = harnessTrackNodeID(h, 0);
            const auto t1 = harnessTrackNodeID(h, 1);
            if (t0.uid != 0 && t1.uid != 0)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    h.graph.addConnection({ { nTone->nodeID, ch }, { t0, ch } });
                    h.graph.addConnection({ { nTone->nodeID, ch }, { nCarrier->nodeID, ch } });
                    h.graph.addConnection({ { nCarrier->nodeID, ch }, { t1, ch } });
                }
                h.graph.prepareToPlay(RenderHarness::kSampleRate, RenderHarness::kBlockSize);
                ok = true;
            }
        }
    }
    return ok && h.waitForBake();
}

// Removes the SOURCE track's connection into the master so the harness output
// carries only the DEST signal (both renders then share the master chain, so
// the A/B delta isolates the compressor). The source still feeds the tap. Waits
// for the bake — an un-baked topology made the earlier measurement flaky.
void isolateSourceFromMaster(RenderHarness& h)
{
    const auto mst = harnessMasterNodeID(h);
    const auto src = harnessTrackNodeID(h, 0);
    {
        const juce::MessageManagerLock pumpPark;
        for (const auto& c : h.graph.getConnections())
            if (c.destination.nodeID == mst && c.source.nodeID == src)
                h.graph.removeConnection(c);
        h.graph.prepareToPlay(RenderHarness::kSampleRate, RenderHarness::kBlockSize);
    }
    h.waitForBake();
}

} // namespace

// ── DSP equivalence: wrapper-without-sidechain == stock compressor ──────────

TEST_F(Sidechain, WrapperMatchesStockCompressor)
{
    constexpr double sr = 44100.0;
    constexpr int blockSize = 512;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, 2 };

    juce::dsp::Compressor<float> stock;
    HDAW::SidechainCompressor wrapper;
    stock.prepare(spec);
    wrapper.prepare(spec);

    // Identical, non-default params.
    const float threshold = -18.0f, ratio = 4.0f, attack = 8.0f, release = 150.0f;
    stock.setThreshold(threshold); stock.setRatio(ratio);
    stock.setAttack(attack);       stock.setRelease(release);
    wrapper.setThreshold(threshold); wrapper.setRatio(ratio);
    wrapper.setAttack(attack);       wrapper.setRelease(release);

    // Deterministic multi-tone stereo stimulus (several blocks so the
    // ballistics state exercises attack AND release phases).
    juce::AudioBuffer<float> input(2, blockSize);
    for (int s = 0; s < blockSize; ++s)
    {
        const double t = (double) s / sr;
        const float l = 0.8f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t)
                      + 0.2f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 3177.0 * t);
        const float r = 0.6f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 331.0 * t + 0.7);
        input.setSample(0, s, l);
        input.setSample(1, s, r);
    }

    for (int block = 0; block < 8; ++block)
    {
        juce::AudioBuffer<float> a(input), b(input);
        juce::MidiBuffer midi;
        juce::dsp::AudioBlock<float> blockA(a);
        juce::dsp::AudioBlock<float> blockB(b);
        stock.process(juce::dsp::ProcessContextReplacing<float>(blockA));
        wrapper.process(juce::dsp::ProcessContextReplacing<float>(blockB));

        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < blockSize; ++s)
                // EXEQ: the wrapper must be SAMPLE-IDENTICAL without a
                // sidechain source (same members, same math, same order).
                ASSERT_EQ(a.getSample(ch, s), b.getSample(ch, s))
                    << "block " << block << " ch " << ch << " sample " << s;
    }
}

// ── Single-gain semantics: the TAP applies the level exactly once ───────────

TEST_F(Sidechain, LevelAppliedOnce)
{
    auto bus = std::make_shared<HDAW::SidechainBus>();
    bus->prepare(512);
    bus->level.store(0.5f, std::memory_order_relaxed);
    bus->enabled.store(true, std::memory_order_relaxed);

    HDAW::SidechainTapProcessor tap;
    tap.setBus(bus);
    tap.prepareToPlay(44100.0, 512);

    constexpr int n = 512;
    juce::AudioBuffer<float> buffer(2, n);
    juce::MidiBuffer midi;
    for (int s = 0; s < n; ++s)
    {
        const float v = 0.4f * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * s / 44100.0);
        buffer.setSample(0, s, v);
        buffer.setSample(1, s, v);
    }

    tap.processBlock(buffer, midi);

    // (a) Bus carries level*stimulus — 0.5x, NOT 0.25x (which would prove a
    // second gain application downstream of the tap).
    for (int s = 0; s < n; ++s)
    {
        const float stimulus = 0.4f * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * s / 44100.0);
        EXPECT_NEAR(bus->data[0][s], 0.5f * stimulus, 1e-6f) << "ch0 sample " << s;
        EXPECT_NEAR(bus->data[1][s], 0.5f * stimulus, 1e-6f) << "ch1 sample " << s;
    }
    // (b) Full block published.
    EXPECT_EQ(bus->framesWritten.load(), n);
    // (c) The tap's output is digital silence — the silence edge stays
    // bit-silent when summed into the destination.
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < n; ++s)
            ASSERT_EQ(buffer.getSample(ch, s), 0.0f) << "ch " << ch << " sample " << s;
}

// ── Gate 1/10: tree → live graph restore, asserted on LIVE processors ───────

TEST_F(Sidechain, RebuildRestoresLiveState)
{
    // Zero-track default: create both tracks + the compressor slot.
    ASSERT_GE(engine.getProjectCommands().addTrack("Source"), 0);
    ASSERT_GE(engine.getProjectCommands().addTrack("Dest"), 0);
    engine.getProjectCommands().addFxSlot(1, "compressor", -1, "");
    engine.drainPendingRoutingRebuild();

    const int sourceStableID = engine.getProjectCommands().getTrackID(0);
    ASSERT_GT(sourceStableID, 0);

    auto& cmds = static_cast<AudioEngineCommands&>(engine.getProjectCommands());

    // Dest given POSITIONALLY (trackId=1), source given as the STABLE id —
    // both spellings accepted, stable wins on disagreement.
    const std::string json = cmds.setFxSidechain(
        1, std::nullopt, 0, std::nullopt, sourceStableID, 0.6f, true);
    {
        auto parsed = juce::JSON::parse(juce::String(json));
        auto* obj = parsed.getDynamicObject();
        ASSERT_NE(obj, nullptr) << json;
        EXPECT_TRUE(static_cast<bool>(obj->getProperty("ok"))) << json;
        EXPECT_EQ(static_cast<int>(obj->getProperty("trackId")), 1) << json;
        EXPECT_EQ(static_cast<int>(obj->getProperty("slotIndex")), 0) << json;
        EXPECT_EQ(static_cast<int>(obj->getProperty("sourceTrackId")), 0) << json;
    }
    engine.drainPendingRoutingRebuild();

    auto assertLiveState = [&](const char* phase) {
        auto* proc = engine.getMainProcessor();
        ASSERT_NE(proc, nullptr);
        auto* rm = proc->getRoutingManager();
        ASSERT_NE(rm, nullptr);
        ASSERT_EQ(rm->getSidechainCount(), 1) << phase;
        const auto* conn = rm->getSidechainConnection(1, 0);
        ASSERT_NE(conn, nullptr) << phase;
        ASSERT_NE(conn->bus, nullptr) << phase;
        EXPECT_EQ(conn->sourceTrackIndex, 0) << phase;
        EXPECT_FLOAT_EQ(conn->bus->level.load(), 0.6f) << phase;
        EXPECT_TRUE(conn->bus->enabled.load()) << phase;

        // Bus registered on the LIVE dest slot (Track + TrackFXSlot chain).
        auto* destTrack = proc->getTrack(1);
        ASSERT_NE(destTrack, nullptr) << phase;
        auto* liveBus = destTrack->getSidechainBus(0);
        ASSERT_NE(liveBus, nullptr) << phase;
        EXPECT_EQ(liveBus, conn->bus.get()) << phase;
    };

    assertLiveState("after command");

    // Full rebuild (the same surface a load/clip-edit rides): LIVE state must
    // be restored from the tree, not from stale handles. NO MessageManagerLock
    // wrapper here — rebuildRoutingGraph parks the pump internally and drives
    // its settle bake via runOnMessageThread; wrapping it in an MML from a
    // non-pump thread blocks that drive forever (the test must mirror the
    // production call surface, send_test.cpp precedent).
    engine.getMainProcessor()->rebuildRoutingGraph();
    engine.drainPendingRoutingRebuild();
    assertLiveState("after rebuild");

    // Incremental level/enabled push (no rebuild) reaches the same live bus.
    const std::string json2 = cmds.setFxSidechain(
        1, std::nullopt, 0, std::nullopt, sourceStableID, 0.25f, false);
    {
        auto parsed = juce::JSON::parse(juce::String(json2));
        auto* obj = parsed.getDynamicObject();
        ASSERT_NE(obj, nullptr) << json2;
        EXPECT_TRUE(static_cast<bool>(obj->getProperty("ok"))) << json2;
    }
    engine.drainPendingRoutingRebuild();
    const auto* conn = engine.getMainProcessor()->getRoutingManager()
                           ->getSidechainConnection(1, 0);
    ASSERT_NE(conn, nullptr);
    ASSERT_NE(conn->bus, nullptr);
    EXPECT_FLOAT_EQ(conn->bus->level.load(), 0.25f);
    EXPECT_FALSE(conn->bus->enabled.load());
}

// ── Validation refusals: error AND tree untouched ────────────────────────────

TEST_F(Sidechain, ValidationRefuses)
{
    ASSERT_GE(engine.getProjectCommands().addTrack("A"), 0);
    ASSERT_GE(engine.getProjectCommands().addTrack("B"), 0);
    engine.getProjectCommands().addFxSlot(0, "compressor", -1, "");
    engine.getProjectCommands().addFxSlot(0, "reverb", -1, "");
    engine.drainPendingRoutingRebuild();

    auto& model = engine.getProjectModel();
    auto slotAt = [&](int slotIndex) {
        return model.getTrackListTree().getChild(0)
            .getChildWithName(IDs::FX_CHAIN).getChild(slotIndex);
    };
    auto expectUntouched = [&](const char* what) {
        EXPECT_FALSE(slotAt(0).hasProperty(IDs::sidechainSource)) << what;
        EXPECT_FALSE(slotAt(0).hasProperty(IDs::sidechainLevel)) << what;
        EXPECT_FALSE(slotAt(0).hasProperty(IDs::sidechainEnabled)) << what;
    };

    std::string err;
    auto& cmds = static_cast<AudioEngineCommands&>(engine.getProjectCommands());
    const int stable0 = cmds.getTrackID(0);
    const int stable1 = cmds.getTrackID(1);
    ASSERT_GT(stable0, 0);
    ASSERT_GT(stable1, 0);
    ASSERT_NE(stable0, stable1);

    // Unknown dest track.
    auto r = cmds.setFxSidechain(99, std::nullopt, 0,
                                 std::nullopt, stable1, {}, {}, &err);
    EXPECT_NE(r.find("unknown track"), std::string::npos) << r;
    expectUntouched("unknown dest track");

    // slotIndex out of range (track 1 has no FX chain at all).
    r = cmds.setFxSidechain(1, std::nullopt, 0,
                                                   std::nullopt, stable0, {}, {}, &err);
    EXPECT_NE(r.find("slotIndex out of range"), std::string::npos) << r;

    // Slot is not a compressor.
    r = cmds.setFxSidechain(0, std::nullopt, 1,
                                                   std::nullopt, stable1, {}, {}, &err);
    EXPECT_NE(r.find("sidechain requires a compressor FX slot"), std::string::npos) << r;
    expectUntouched("wrong fxType");

    // Self-sidechain.
    r = cmds.setFxSidechain(0, std::nullopt, 0,
                                                   std::nullopt, stable0, {}, {}, &err);
    EXPECT_NE(r.find("self-sidechain refused (acyclic graph)"), std::string::npos) << r;
    expectUntouched("self sidechain");

    // Unknown source track (stable id that names nothing).
    r = cmds.setFxSidechain(0, std::nullopt, 0,
                                                   12345, std::nullopt, {}, {}, &err);
    EXPECT_NE(r.find("source track not found: 12345"), std::string::npos) << r;
    expectUntouched("unknown source");

    // Level outside 0..1 is REFUSED, never clamped.
    r = cmds.setFxSidechain(0, std::nullopt, 0,
                                                   std::nullopt, stable1, 1.5f, {}, &err);
    EXPECT_NE(r.find("level out of range"), std::string::npos) << r;
    expectUntouched("level range");
}

// ── Positional sourceTrackId 0 is a VALID source (track 0), not "clear" ─────

TEST_F(Sidechain, PositionalZeroResolvesTrackZero)
{
    ASSERT_GE(engine.getProjectCommands().addTrack("Kick"), 0);
    ASSERT_GE(engine.getProjectCommands().addTrack("Bass"), 0);
    engine.getProjectCommands().addFxSlot(1, "compressor", -1, "");
    engine.drainPendingRoutingRebuild();

    auto& cmds = static_cast<AudioEngineCommands&>(engine.getProjectCommands());
    const int stable0 = cmds.getTrackID(0);

    std::string err;
    // EXPLICIT positional sourceTrackId = 0 → source track 0, NOT a clear.
    const std::string json = cmds.setFxSidechain(1, std::nullopt, 0,
                                                 0, std::nullopt, {}, {}, &err);
    {
        auto parsed = juce::JSON::parse(juce::String(json));
        auto* obj = parsed.getDynamicObject();
        ASSERT_NE(obj, nullptr) << json;
        EXPECT_TRUE(static_cast<bool>(obj->getProperty("ok"))) << json;
        EXPECT_EQ(static_cast<int>(obj->getProperty("sourceTrackId")), 0) << json;
    }
    // Tree stores track 0's STABLE id.
    auto slot = engine.getProjectModel().getTrackListTree().getChild(1)
                    .getChildWithName(IDs::FX_CHAIN).getChild(0);
    ASSERT_TRUE(slot.isValid());
    EXPECT_EQ(static_cast<int>(slot.getProperty(IDs::sidechainSource, 0)), stable0);

    // Live connection exists and points at track 0.
    engine.drainPendingRoutingRebuild();
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    const auto* conn = rm->getSidechainConnection(1, 0);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->sourceTrackIndex, 0);

    // Clear with BOTH source args ABSENT.
    const std::string json2 = cmds.setFxSidechain(1, std::nullopt, 0,
                                                  std::nullopt, std::nullopt, {}, {}, &err);
    {
        auto parsed = juce::JSON::parse(juce::String(json2));
        auto* obj = parsed.getDynamicObject();
        ASSERT_NE(obj, nullptr) << json2;
        EXPECT_TRUE(static_cast<bool>(obj->getProperty("ok"))) << json2;
    }
    EXPECT_FALSE(slot.hasProperty(IDs::sidechainSource));
    EXPECT_FALSE(slot.hasProperty(IDs::sidechainLevel));
    EXPECT_FALSE(slot.hasProperty(IDs::sidechainEnabled));
}

// ── Stored-edge cycle refusal (A→B then B→A), transitive chains allowed ─────

TEST_F(Sidechain, ReciprocalCycleRefused)
{
    ASSERT_GE(engine.getProjectCommands().addTrack("A"), 0);
    ASSERT_GE(engine.getProjectCommands().addTrack("B"), 0);
    ASSERT_GE(engine.getProjectCommands().addTrack("C"), 0);
    engine.getProjectCommands().addFxSlot(1, "compressor", -1, "");
    engine.getProjectCommands().addFxSlot(0, "compressor", -1, "");
    engine.getProjectCommands().addFxSlot(2, "compressor", -1, "");
    engine.drainPendingRoutingRebuild();

    auto& cmds = static_cast<AudioEngineCommands&>(engine.getProjectCommands());
    std::string err;

    // Legal edge: B ← A (dest track 1, source track 0).
    const std::string ok1 = cmds.setFxSidechain(1, std::nullopt, 0,
                                                0, std::nullopt, {}, {}, &err);
    EXPECT_NE(ok1.find("\"ok\": true"), std::string::npos) << ok1;
    engine.drainPendingRoutingRebuild();

    // Reciprocal edge A ← B would close A→B→A — refused BEFORE any mutation.
    const std::string refused = cmds.setFxSidechain(0, std::nullopt, 0,
                                                    1, std::nullopt, {}, {}, &err);
    EXPECT_NE(refused.find("sidechain cycle refused"), std::string::npos) << refused;
    auto slotA = engine.getProjectModel().getTrackListTree().getChild(0)
                     .getChildWithName(IDs::FX_CHAIN).getChild(0);
    EXPECT_FALSE(slotA.hasProperty(IDs::sidechainSource)) << refused;
    engine.drainPendingRoutingRebuild();
    EXPECT_EQ(engine.getMainProcessor()->getRoutingManager()->getSidechainCount(), 1);

    // Legal transitive chain: C ← B (A→B→C, no cycle) still succeeds.
    const std::string ok2 = cmds.setFxSidechain(2, std::nullopt, 0,
                                                1, std::nullopt, {}, {}, &err);
    EXPECT_NE(ok2.find("\"ok\": true"), std::string::npos) << ok2;
    engine.drainPendingRoutingRebuild();
    EXPECT_EQ(engine.getMainProcessor()->getRoutingManager()->getSidechainCount(), 2);
}

// ── Render-level proof: sidechain audibly ducks the destination ─────────────
// (RenderHarness idiom: deviceless offline graph, parked pump, bake-wait after
// every topology change — see render_harness.h.)

TEST_F(SidechainRender, DuckingObservable)
{
    constexpr double sr = RenderHarness::kSampleRate;
    constexpr int blockSize = RenderHarness::kBlockSize;

    // FILELESS end-to-end: a deterministic tone node feeds track 0 (source) and,
    // through a low-gain carrier node, track 1 (dest compressor). The carrier
    // gain keeps the dest carrier BELOW the threshold, so the baseline
    // (sidechain off) is pass-through and any reduction is attributable to the
    // external (sidechain) envelope, not the compressor's own.
    RenderHarness harness;
    {
        juce::ValueTree emptyClips("CLIP_LIST");
        harness.init(emptyClips);
    }

    auto track1 = makeSeedTrack("Dest");
    // Threshold -20 dB (0.1): the 0.02-gained carrier's own envelope (~0.018)
    // stays below it, while the source's sidechain envelope (~0.18, after the
    // source track's centre-pan attenuation) sits well above → a solid duck.
    addCompressorSlot(track1, { { "param_0", -20.0 }, { "param_1", 20.0 } });
    {
        const juce::MessageManagerLock pumpPark;
        harness.model.getTrackListTree().addChild(track1, -1, nullptr);
    }
    harness.model.scanAndSyncTrackIDs();
    const int sourceStableID = static_cast<int>(
        harness.model.getTrackListTree().getChild(0).getProperty(IDs::trackID, 0));
    ASSERT_GT(sourceStableID, 0);

    harness.build();
    ASSERT_TRUE(attachFilelessGraph(harness, /*quietDest=*/true))
        << "fileless graph injection failed (track nodes missing?)";

    const int numBlocks = blocksFor(1.0);
    juce::AudioBuffer<float> out(2, numBlocks * blockSize);

    // Baseline A: no sidechain property at all.
    isolateSourceFromMaster(harness);
    harness.render(numBlocks, out);
    const float rmsA = rmsOf(out);
    EXPECT_GT(rmsA, 0.01f) << "dest baseline silent?";

    // Sidechain ON: tree property + full rebuild (topology change).
    harness.model.getTrackListTree().getChild(1)
        .getChildWithName(IDs::FX_CHAIN).getChild(0)
        .setProperty(IDs::sidechainSource, sourceStableID, nullptr);
    {
        const juce::MessageManagerLock pumpPark;
        harness.routing->rebuildFromValueTree();
        harness.routing->reconnectMasterToOutput();
        harness.graph.prepareToPlay(sr, blockSize);
    }
    harness.graph.setNonRealtime(true);
    harness.routing->setClipSourcesNonRealtime(true);
    ASSERT_TRUE(harness.waitForBake());

    // rebuildFromValueTree cleared the graph — re-inject the fileless chain.
    ASSERT_TRUE(attachFilelessGraph(harness, /*quietDest=*/true));

    // LIVE graph proof (Gate 1/10 on the render path too).
    ASSERT_EQ(harness.routing->getSidechainCount(), 1);
    const auto* conn = harness.routing->getSidechainConnection(1, 0);
    ASSERT_NE(conn, nullptr);
    ASSERT_NE(conn->bus, nullptr);
    EXPECT_EQ(conn->sourceTrackIndex, 0);
    ASSERT_NE(harness.routing->getTrackNode(1), nullptr);
    EXPECT_EQ(harness.routing->getTrackNode(1)->getSidechainBus(0), conn->bus.get());

    isolateSourceFromMaster(harness);
    harness.render(numBlocks, out);
    const float rmsB = rmsOf(out);

    const float deltaDb = 20.0f * std::log10(std::max(rmsB, 1e-9f) / std::max(rmsA, 1e-9f));
    EXPECT_LT(deltaDb, -3.0f) << "expected a decisive duck (>=3 dB), got " << deltaDb
                              << " dB (rmsA=" << rmsA << " rmsB=" << rmsB << ")";

    harness.shutdown();
}

// ── The silence edge sums +0.0f exactly: a connected-but-disabled tap must be
//    bit-identical to no sidechain at all. ────────────────────────────────────

TEST_F(SidechainRender, SilenceEdgeSumsExactly)
{
    // FILELESS: two harnesses fed by the same deterministic tone. Harness B has
    // the tap CONNECTED but DISABLED (a payload of exact digital silence); the
    // harness output must then be BIT-identical to harness A (no sidechain at
    // all) — proving the order edge sums +0.0f and never colors the dest.
    auto buildHarness = [&](bool withSidechain) {
        auto harness = std::make_unique<RenderHarness>();
        {
            juce::ValueTree emptyClips("CLIP_LIST");
            harness->init(emptyClips);
        }
        auto track1 = makeSeedTrack("Dest");
        addCompressorSlot(track1, { { "param_0", -12.0 }, { "param_1", 20.0 } });
        {
            const juce::MessageManagerLock pumpPark;
            harness->model.getTrackListTree().addChild(track1, -1, nullptr);
        }
        harness->model.scanAndSyncTrackIDs();
        if (withSidechain)
        {
            const int sourceStableID = static_cast<int>(
                harness->model.getTrackListTree().getChild(0).getProperty(IDs::trackID, 0));
            auto slot = harness->model.getTrackListTree().getChild(1)
                            .getChildWithName(IDs::FX_CHAIN).getChild(0);
            slot.setProperty(IDs::sidechainSource, sourceStableID, nullptr);
            // Connected but DISABLED: the tap must still output exact silence.
            slot.setProperty(IDs::sidechainEnabled, false, nullptr);
        }
        harness->build();
        // quietDest=false → carrier gain 1.0; the payload here is the SILENCE
        // node (gain 0.0), so the dest main path is exactly zero in BOTH
        // harnesses, while the ORDER edge still exists.
        const bool injected = attachFilelessGraph(*harness, /*quietDest=*/false);
        EXPECT_TRUE(injected);
        return harness;
    };

    auto harnessA = buildHarness(false);
    auto harnessB = buildHarness(true);

    ASSERT_EQ(harnessB->routing->getSidechainCount(), 1);
    ASSERT_NE(harnessB->routing->getSidechainConnection(1, 0), nullptr);

    juce::AudioBuffer<float> outA, outB;
    const int numBlocks = blocksFor(0.8);
    outA.setSize(2, numBlocks * RenderHarness::kBlockSize);
    outB.setSize(2, numBlocks * RenderHarness::kBlockSize);
    harnessA->render(numBlocks, outA);
    harnessB->render(numBlocks, outB);

    ASSERT_EQ(outA.getNumSamples(), outB.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < outA.getNumSamples(); ++s)
            // BIT-identical: the tap's silence edge sums +0.0f, never colors.
            ASSERT_EQ(outA.getSample(ch, s), outB.getSample(ch, s))
                << "ch " << ch << " sample " << s;

    harnessA->shutdown();
    harnessB->shutdown();
}
