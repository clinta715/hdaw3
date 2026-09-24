#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/RoutingManager.h"
#include "engine/SendProcessor.h"
#include "engine/Track.h"
#include "engine/FxBusProcessor.h"
#include "engine/GroupBusProcessor.h"
#include "engine/InternalFilter.h"
#include "engine/TrackFXSlot.h"
#include "engine/TransportManager.h"
#include "model/ProjectModel.h"
#include "common/BusFxDefs.h"
#include "common/BusInfo.h"
#include "common/ReadModel.h"

#include <cmath>
#include <string>
#include <vector>

namespace {
// Zero-track default contract (v0.33+): createDefaultProject() ships an empty
// TRACK_LIST — tests own their setup. Send tests need a host track (0) plus a
// send target (1); drain the coalesced routing rebuild so tree edits below hit
// a deterministic projection (lessons 9/10/12; no sleeps).
int seedTrack(AudioEngine& engine, int count = 1)
{
    int idx = -1;
    for (int i = 0; i < count; ++i)
        idx = engine.getProjectCommands().addTrack("Track " + std::to_string(i));
    engine.drainPendingRoutingRebuild();
    return idx;
}
} // namespace

TEST(Send, ReadModelReturnsSends)
{
    AudioEngine engine;
    engine.initialize();

    // Zero-track default (v0.33+): seed the two tracks this test uses (host
    // track 0 + send target 1) and drain the coalesced routing rebuild.
    ASSERT_GE(seedTrack(engine, 2), 0);

    auto& model = engine.getProjectModel();
    auto trackList = model.getTrackListTree();
    ASSERT_GT(trackList.getNumChildren(), 0);

    auto trackTree = trackList.getChild(0);
    juce::ValueTree sendList(IDs::SEND_LIST);
    {
        juce::ValueTree send(IDs::SEND);
        send.setProperty(IDs::sendLevel, 0.75, nullptr);
        send.setProperty(IDs::sendMode, juce::String("pre"), nullptr);
        send.setProperty(IDs::sendTarget, 1, nullptr);
        send.setProperty(IDs::bypassed, false, nullptr);
        sendList.addChild(send, -1, nullptr);
    }
    {
        juce::ValueTree send(IDs::SEND);
        send.setProperty(IDs::sendLevel, 0.25, nullptr);
        send.setProperty(IDs::sendMode, juce::String("post"), nullptr);
        send.setProperty(IDs::sendTarget, 1, nullptr);
        send.setProperty(IDs::bypassed, true, nullptr);
        sendList.addChild(send, -1, nullptr);
    }
    trackTree.addChild(sendList, -1, nullptr);

    engine.getMainProcessor()->rebuildRoutingGraph();

    auto sends = engine.getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 2u);

    EXPECT_EQ(sends[0].sendIndex, 0);
    EXPECT_FLOAT_EQ(sends[0].level, 0.75f);
    EXPECT_TRUE(sends[0].isPreFader);
    EXPECT_FALSE(sends[0].bypassed);

    EXPECT_EQ(sends[1].sendIndex, 1);
    EXPECT_FLOAT_EQ(sends[1].level, 0.25f);
    EXPECT_FALSE(sends[1].isPreFader);
    EXPECT_TRUE(sends[1].bypassed);
}

TEST(Send, SetLevelThroughCommands)
{
    AudioEngine engine;
    engine.initialize();

    // Zero-track default (v0.33+): seed the two tracks this test uses (host
    // track 0 + send target 1) and drain the coalesced routing rebuild.
    ASSERT_GE(seedTrack(engine, 2), 0);

    auto& model = engine.getProjectModel();
    auto trackTree = model.getTrackListTree().getChild(0);
    juce::ValueTree sendList(IDs::SEND_LIST);
    {
        juce::ValueTree send(IDs::SEND);
        send.setProperty(IDs::sendLevel, 0.0, nullptr);
        send.setProperty(IDs::sendMode, juce::String("post"), nullptr);
        send.setProperty(IDs::sendTarget, 1, nullptr);
        sendList.addChild(send, -1, nullptr);
    }
    trackTree.addChild(sendList, -1, nullptr);

    engine.getMainProcessor()->rebuildRoutingGraph();

    engine.getProjectCommands().setTrackSendLevel(0, 0, 0.8f);

    auto sends = engine.getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 1u);
    EXPECT_FLOAT_EQ(sends[0].level, 0.8f);

    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* rm = proc->getRoutingManager();
    ASSERT_NE(rm, nullptr);
}

TEST(Send, SetModeThroughCommands)
{
    AudioEngine engine;
    engine.initialize();

    // Zero-track default (v0.33+): seed the two tracks this test uses (host
    // track 0 + send target 1) and drain the coalesced routing rebuild.
    ASSERT_GE(seedTrack(engine, 2), 0);

    auto& model = engine.getProjectModel();
    auto trackTree = model.getTrackListTree().getChild(0);
    juce::ValueTree sendList(IDs::SEND_LIST);
    {
        juce::ValueTree send(IDs::SEND);
        send.setProperty(IDs::sendLevel, 0.5, nullptr);
        send.setProperty(IDs::sendMode, juce::String("post"), nullptr);
        send.setProperty(IDs::sendTarget, 1, nullptr);
        sendList.addChild(send, -1, nullptr);
    }
    trackTree.addChild(sendList, -1, nullptr);

    engine.getMainProcessor()->rebuildRoutingGraph();

    engine.getProjectCommands().setTrackSendMode(0, 0, true);

    auto sends = engine.getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 1u);
    EXPECT_TRUE(sends[0].isPreFader);
}

TEST(Send, SetBypassedThroughCommands)
{
    AudioEngine engine;
    engine.initialize();

    // Zero-track default (v0.33+): seed the two tracks this test uses (host
    // track 0 + send target 1) and drain the coalesced routing rebuild.
    ASSERT_GE(seedTrack(engine, 2), 0);

    auto& model = engine.getProjectModel();
    auto trackTree = model.getTrackListTree().getChild(0);
    juce::ValueTree sendList(IDs::SEND_LIST);
    {
        juce::ValueTree send(IDs::SEND);
        send.setProperty(IDs::sendLevel, 0.5, nullptr);
        send.setProperty(IDs::sendMode, juce::String("post"), nullptr);
        send.setProperty(IDs::sendTarget, 1, nullptr);
        send.setProperty(IDs::bypassed, false, nullptr);
        sendList.addChild(send, -1, nullptr);
    }
    trackTree.addChild(sendList, -1, nullptr);

    engine.getMainProcessor()->rebuildRoutingGraph();

    engine.getProjectCommands().setTrackSendBypassed(0, 0, true);

    auto sends = engine.getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 1u);
    EXPECT_TRUE(sends[0].bypassed);
}

TEST(Send, StateSurvivesRoutingGraphRebuild)
{
    AudioEngine engine;
    engine.initialize();

    // Zero-track default (v0.33+): seed the two tracks this test uses (host
    // track 0 + send target 1) and drain the coalesced routing rebuild.
    ASSERT_GE(seedTrack(engine, 2), 0);

    auto& model = engine.getProjectModel();
    auto trackTree = model.getTrackListTree().getChild(0);
    juce::ValueTree sendList(IDs::SEND_LIST);
    {
        juce::ValueTree send(IDs::SEND);
        send.setProperty(IDs::sendLevel, 0.6, nullptr);
        send.setProperty(IDs::sendMode, juce::String("pre"), nullptr);
        send.setProperty(IDs::sendTarget, 1, nullptr);
        send.setProperty(IDs::bypassed, true, nullptr);
        sendList.addChild(send, -1, nullptr);
    }
    trackTree.addChild(sendList, -1, nullptr);

    engine.getMainProcessor()->rebuildRoutingGraph();

    engine.getProjectCommands().setTrackSendLevel(0, 0, 0.9f);
    engine.getProjectCommands().setTrackSendMode(0, 0, false);
    engine.getProjectCommands().setTrackSendBypassed(0, 0, false);

    engine.getMainProcessor()->rebuildRoutingGraph();

    auto sends = engine.getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 1u);
    EXPECT_FLOAT_EQ(sends[0].level, 0.9f);
    EXPECT_FALSE(sends[0].isPreFader);
    EXPECT_FALSE(sends[0].bypassed);
}

// ─── Bus / send CREATION (2026-09-22) ────────────────────────────────────────
// The engine slice of the agent bus surface: createBus/createSend/removeBus/
// removeSend mutate the ValueTree and issue ONE rebuildRoutingGraph, so every
// assertion below is on the LIVE graph (never the ReadModel — Gates 1/6/10).
// RoutingManager::sendConnections has no public accessor and RoutingManager.
// {h,cpp} is frozen for this change, so the live send topology is read through
// the public AudioProcessorGraph seam (MainAudioProcessor::getGraph /
// getGraphLock) plus the existing public RoutingManager::getFxBus.

namespace {

// Bootstraps a live RoutingManager on a deviceless session. Preparing is
// idempotent, so this ALWAYS prepares rather than early-returning on an existing
// manager: AudioEngine::initialize() can leave a manager in place while
// getSampleRate() is still 0, and that is exactly the state in which
// rebuildRoutingGraph's `if (getSampleRate() > 0)` guard skips re-preparing
// freshly added nodes - a bus created afterwards stays unprepared, its DSP never
// runs, and (before FxBusProcessor::processBlock grew its fail-safe) processing
// it corrupted memory. Measured 2026-09-23: the chained filter return passed
// audio through unprocessed (lp == hp bit-for-bit).
bool ensureLiveRoutingGraph(AudioEngine& engine)
{
    auto* proc = engine.getMainProcessor();
    if (proc == nullptr) return false;
    {
        const juce::MessageManagerLock pumpPark;
        proc->prepareToPlay(44100.0, 512);
    }
    engine.drainPendingRoutingRebuild();
    return proc->getRoutingManager() != nullptr;
}

juce::AudioProcessorGraph::NodeID nodeIdOfLiveProcessor(AudioEngine& engine,
                                                        const juce::AudioProcessor* target)
{
    auto* proc = engine.getMainProcessor();
    if (proc == nullptr || target == nullptr) return {};
    const juce::SpinLock::ScopedLockType graphLock(proc->getGraphLock());
    for (auto* node : proc->getGraph().getNodes())
        if (node != nullptr && node->getProcessor() == target)
            return node->nodeID;
    return {};
}

std::vector<HDAW::SendProcessor*> liveSendProcessors(AudioEngine& engine)
{
    std::vector<HDAW::SendProcessor*> out;
    auto* proc = engine.getMainProcessor();
    if (proc == nullptr) return out;
    const juce::SpinLock::ScopedLockType graphLock(proc->getGraphLock());
    for (auto* node : proc->getGraph().getNodes())
        if (node != nullptr)
            if (auto* send = dynamic_cast<HDAW::SendProcessor*>(node->getProcessor()))
                out.push_back(send);
    return out;
}

template <typename T>
int countLiveProcessors(AudioEngine& engine)
{
    int count = 0;
    auto* proc = engine.getMainProcessor();
    if (proc == nullptr) return count;
    const juce::SpinLock::ScopedLockType graphLock(proc->getGraphLock());
    for (auto* node : proc->getGraph().getNodes())
        if (node != nullptr && dynamic_cast<T*>(node->getProcessor()) != nullptr)
            ++count;
    return count;
}

bool liveGraphHasConnection(AudioEngine& engine,
                            juce::AudioProcessorGraph::NodeID from, int fromChannel,
                            juce::AudioProcessorGraph::NodeID to, int toChannel)
{
    auto* proc = engine.getMainProcessor();
    if (proc == nullptr) return false;
    const juce::SpinLock::ScopedLockType graphLock(proc->getGraphLock());
    for (const auto& c : proc->getGraph().getConnections())
        if (c.source.nodeID == from && c.source.channelIndex == fromChannel
            && c.destination.nodeID == to && c.destination.channelIndex == toChannel)
            return true;
    return false;
}

juce::ValueTree findBusInTree(const juce::ValueTree& busList, int busID)
{
    for (int i = 0; i < busList.getNumChildren(); ++i)
        if (static_cast<int>(busList.getChild(i).getProperty(IDs::busID, -1)) == busID)
            return busList.getChild(i);
    return {};
}

} // namespace

// G1: a created bus exists as a live processor, and it is the TREE that put it
// there — the same assertions hold after an independent rebuildRoutingGraph.
TEST(BusSendCreate, CreateBusAndSendReachTheLiveGraph)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 2), 0);
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto& cmds = engine.getProjectCommands();
    auto& model = engine.getProjectModel();
    auto busList = model.getBusListTree();
    ASSERT_TRUE(busList.isValid());
    const int busesBefore = busList.getNumChildren();

    const auto fxBus = cmds.createBus("fx", "Dub Delay", "delay", 0);
    ASSERT_TRUE(fxBus.ok) << fxBus.error;
    EXPECT_EQ(fxBus.busID, 2);            // 0 = master, 1 = default "Reverb"
    EXPECT_TRUE(fxBus.error.empty());
    const auto groupBus = cmds.createBus("group", "Drum Group", "", 0);
    ASSERT_TRUE(groupBus.ok) << groupBus.error;
    EXPECT_EQ(groupBus.busID, 3);
    EXPECT_EQ(busList.getNumChildren(), busesBefore + 2);

    const auto busTree = findBusInTree(busList, 2);
    ASSERT_TRUE(busTree.isValid());
    EXPECT_EQ(busTree.getProperty(IDs::name).toString(), juce::String("Dub Delay"));
    EXPECT_EQ(busTree.getProperty(IDs::busType).toString(), juce::String("fx"));
    EXPECT_EQ(static_cast<int>(busTree.getProperty(IDs::busTarget)), 0);
    EXPECT_EQ(busTree.getProperty(IDs::fxType).toString(), juce::String("delay"));

    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* rm = proc->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    ASSERT_NE(rm->getFxBus(2), nullptr) << "bus 2 has no live FxBusProcessor";
    EXPECT_EQ(countLiveProcessors<HDAW::GroupBusProcessor>(engine), 1);

    const auto send = cmds.createSend(0, 2, 0.5f, false);
    ASSERT_TRUE(send.ok) << send.error;
    EXPECT_EQ(send.sendIndex, 0);

    // createSend issued its own rebuild, which REPLACES the RoutingManager (and
    // every live processor), so re-fetch before touching live state.
    rm = proc->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    auto* liveFxBus = rm->getFxBus(2);
    ASSERT_NE(liveFxBus, nullptr);

    // Live send connection {track 0, send 0}: SendProcessor node + the two
    // graph edges (track -> send -> bus), asserted on the live processors.
    auto sends = liveSendProcessors(engine);
    ASSERT_EQ(sends.size(), 1u);
    EXPECT_FLOAT_EQ(sends[0]->getSendLevel(), 0.5f);
    EXPECT_FALSE(sends[0]->isPreFader());
    EXPECT_FALSE(sends[0]->isBypassed());

    const auto trackNode = nodeIdOfLiveProcessor(engine, proc->getTrack(0));
    const auto sendNode = nodeIdOfLiveProcessor(engine, sends[0]);
    const auto fxBusNode = nodeIdOfLiveProcessor(engine, liveFxBus);
    ASSERT_NE(trackNode.uid, 0u);
    ASSERT_NE(sendNode.uid, 0u);
    ASSERT_NE(fxBusNode.uid, 0u);
    EXPECT_TRUE(liveGraphHasConnection(engine, trackNode, 0, sendNode, 0));
    EXPECT_TRUE(liveGraphHasConnection(engine, sendNode, 0, fxBusNode, 0));

    auto sendListTree = engine.getProjectModel().getTrackListTree()
                            .getChild(0).getChildWithName(IDs::SEND_LIST);
    ASSERT_TRUE(sendListTree.isValid());
    ASSERT_EQ(sendListTree.getNumChildren(), 1);
    EXPECT_EQ(static_cast<int>(sendListTree.getChild(0).getProperty(IDs::sendTarget)), 2);
    EXPECT_DOUBLE_EQ(static_cast<double>(sendListTree.getChild(0).getProperty(IDs::sendLevel)), 0.5);
    EXPECT_EQ(sendListTree.getChild(0).getProperty(IDs::sendMode).toString(), juce::String("post"));
    EXPECT_FALSE(static_cast<bool>(sendListTree.getChild(0).getProperty(IDs::bypassed, true)));

    // Gate 1/6/10: the bus node + send connection must survive an unrelated
    // later structural rebuild — proof they live in the tree, not in a
    // transient projection.
    proc->rebuildRoutingGraph();
    rm = proc->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    EXPECT_NE(rm->getFxBus(2), nullptr) << "fx bus lost on rebuild";
    sends = liveSendProcessors(engine);
    ASSERT_EQ(sends.size(), 1u) << "send connection lost on rebuild";
    EXPECT_FLOAT_EQ(sends[0]->getSendLevel(), 0.5f);
    EXPECT_FALSE(sends[0]->isPreFader());
}

// Gate 4/9: a removal cascades to every send that targeted the bus (a dangling
// sendTarget is a silent dead node in RoutingManager::addSend).
TEST(BusSendCreate, RemoveBusCascadesSendsFromTreeAndLiveGraph)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 2), 0);
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto& cmds = engine.getProjectCommands();
    auto busList = engine.getProjectModel().getBusListTree();
    const auto bus = cmds.createBus("fx", "Dub Delay", "delay", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    const auto sendA = cmds.createSend(0, 2, 0.5f, false);
    ASSERT_TRUE(sendA.ok) << sendA.error;
    const auto sendB = cmds.createSend(1, 2, 0.25f, true);
    ASSERT_TRUE(sendB.ok) << sendB.error;
    ASSERT_EQ(liveSendProcessors(engine).size(), 2u);

    std::string error;
    EXPECT_TRUE(cmds.removeBus(2, error)) << error;
    EXPECT_TRUE(error.empty());

    EXPECT_FALSE(findBusInTree(busList, 2).isValid());
    auto trackList = engine.getProjectModel().getTrackListTree();
    for (int t = 0; t < 2; ++t)
    {
        auto sendList = trackList.getChild(t).getChildWithName(IDs::SEND_LIST);
        EXPECT_FALSE(sendList.isValid() && sendList.getNumChildren() > 0)
            << "track " << t << " still targets the removed bus";
    }

    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    EXPECT_EQ(rm->getFxBus(2), nullptr);
    EXPECT_TRUE(liveSendProcessors(engine).empty());

    // The removal is ONE undo unit: undoing it restores the bus AND the sends
    // the cascade took with it.
    ASSERT_TRUE(cmds.canUndo());
    cmds.undo();
    EXPECT_TRUE(findBusInTree(busList, 2).isValid()) << "undo did not restore the bus";
    auto restored = trackList.getChild(0).getChildWithName(IDs::SEND_LIST);
    ASSERT_TRUE(restored.isValid());
    EXPECT_EQ(restored.getNumChildren(), 1) << "undo did not restore the cascaded send";
    EXPECT_EQ(static_cast<int>(restored.getChild(0).getProperty(IDs::sendTarget)), 2);
}

// Rejections must be loud (ok=false + error) and leave the tree untouched — an
// accepted-but-invalid node is a silent no-op further down (Gate 2).
TEST(BusSendCreate, RejectionsLeaveTheTreeUntouched)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 2), 0);
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto& cmds = engine.getProjectCommands();
    auto& model = engine.getProjectModel();
    auto busList = model.getBusListTree();
    const int busesBefore = busList.getNumChildren();
    std::string error;

    const auto badType = cmds.createBus("bogus", "Nope", "delay", 0);
    EXPECT_FALSE(badType.ok);
    EXPECT_FALSE(badType.error.empty());
    EXPECT_EQ(badType.busID, -1);

    const auto badFxType = cmds.createBus("fx", "Nope", "notafxtype", 0);
    EXPECT_FALSE(badFxType.ok);
    EXPECT_NE(badFxType.error.find("reverb"), std::string::npos)
        << "the error must name the accepted fx types: " << badFxType.error;
    EXPECT_NE(badFxType.error.find("filter"), std::string::npos)
        << "the error must name the accepted fx types, filter included: " << badFxType.error;

    const auto emptyFxType = cmds.createBus("fx", "Nope", "", 0);
    EXPECT_FALSE(emptyFxType.ok);

    const auto badBusTarget = cmds.createBus("fx", "Nope", "delay", 9999);
    EXPECT_FALSE(badBusTarget.ok);
    EXPECT_FALSE(badBusTarget.error.empty());

    EXPECT_EQ(busList.getNumChildren(), busesBefore);

    const auto badSendTarget = cmds.createSend(0, 9999, 0.5f, false);
    EXPECT_FALSE(badSendTarget.ok);
    EXPECT_EQ(badSendTarget.sendIndex, -1);
    EXPECT_FALSE(badSendTarget.error.empty());
    // Rejected before any edit: not even an empty SEND_LIST is stamped.
    EXPECT_FALSE(model.getTrackListTree().getChild(0).getChildWithName(IDs::SEND_LIST).isValid());

    const auto badTrack = cmds.createSend(99, 0, 0.5f, false);
    EXPECT_FALSE(badTrack.ok);
    EXPECT_FALSE(badTrack.error.empty());

    error.clear();
    EXPECT_FALSE(cmds.removeBus(0, error)) << "the master bus must not be removable";
    EXPECT_FALSE(error.empty());
    error.clear();
    EXPECT_FALSE(cmds.removeBus(9999, error));
    EXPECT_FALSE(error.empty());
    error.clear();
    EXPECT_FALSE(cmds.removeSend(0, 0, error)) << "there is no send to remove";
    EXPECT_FALSE(error.empty());
    error.clear();
    EXPECT_FALSE(cmds.removeSend(99, 0, error));

    EXPECT_EQ(busList.getNumChildren(), busesBefore);
}

// G5: createBus opens the undo unit and createSend joins it, so the
// "create the bus, then route to it" idiom reverts as ONE undo step.
TEST(BusSendCreate, UndoRevertsBusAndSendAsOneUnit)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 2), 0);
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto& cmds = engine.getProjectCommands();
    auto& model = engine.getProjectModel();
    auto busList = model.getBusListTree();
    const auto bus = cmds.createBus("fx", "Dub Delay", "delay", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    const auto send = cmds.createSend(0, 2, 0.5f, false);
    ASSERT_TRUE(send.ok) << send.error;

    ASSERT_TRUE(cmds.canUndo());
    cmds.undo();

    EXPECT_FALSE(findBusInTree(busList, 2).isValid()) << "bus survived the undo";
    auto sendList = model.getTrackListTree().getChild(0).getChildWithName(IDs::SEND_LIST);
    EXPECT_FALSE(sendList.isValid() && sendList.getNumChildren() > 0) << "send survived the undo";
    // The bus's own unit, not the whole session: the seeded tracks stay.
    EXPECT_EQ(model.getTrackListTree().getNumChildren(), 2);
}

// The logged clamp: a send level is a linear non-negative gain.
TEST(BusSendCreate, CreateSendClampsNegativeLevelAndHonoursPreFader)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto& cmds = engine.getProjectCommands();
    const auto send = cmds.createSend(0, 1, -0.5f, true);   // bus 1 = default Reverb
    ASSERT_TRUE(send.ok) << send.error;
    EXPECT_EQ(send.sendIndex, 0);

    auto sendList = engine.getProjectModel().getTrackListTree()
                        .getChild(0).getChildWithName(IDs::SEND_LIST);
    ASSERT_TRUE(sendList.isValid());
    ASSERT_EQ(sendList.getNumChildren(), 1);
    EXPECT_DOUBLE_EQ(static_cast<double>(sendList.getChild(0).getProperty(IDs::sendLevel)), 0.0);
    EXPECT_EQ(sendList.getChild(0).getProperty(IDs::sendMode).toString(), juce::String("pre"));

    auto sends = liveSendProcessors(engine);
    ASSERT_EQ(sends.size(), 1u);
    EXPECT_FLOAT_EQ(sends[0]->getSendLevel(), 0.0f) << "negative send gain was not clamped";
    EXPECT_TRUE(sends[0]->isPreFader());

    std::string error;
    EXPECT_TRUE(cmds.removeSend(0, 0, error)) << error;
    EXPECT_TRUE(liveSendProcessors(engine).empty());
    error.clear();
    EXPECT_FALSE(cmds.removeSend(0, 0, error)) << "index is now out of range";
    EXPECT_FALSE(error.empty());
}

// ─── Bus FX params (2026-09-22, slice C1) ────────────────────────────────────
// A bus return is shapable: set_bus_fx_param's command writes the BUS node's
// param_<i> (the durable source a rebuild restores) AND the running
// FxBusProcessor. Every assertion below is on the LIVE processor, never on the
// tree alone — a value that only lands in the tree is not shaping anything.

namespace {

HDAW::FxBusProcessor* liveFxBus(AudioEngine& engine, int busID)
{
    auto* proc = engine.getMainProcessor();
    if (proc == nullptr) return nullptr;
    auto* rm = proc->getRoutingManager();
    return (rm != nullptr) ? rm->getFxBus(busID) : nullptr;
}

// Renders a steady 1 kHz sine through a FRESH bus with one param applied and
// returns the last block's RMS — a readback of the atomic cannot prove the DSP
// saw the value, only the rendered signal can.
float busSineRms(const juce::String& fxType, int paramIndex, float value, int blocks = 4)
{
    HDAW::FxBusProcessor fx("Probe", fxType);
    fx.prepareToPlay(44100.0, 512);
    fx.setParam(paramIndex, value);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    float rms = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        for (int s = 0; s < 512; ++s)
        {
            const float v = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi
                                            * 1000.0 * s / 44100.0);
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, s, v);
        }
        fx.processBlock(buffer, midi);
        if (b != blocks - 1) continue;
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                sum += static_cast<double>(buffer.getSample(ch, s)) * buffer.getSample(ch, s);
        rms = static_cast<float>(std::sqrt(sum / (2 * 512)));
    }
    return rms;
}

// Energy of the reverb TAIL (blocks 6..23, i.e. past the comb delays at
// ~1100-1600 samples) after a single impulse: the room size rides the comb
// feedback, so a bigger room keeps ringing.
double busReverbTailEnergy(float roomSize)
{
    HDAW::FxBusProcessor fx("Probe", "reverb");
    fx.prepareToPlay(44100.0, 512);
    fx.setParam(0, roomSize);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    double energy = 0.0;
    for (int b = 0; b < 24; ++b)
    {
        buffer.clear();
        if (b == 0)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, 0, 1.0f);
        fx.processBlock(buffer, midi);
        if (b < 6) continue;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                energy += static_cast<double>(buffer.getSample(ch, s)) * buffer.getSample(ch, s);
    }
    return energy;
}

// Renders an impulse THROUGH a SendProcessor into a fresh delay bus — the
// return's real input path (the send scales the host signal, the bus runs its
// chain) — and returns the concatenated left channel. `playHead`, when given,
// drives SyncToTempo exactly as the graph's playhead drives it in the session
// (FxBusProcessor reads getPlayHead() every block, like Track does for slots).
std::vector<float> renderSendIntoDelayBus(float delaySec, float feedback, float mix,
                                          float sync, float division,
                                          juce::AudioPlayHead* playHead,
                                          int blocks, int blockSize = 512)
{
    HDAW::SendProcessor send;
    send.prepareToPlay(44100.0, blockSize);
    send.setSendLevel(1.0f);

    HDAW::FxBusProcessor bus("Dub Delay", "delay");
    bus.prepareToPlay(44100.0, blockSize);
    bus.setParam(0, delaySec);
    bus.setParam(1, feedback);
    bus.setParam(2, mix);
    bus.setParam(3, sync);
    bus.setParam(4, division);
    if (playHead != nullptr)
        bus.setPlayHead(playHead);

    std::vector<float> out;
    out.reserve(static_cast<size_t>(blocks) * static_cast<size_t>(blockSize));
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        if (b == 0)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, 0, 1.0f);
        send.processBlock(buffer, midi);
        bus.processBlock(buffer, midi);
        for (int s = 0; s < blockSize; ++s)
            out.push_back(buffer.getSample(0, s));
    }
    return out;
}

// Peak |sample| within +-2 of `index` — the tap lands on the sample the derived
// time rounds to, and this keeps the assertion about the tap, not about a
// rounding rule.
float peakNear(const std::vector<float>& out, int index)
{
    float peak = 0.0f;
    for (int i = juce::jmax(0, index - 2); i <= juce::jmin((int) out.size() - 1, index + 2); ++i)
        peak = juce::jmax(peak, std::fabs(out[(size_t) i]));
    return peak;
}

} // namespace

// G1/G2: the command's value is on the running return and in the tree.
TEST(BusFxParam, SetParamReachesTheLiveProcessorAndTheTree)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto* live = liveFxBus(engine, 1);          // 1 = the default "Reverb" fx bus
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(live->getFxType(), juce::String("reverb"));
    EXPECT_FLOAT_EQ(live->getParam(0), 0.5f) << "an untouched bus must run the def default";

    auto& cmds = engine.getProjectCommands();
    std::string error;
    ASSERT_TRUE(cmds.setBusFxParam(1, 0, 0.9f, error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_FLOAT_EQ(live->getParam(0), 0.9f) << "the live return did not take the value";

    auto busTree = findBusInTree(engine.getProjectModel().getBusListTree(), 1);
    ASSERT_TRUE(busTree.isValid());
    ASSERT_TRUE(busTree.hasProperty("param_0")) << "the durable tree property was not written";
    EXPECT_NEAR(static_cast<double>(busTree.getProperty("param_0")), 0.9, 1e-6);
    EXPECT_FALSE(busTree.hasProperty("param_1")) << "only the addressed param may be written";
}

// G2: the value is re-applied after rebuildRoutingGraph AND after a
// prepareToPlay (which re-runs resetFxChain), including a value written before
// the bus's processor ever existed.
TEST(BusFxParam, ValuesSurviveRebuildAndReprepare)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);

    // Written BEFORE the graph is prepared: the tree is the only carrier here,
    // and it is what the first prepareToPlay's graph build restores from.
    auto& cmds = engine.getProjectCommands();
    std::string error;
    ASSERT_TRUE(cmds.setBusFxParam(1, 0, 0.9f, error)) << error;
    ASSERT_TRUE(cmds.setBusFxParam(1, 2, 0.1f, error)) << error;
    auto busTree = findBusInTree(engine.getProjectModel().getBusListTree(), 1);
    ASSERT_TRUE(busTree.isValid());
    EXPECT_NEAR(static_cast<double>(busTree.getProperty("param_0")), 0.9, 1e-6);

    ASSERT_TRUE(ensureLiveRoutingGraph(engine));      // first prepareToPlay
    auto* live = liveFxBus(engine, 1);
    ASSERT_NE(live, nullptr);
    EXPECT_FLOAT_EQ(live->getParam(0), 0.9f) << "a pre-prepare write was lost by prepareToPlay";
    EXPECT_FLOAT_EQ(live->getParam(2), 0.1f);

    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    proc->rebuildRoutingGraph();
    live = liveFxBus(engine, 1);
    ASSERT_NE(live, nullptr) << "the fx bus was lost on rebuild";
    EXPECT_FLOAT_EQ(live->getParam(0), 0.9f) << "Room Size lost on rebuild (G2)";
    EXPECT_FLOAT_EQ(live->getParam(2), 0.1f) << "Wet Level lost on rebuild (G2)";

    {
        const juce::MessageManagerLock pumpPark;
        proc->prepareToPlay(44100.0, 512);
    }
    engine.drainPendingRoutingRebuild();
    live = liveFxBus(engine, 1);
    ASSERT_NE(live, nullptr);
    EXPECT_FLOAT_EQ(live->getParam(0), 0.9f) << "resetFxChain re-applied the literals, not the params";
    EXPECT_FLOAT_EQ(live->getParam(2), 0.1f);
}

// G3: clamping and rejection at the command entry point (lesson 23), with no
// mutation of any kind on a rejection.
TEST(BusFxParam, ClampsOutOfRangeAndRejectsBadTargets)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto& cmds = engine.getProjectCommands();
    auto& model = engine.getProjectModel();
    auto busList = model.getBusListTree();
    auto* live = liveFxBus(engine, 1);
    ASSERT_NE(live, nullptr);
    std::string error;

    // Above max / below min clamp to the def at BOTH ends (command + processor).
    ASSERT_TRUE(cmds.setBusFxParam(1, 0, 5.0f, error)) << error;
    EXPECT_FLOAT_EQ(live->getParam(0), 1.0f) << "value above the def max was not clamped";
    auto busTree = findBusInTree(busList, 1);
    EXPECT_NEAR(static_cast<double>(busTree.getProperty("param_0")), 1.0, 1e-6);
    ASSERT_TRUE(cmds.setBusFxParam(1, 0, -3.0f, error)) << error;
    EXPECT_FLOAT_EQ(live->getParam(0), 0.0f) << "value below the def min was not clamped";

    // Unknown paramIndex: rejected, and neither the tree nor the processor moves.
    error.clear();
    const int propsBefore = busTree.getNumProperties();
    EXPECT_FALSE(cmds.setBusFxParam(1, 99, 0.5f, error));
    EXPECT_NE(error.find("out of range"), std::string::npos) << error;
    EXPECT_EQ(busTree.getNumProperties(), propsBefore) << "a rejected index still wrote the tree";
    EXPECT_FLOAT_EQ(live->getParam(0), 0.0f) << "a rejected index still moved the processor";

    // Unknown busID.
    error.clear();
    EXPECT_FALSE(cmds.setBusFxParam(999, 0, 0.5f, error));
    EXPECT_NE(error.find("no bus with id 999"), std::string::npos) << error;

    // A non-fx bus (group) has no params to shape.
    const auto group = cmds.createBus("group", "Drum Group", "", 0);
    ASSERT_TRUE(group.ok) << group.error;
    error.clear();
    EXPECT_FALSE(cmds.setBusFxParam(group.busID, 0, 0.5f, error));
    EXPECT_NE(error.find("is not an fx bus"), std::string::npos) << error;

    // An fx bus whose fxType has no defs: writing a param would be fake (G5).
    juce::ValueTree bogus(IDs::BUS);
    bogus.setProperty(IDs::name, "Bogus Bus", nullptr);
    bogus.setProperty(IDs::busID, 42, nullptr);
    bogus.setProperty(IDs::busType, "fx", nullptr);
    bogus.setProperty(IDs::busTarget, 0, nullptr);
    bogus.setProperty(IDs::fxType, "notafxtype", nullptr);
    busList.addChild(bogus, -1, nullptr);
    error.clear();
    EXPECT_FALSE(cmds.setBusFxParam(42, 0, 0.5f, error));
    EXPECT_NE(error.find("unsupported fxType \"notafxtype\""), std::string::npos) << error;
    EXPECT_FALSE(findBusInTree(busList, 42).hasProperty("param_0"));
    busList.removeChild(busList.indexOf(bogus), nullptr);

    // Processor-level entry clamp: the def wins even when a caller bypasses the
    // command (lesson 23 applies at every entry).
    HDAW::FxBusProcessor eqBus("Test EQ", "eq");
    eqBus.setParam(0, 1.0f);                      // below the 20 Hz minimum
    EXPECT_FLOAT_EQ(eqBus.getParam(0), 20.0f);
    eqBus.setParam(1, 500.0f);                    // above the Q maximum
    EXPECT_FLOAT_EQ(eqBus.getParam(1), 10.0f);
    eqBus.setParam(7, 0.5f);                      // no such param
    EXPECT_FLOAT_EQ(eqBus.getParam(7), 0.0f);
}

// The delay bus is the full 6-param delay (the shared InternalDelay DSP backs
// all six), the line's capacity is sized so the def's 5 s top is reachable
// rather than silently clipped, and it adds no PDC latency.
TEST(BusFxParam, DelayBusHoldsTheFullDefRange)
{
    HDAW::FxBusProcessor delayBus("Test Delay", "delay");
    delayBus.prepareToPlay(96000.0, 512);         // the worst case for a fixed capacity
    delayBus.setParam(0, 5.0f);                   // the def max: must not trip the line's jassert
    EXPECT_FLOAT_EQ(delayBus.getParam(0), 5.0f);
    delayBus.setParam(0, 100.0f);                 // clamped by the def, not by the capacity
    EXPECT_FLOAT_EQ(delayBus.getParam(0), 5.0f);
    // A value written before prepare is re-applied by resetFxChain.
    HDAW::FxBusProcessor fresh("Test Delay 2", "delay");
    fresh.setParam(0, 0.25f);
    fresh.prepareToPlay(44100.0, 512);
    EXPECT_FLOAT_EQ(fresh.getParam(0), 0.25f);
    // A delay effect is not a latent process: it must not move the project's
    // reported latency (G-C3-5).
    EXPECT_EQ(delayBus.getLatencySamples(), 0);
    EXPECT_EQ(fresh.getLatencySamples(), 0);
}

// ─── Slice C3: the bus delay really repeats (2026-09-22) ─────────────────────
// The point of the DSP extraction: a send into a delay return with Feedback
// produces repeats the bare DelayLine could not — measurably more tail energy
// and a much longer tail than the same send with Feedback 0. Assertions are on
// the rendered send→return signal, never on the stored value.
TEST(BusFxParam, DelayBusFeedbackMakesTheReturnRepeat)
{
    constexpr float kDelaySec = 0.05f;                      // 2205 samples
    const int delaySamps = juce::roundToInt(kDelaySec * 44100.0f);

    const auto withFeedback    = renderSendIntoDelayBus(kDelaySec, 0.7f, 0.5f, 0.0f, 0.0f, nullptr, 100);
    const auto withoutFeedback = renderSendIntoDelayBus(kDelaySec, 0.0f, 0.5f, 0.0f, 0.0f, nullptr, 100);
    ASSERT_EQ(withFeedback.size(), withoutFeedback.size());

    // Mix 0.5: the dry send arrives at sample 0, the first echo one delay later.
    EXPECT_NEAR(withFeedback[0], 0.5f, 0.01f) << "the dry half of the return";
    EXPECT_NEAR(withFeedback[(size_t) delaySamps], 0.5f, 0.01f) << "echo #1";
    EXPECT_NEAR(withFeedback[(size_t) (2 * delaySamps)], 0.35f, 0.01f) << "echo #2 = echo #1 * 0.7";

    struct Tail { double energy = 0.0; int taps = 0; size_t last = 0; };
    auto tailAfterFirstEcho = [delaySamps](const std::vector<float>& out)
    {
        Tail t;
        for (size_t i = (size_t) (2 * delaySamps + 3); i < out.size(); ++i)
        {
            t.energy += static_cast<double>(out[i]) * out[i];
            if (std::fabs(out[i]) > 1e-3f) { ++t.taps; t.last = i; }
        }
        return t;
    };
    const Tail hi = tailAfterFirstEcho(withFeedback);
    const Tail lo = tailAfterFirstEcho(withoutFeedback);

    EXPECT_GT(hi.energy, 0.05) << "Feedback 0.7 left no repeating tail (energy=" << hi.energy << ")";
    EXPECT_LT(lo.energy, 1e-6) << "Feedback 0 must leave nothing after its single echo (energy="
                               << lo.energy << ")";
    EXPECT_GE(hi.taps, 8) << "0.7 feedback must give many repeats, got " << hi.taps;
    EXPECT_EQ(lo.taps, 0) << "feedback 0 must not repeat";
    EXPECT_GT(hi.last, static_cast<size_t>(10 * delaySamps))
        << "the tail must still be ringing 10 echoes in (last=" << hi.last << ")";
}

// G-C3-3: SyncToTempo is real on the return — the tap spacing follows Division
// AND the project BPM, measured on the rendered signal. The playhead is the
// production path (FxBusProcessor reads getPlayHead() like Track does).
TEST(BusFxParam, DelayBusSyncFollowsDivisionAndProjectBpm)
{
    HDAW::TransportManager transport;
    HDAW::InternalPlayHead playHead(transport);

    const float sync = 1.0f, mix = 1.0f, fb = 0.0f;   // pure wet, single tap per division
    constexpr int kBlocks = 60;                        // 30720 samples

    transport.setBPM(120.0);   // 1/8 = 0.0625 s (2756), 1/4 = 0.125 s (5513)
    const auto eighth120  = renderSendIntoDelayBus(1.0f, fb, mix, sync, 0.0f, &playHead, kBlocks);
    const auto quarter120 = renderSendIntoDelayBus(1.0f, fb, mix, sync, 6.0f, &playHead, kBlocks);

    transport.setBPM(60.0);    // 1/4 = 0.25 s (11025)
    const auto quarter60  = renderSendIntoDelayBus(1.0f, fb, mix, sync, 6.0f, &playHead, kBlocks);

    EXPECT_NEAR(peakNear(eighth120, 2756), 1.0f, 0.02f) << "1/8 @ 120 BPM did not tap at 0.0625 s";
    EXPECT_NEAR(peakNear(quarter120, 5513), 1.0f, 0.02f) << "1/4 @ 120 BPM did not tap at 0.125 s";
    EXPECT_NEAR(peakNear(quarter60, 11025), 1.0f, 0.02f) << "1/4 @ 60 BPM did not tap at 0.25 s";

    // The division/BPM really moved the tap: the other candidates are silent.
    EXPECT_LT(peakNear(eighth120, 5513), 1e-3f) << "division 0 tapped at the 1/4 position";
    EXPECT_LT(peakNear(quarter120, 2756), 1e-3f) << "division 6 tapped at the 1/8 position";
    EXPECT_LT(peakNear(quarter60, 5513), 1e-3f) << "the BPM change did not move the tap";
    EXPECT_LT(peakNear(quarter120, 11025), 1e-3f);
}

// G-C3-4: a hostile feedback cannot run the recursion away, and the Delay Time
// def top (5 s) is real rather than aliased by a short line.
TEST(BusFxParam, DelayBusClampsHostileFeedbackAndHoldsTheFiveSecondTop)
{
    // Feedback 5.0 clamps to the def max 0.99 at both entries, and ~4.6 s of
    // input through it (230 round trips) stays finite and bounded. Unclamped it
    // would be past 1e300 within a few hundred repeats.
    HDAW::FxBusProcessor hostile("Hostile", "delay");
    hostile.prepareToPlay(44100.0, 512);
    hostile.setParam(1, 5.0f);
    EXPECT_FLOAT_EQ(hostile.getParam(1), 0.99f) << "feedback above the def max was not clamped";
    hostile.setParam(0, 0.02f);
    hostile.setParam(2, 1.0f);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 400; ++b)
    {
        buffer.clear();
        for (int s = 0; s < 512; ++s)
        {
            const float v = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi
                                            * 2000.0 * (s + b * 512) / 44100.0);
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, s, v);
        }
        hostile.processBlock(buffer, midi);
        double sumAbs = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                sumAbs += std::fabs(static_cast<double>(buffer.getSample(ch, s)));
        ASSERT_TRUE(std::isfinite(sumAbs)) << "the feedback recursion ran away at block " << b;
        ASSERT_LT(sumAbs, 1.0e6) << "the feedback recursion grew without bound at block " << b;
    }

    // Delay Time 100 s clamps to 5 s and the echo really lands at 5 s (220500
    // samples): the line's capacity was sized to the def, so the previous ~1 s
    // capacity would have aliased this tap back to 44100.
    constexpr int kFiveSecondSamples = 220500;
    const auto fiveSeconds = renderSendIntoDelayBus(100.0f, 0.0f, 1.0f, 0.0f, 0.0f, nullptr, 440);
    ASSERT_GT((int) fiveSeconds.size(), kFiveSecondSamples + 4);
    EXPECT_NEAR(peakNear(fiveSeconds, kFiveSecondSamples), 1.0f, 0.02f)
        << "the 5 s Delay Time def top is not reachable";
    EXPECT_NEAR(peakNear(fiveSeconds, 44100), 0.0f, 1e-4f)
        << "the tap aliased to the old 1 s capacity";
}

// G-C3-6: the delay's new params live on the BUS node and are re-applied after
// rebuildRoutingGraph(), where the rebuilt return still repeats.
TEST(BusFxParam, DelayParamsSurviveRebuildAndDriveTheRebuiltReturn)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto& cmds = engine.getProjectCommands();
    const auto bus = cmds.createBus("fx", "Dub Delay", "delay", 0);
    ASSERT_TRUE(bus.ok) << bus.error;

    std::string error;
    ASSERT_TRUE(cmds.setBusFxParam(bus.busID, 0, 0.05f, error)) << error;
    ASSERT_TRUE(cmds.setBusFxParam(bus.busID, 1, 0.7f, error)) << error;
    ASSERT_TRUE(cmds.setBusFxParam(bus.busID, 2, 1.0f, error)) << error;
    auto* live = liveFxBus(engine, bus.busID);
    ASSERT_NE(live, nullptr);
    EXPECT_FLOAT_EQ(live->getParam(1), 0.7f);

    engine.getMainProcessor()->rebuildRoutingGraph();
    live = liveFxBus(engine, bus.busID);
    ASSERT_NE(live, nullptr) << "the delay return was lost on rebuild";
    EXPECT_FLOAT_EQ(live->getParam(0), 0.05f) << "Delay Time lost on rebuild";
    EXPECT_FLOAT_EQ(live->getParam(1), 0.7f) << "Feedback lost on rebuild";
    EXPECT_FLOAT_EQ(live->getParam(2), 1.0f) << "Mix lost on rebuild";

    // The rebuilt processor repeats: the same impulse A/B as the fresh-bus test,
    // driven straight into the restored return.
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    double tailEnergy = 0.0;
    for (int b = 0; b < 40; ++b)
    {
        buffer.clear();
        if (b == 0)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, 0, 1.0f);
        live->processBlock(buffer, midi);
        if (b >= 2)
            for (int s = 0; s < 512; ++s)
                tailEnergy += static_cast<double>(buffer.getSample(0, s)) * buffer.getSample(0, s);
    }
    EXPECT_GT(tailEnergy, 0.01) << "the restored return does not repeat (tail=" << tailEnergy << ")";
}

// The live-apply contract: each bus type's params reach the RENDERED signal,
// not just the atomic readback (a stored-but-unapplied param would pass every
// other test in this suite).
TEST(BusFxParam, ParamChangesTheRenderedOutput)
{
    const float gainDown = busSineRms("eq", 2, -24.0f);
    const float gainUp   = busSineRms("eq", 2, 24.0f);
    EXPECT_LT(gainDown, gainUp * 0.1f)
        << "EQ Gain did not reach the rendered signal (down=" << gainDown
        << " up=" << gainUp << ")";

    const double smallRoom = busReverbTailEnergy(0.0f);
    const double bigRoom   = busReverbTailEnergy(1.0f);
    EXPECT_GT(bigRoom, smallRoom * 2.0)
        << "reverb Room Size did not reach the rendered tail (small=" << smallRoom
        << " big=" << bigRoom << ")";

    // Delay Time moves the echo: 0.01 s (441 samples) lands inside the first
    // block, 0.5 s (22050 samples) does not. Mix 1.0 makes the echo the whole
    // output of the return (the shared internal delay's blend, like the track
    // delay's).
    auto delayedImpulse = [](float seconds)
    {
        HDAW::FxBusProcessor fx("Probe", "delay");
        fx.prepareToPlay(44100.0, 512);
        fx.setParam(0, seconds);
        fx.setParam(2, 1.0f);         // Mix: pure wet
        juce::AudioBuffer<float> buffer(2, 512);
        buffer.clear();
        buffer.setSample(0, 0, 1.0f);
        buffer.setSample(1, 0, 1.0f);
        juce::MidiBuffer midi;
        fx.processBlock(buffer, midi);
        return buffer.getSample(0, 441);
    };
    EXPECT_NEAR(delayedImpulse(0.01f), 1.0f, 0.05f) << "Delay Time did not reach the delay line";
    EXPECT_NEAR(delayedImpulse(0.5f), 0.0f, 1e-4f);
}

// The shared def table is the track-FX table for the same type: one parameter
// space for the surfaces, not two.
TEST(BusFxParam, DefTableMatchesTrackFxDefs)
{
    for (const char* type : { "reverb", "eq", "compressor", "delay", "filter" })
    {
        const auto& busDefs = HDAW::busFxParamDefs(type);
        const auto trackDefs = HDAW::TrackFXSlot::getParamDefsForType(type);
        ASSERT_EQ(busDefs.size(), trackDefs.size()) << type;
        for (size_t i = 0; i < busDefs.size(); ++i)
        {
            EXPECT_EQ(juce::String(busDefs[i].name), trackDefs[i].name) << type << " #" << i;
            EXPECT_FLOAT_EQ(busDefs[i].def, trackDefs[i].defaultValue) << type << " #" << i;
            EXPECT_FLOAT_EQ(busDefs[i].min, trackDefs[i].minValue) << type << " #" << i;
            EXPECT_FLOAT_EQ(busDefs[i].max, trackDefs[i].maxValue) << type << " #" << i;
        }
    }

    // The delay return advertises the FULL track delay table (slice C3): the
    // shared InternalDelay DSP applies all six, so nothing here is a fake
    // param (G5) — including the 0.99 feedback top that guards the recursion.
    const auto trackDelayDefs = HDAW::TrackFXSlot::getParamDefsForType("delay");
    const auto& busDelayDefs = HDAW::busFxParamDefs("delay");
    ASSERT_EQ(trackDelayDefs.size(), 6u) << "the track delay is the 6-param one";
    ASSERT_EQ(busDelayDefs.size(), 6u) << "the delay return must expose the same 6 params";
    EXPECT_EQ(juce::String(busDelayDefs[0].name), juce::String("Delay Time"));
    EXPECT_EQ(juce::String(busDelayDefs[1].name), juce::String("Feedback"));
    EXPECT_EQ(juce::String(busDelayDefs[2].name), juce::String("Mix"));
    EXPECT_EQ(juce::String(busDelayDefs[3].name), juce::String("SyncToTempo"));
    EXPECT_EQ(juce::String(busDelayDefs[4].name), juce::String("Division"));
    EXPECT_EQ(juce::String(busDelayDefs[5].name), juce::String("Damping"));
    EXPECT_FLOAT_EQ(busDelayDefs[1].max, 0.99f);
    EXPECT_FLOAT_EQ(busDelayDefs[4].max, 6.0f);

    EXPECT_EQ(HDAW::busFxParamDefs("filter").size(), 3u)
        << "the filter return exposes Cutoff / Mode / Resonance";
    EXPECT_TRUE(HDAW::busFxParamDefs("notafxtype").empty());
    EXPECT_TRUE(HDAW::busFxParamDefs("").empty());
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("reverb", 0, 9.0f), 1.0f);
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("reverb", 9, 9.0f), 9.0f) << "unknown index is not a clamp";
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("delay", 1, 4.0f), 0.99f);
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("filter", 0, 5.0f), 20.0f);
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("filter", 0, 1.0e6f), 20000.0f);
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("filter", 1, 9.0f), 2.0f);
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("filter", 2, 0.0f), 0.1f);
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("filter", 2, 99.0f), 10.0f);

    // G1: the accepted-type list names filter in the same order both surfaces
    // and createBus's rejection message read it (one list, every message).
    bool filterListed = false;
    for (const char* t : HDAW::busFxTypes())
        filterListed = filterListed || (std::string(t) == "filter");
    EXPECT_TRUE(filterListed) << "busFxTypes() must accept filter";
    EXPECT_NE(std::string(HDAW::busFxTypesText()).find("filter"), std::string::npos);
    EXPECT_EQ(std::string(HDAW::busFxTypesText()), std::string("reverb, delay, eq, compressor, filter"));
}

// G4 basis: the shared read shaping (both surfaces return these strings) sees
// every bus of the default project, sorted by busID, with the right defs.
TEST(BusFxParam, ReadShapingDescribesTheDefaultBuses)
{
    AudioEngine engine;
    engine.initialize();
    auto busList = engine.getProjectModel().getBusListTree();
    ASSERT_TRUE(busList.isValid());

    const auto buses = HDAW::readBuses(busList);
    ASSERT_EQ(buses.size(), 2u);
    EXPECT_EQ(buses[0].busID, 0);
    EXPECT_EQ(buses[0].name, "Master");
    EXPECT_EQ(buses[0].busType, "master");
    EXPECT_EQ(buses[1].busID, 1);
    EXPECT_EQ(buses[1].name, "Reverb");
    EXPECT_EQ(buses[1].busType, "fx");
    EXPECT_EQ(buses[1].fxType, "reverb");
    EXPECT_EQ(buses[1].busTarget, 0);

    auto parsed = juce::JSON::parse(juce::String(HDAW::shapeBusesJson(buses)));
    auto* arr = parsed.getArray();
    ASSERT_NE(arr, nullptr) << "shapeBusesJson must emit a bare JSON array";
    ASSERT_EQ(arr->size(), 2);
    EXPECT_EQ(static_cast<int>((*arr)[0].getProperty("busID", -1)), 0);
    EXPECT_EQ((*arr)[1].getProperty("name", "").toString(), juce::String("Reverb"));
    EXPECT_EQ((*arr)[1].getProperty("fxType", "").toString(), juce::String("reverb"));

    // list_bus_fx_params' payload: the defs the reverb return actually honors.
    auto busTree = HDAW::findBusNode(busList, 1);
    auto fxParsed = juce::JSON::parse(juce::String(HDAW::shapeBusFxParamsJson(busTree)));
    EXPECT_EQ(fxParsed.getProperty("busID", -1).toString(), juce::String("1"));
    EXPECT_EQ(fxParsed.getProperty("fxType", "").toString(), juce::String("reverb"));
    auto* params = fxParsed.getProperty("params", juce::var()).getArray();
    ASSERT_NE(params, nullptr);
    ASSERT_EQ(params->size(), 5) << "reverb exposes exactly its 5 DSP params";
    EXPECT_EQ((*params)[0].getProperty("index", -1).toString(), juce::String("0"));
    EXPECT_EQ((*params)[0].getProperty("name", "").toString(), juce::String("Room Size"));
    EXPECT_NEAR(static_cast<double>((*params)[0].getProperty("maxValue", 0.0)), 1.0, 1e-6);
    EXPECT_NEAR(static_cast<double>((*params)[0].getProperty("defaultValue", 0.0)), 0.5, 1e-6);
    EXPECT_NEAR(static_cast<double>((*params)[0].getProperty("value", 0.0)), 0.5, 1e-6)
        << "an absent tree param reads back as the def default";

    // A value written by the command is what the readback reports.
    std::string error;
    ASSERT_TRUE(engine.getProjectCommands().setBusFxParam(1, 0, 0.9f, error)) << error;
    auto after = juce::JSON::parse(juce::String(HDAW::shapeBusFxParamsJson(busTree)));
    auto* afterParams = after.getProperty("params", juce::var()).getArray();
    ASSERT_NE(afterParams, nullptr);
    EXPECT_NEAR(static_cast<double>((*afterParams)[0].getProperty("value", 0.0)), 0.9, 1e-6);

    // The read-path failures both surfaces report verbatim.
    EXPECT_TRUE(HDAW::findFxBusForRead(busList, 1).bus.isValid());
    EXPECT_EQ(HDAW::findFxBusForRead(busList, 999).error, "no bus with id 999");
    EXPECT_EQ(HDAW::findFxBusForRead(busList, 0).error,
              "bus 0 is not an fx bus (busType \"master\")");
}

// ─── Slice E: the filter return (2026-09-23) ────────────────────────────────
// A return can be high-passed: the bus `filter` chain runs the SAME
// InternalFilter DSP a track's internal filter slot runs (extracted verbatim),
// so the classic dub move — rolling the lows off a delay return so the repeats
// stop muddying the bass — becomes expressible. The eq return is still a single
// PEAK filter and cannot do this. Assertions are on the RENDERED signal and on
// the LIVE graph, never on the stored value alone.
namespace {

// Steady-state RMS of a continuous 0.5-amplitude `freqHz` sine that has been
// run through `proc` for `warmupBlocks` blocks and then measured over
// `measureBlocks`. The phase runs across blocks (a per-block phase reset would
// inject a step the filter would ring on).
template <typename Processor>
double steadySineRms(Processor& proc, double freqHz, int warmupBlocks = 4, int measureBlocks = 8)
{
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    double sum = 0.0;
    int measured = 0;
    for (int b = 0; b < warmupBlocks + measureBlocks; ++b)
    {
        buffer.clear();
        for (int s = 0; s < 512; ++s)
        {
            const float v = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi
                                            * freqHz * (s + b * 512) / 44100.0);
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, s, v);
        }
        proc.processBlock(buffer, midi);
        if (b < warmupBlocks) continue;
        ++measured;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 512; ++s)
                sum += static_cast<double>(buffer.getSample(ch, s)) * buffer.getSample(ch, s);
    }
    return std::sqrt(sum / (double) (measured * 2 * 512));
}

// A fresh filter bus with Cutoff/Mode set, fed a steady sine.
double filterBusSineRms(int mode, float cutoffHz, double freqHz)
{
    HDAW::FxBusProcessor fx("Probe", "filter");
    fx.prepareToPlay(44100.0, 512);
    fx.setParam(0, cutoffHz);                        // Cutoff
    fx.setParam(1, static_cast<float>(mode));        // Mode: 0=LP, 1=HP, 2=BP
    return steadySineRms(fx, freqHz);
}

// Steady-state gain of the extracted filter at `freqHz` (0.5-amplitude sine in,
// RMS ratio out) — the analytic anchor for the TPT solve.
double filterGainAt(HDAW::InternalFilter& f, double freqHz, int samples = 200000)
{
    double sum = 0.0;
    int measured = 0;
    for (int i = 0; i < samples; ++i)
    {
        const float x = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi
                                        * freqHz * i / 44100.0);
        const float y = f.processSample(0, x);
        if (i < samples / 2) continue;
        ++measured;
        sum += static_cast<double>(y) * y;
    }
    const double inRms = 0.5 / std::sqrt(2.0);
    return std::sqrt(sum / measured) / inRms;
}

} // namespace

// G4 (analytic anchor): the TPT solve is exact AT the cutoff, where all three
// modes have gain 1/k = Resonance/2 — the property the 2026-09-09 hand-rolled
// variant broke (it never swept its cutoff). Measured on the extracted class
// directly, in the steady state, with the values the surfaces advertise.
TEST(InternalFilterDsp, IsExactAtTheCutoffInEveryMode)
{
    for (const float res : { 0.7f, 2.0f })
    {
        const float expected = res / 2.0f;           // 1/k, k = 2/Q
        for (const int mode : { 0, 1, 2 })
        {
            HDAW::InternalFilter f;
            f.prepare(44100.0);
            f.setParam(0, 1000.0f);
            f.setParam(1, static_cast<float>(mode));
            f.setParam(2, res);
            EXPECT_NEAR(filterGainAt(f, 1000.0), expected, 0.01)
                << "mode " << mode << " resonance " << res;
        }
    }

    // A 1 kHz lowpass passes 100 Hz and rejects 5 kHz (the sweep is real, not a
    // pass-through); highpass is the mirror image. Analytic TPT values.
    HDAW::InternalFilter lp;
    lp.prepare(44100.0);
    lp.setParam(0, 1000.0f);
    lp.setParam(1, 0.0f);
    lp.setParam(2, 0.7f);
    EXPECT_NEAR(filterGainAt(lp, 100.0), 0.971, 0.02);
    EXPECT_NEAR(filterGainAt(lp, 5000.0), 0.033, 0.02);

    HDAW::InternalFilter hp;
    hp.prepare(44100.0);
    hp.setParam(0, 1000.0f);
    hp.setParam(1, 1.0f);
    hp.setParam(2, 0.7f);
    EXPECT_NEAR(filterGainAt(hp, 100.0), 0.0097, 0.01);
    EXPECT_NEAR(filterGainAt(hp, 5000.0), 0.902, 0.02);
}

// G5 at the DSP entry: every param clamps to its def, Mode is the int enum it
// is advertised as (rounded, so a fractional value cannot fall through the mode
// switch to lowpass), and an unknown index mutates nothing.
TEST(InternalFilterDsp, ClampsEveryParamAndIgnoresUnknownIndexes)
{
    HDAW::InternalFilter f;
    f.prepare(44100.0);

    f.setParam(0, 1.0f);        EXPECT_FLOAT_EQ(f.getParam(0), 20.0f);
    f.setParam(0, 1.0e6f);      EXPECT_FLOAT_EQ(f.getParam(0), 20000.0f);
    f.setParam(1, 9.0f);        EXPECT_FLOAT_EQ(f.getParam(1), 2.0f);
    f.setParam(1, -3.0f);       EXPECT_FLOAT_EQ(f.getParam(1), 0.0f);
    f.setParam(1, 1.6f);        EXPECT_FLOAT_EQ(f.getParam(1), 2.0f) << "Mode is an int enum";
    f.setParam(2, 0.0f);        EXPECT_FLOAT_EQ(f.getParam(2), 0.1f);
    f.setParam(2, 99.0f);       EXPECT_FLOAT_EQ(f.getParam(2), 10.0f);

    f.setParam(0, 500.0f);
    f.setParam(9, 123.0f);
    f.setParam(-1, 123.0f);
    EXPECT_FLOAT_EQ(f.getParam(0), 500.0f) << "an unknown index still moved the filter";
    EXPECT_FLOAT_EQ(f.getParam(9), 0.0f);
}

// G2 — the point of the slice: the filter return changes the audio it passes.
// A 100 Hz "bass" send into a 1 kHz-cutoff return keeps ~0.97 of its level in
// Lowpass and ~0.01 in Highpass; a 5 kHz send is the mirror image; and moving
// the cutoff in Lowpass moves the band. A param that reads back but never
// reaches the DSP fails here.
TEST(BusFxParam, FilterBusModeAndCutoffChangeTheRenderedAudio)
{
    const double bassLp   = filterBusSineRms(0, 1000.0f, 100.0);
    const double bassHp   = filterBusSineRms(1, 1000.0f, 100.0);
    const double trebleLp = filterBusSineRms(0, 1000.0f, 5000.0);
    const double trebleHp = filterBusSineRms(1, 1000.0f, 5000.0);
    const double bassLpLowCut = filterBusSineRms(0, 250.0f, 100.0);

    EXPECT_NEAR(bassLp, 0.343, 0.03) << "Lowpass @1k must pass 100 Hz";
    EXPECT_LT(bassHp, bassLp * 0.05)
        << "Highpass @1k did not remove the low band (lp=" << bassLp << " hp=" << bassHp << ")";
    EXPECT_NEAR(trebleHp, 0.319, 0.03) << "Highpass @1k must pass 5 kHz";
    EXPECT_LT(trebleLp, trebleHp * 0.1)
        << "Lowpass @1k did not remove the high band (lp=" << trebleLp << " hp=" << trebleHp << ")";
    EXPECT_GT(bassLp, bassLpLowCut * 1.15)
        << "the Cutoff param did not move the low band (1k=" << bassLp
        << " 250=" << bassLpLowCut << ")";

    // Mode is the enum the def advertises, and the readback reports what the DSP
    // runs.
    HDAW::FxBusProcessor fx("Probe", "filter");
    fx.prepareToPlay(44100.0, 512);
    EXPECT_FLOAT_EQ(fx.getParam(0), 1000.0f) << "an untouched filter return runs the def defaults";
    EXPECT_FLOAT_EQ(fx.getParam(1), 0.0f);
    EXPECT_FLOAT_EQ(fx.getParam(2), 0.7f);
    fx.setParam(1, 5.0f);   EXPECT_FLOAT_EQ(fx.getParam(1), 2.0f);
    fx.setParam(0, 1.0f);   EXPECT_FLOAT_EQ(fx.getParam(0), 20.0f);
    fx.setParam(0, 1.0e6f); EXPECT_FLOAT_EQ(fx.getParam(0), 20000.0f);
    fx.setParam(2, 0.0f);   EXPECT_FLOAT_EQ(fx.getParam(2), 0.1f);
    fx.setParam(2, 99.0f);  EXPECT_FLOAT_EQ(fx.getParam(2), 10.0f);
    fx.setParam(7, 0.5f);   EXPECT_FLOAT_EQ(fx.getParam(7), 0.0f) << "no such param";
}

// G1/G5: createBus accepts "filter", the live return is a filter chain running
// the track filter's defs, its params clamp at the command AND the processor, an
// unknown paramIndex is rejected with no mutation, and the values survive a
// rebuild.
TEST(BusSendCreate, CreateFilterBusAndShapeItsParams)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto& cmds = engine.getProjectCommands();
    auto busList = engine.getProjectModel().getBusListTree();
    const auto bus = cmds.createBus("fx", "Dub HPF", "filter", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    EXPECT_EQ(bus.busID, 2);                 // 0 = master, 1 = default "Reverb"

    auto busTree = findBusInTree(busList, bus.busID);
    ASSERT_TRUE(busTree.isValid());
    EXPECT_EQ(busTree.getProperty(IDs::busType).toString(), juce::String("fx"));
    EXPECT_EQ(busTree.getProperty(IDs::fxType).toString(), juce::String("filter"));
    EXPECT_EQ(static_cast<int>(busTree.getProperty(IDs::busTarget)), 0);

    auto* live = liveFxBus(engine, bus.busID);
    ASSERT_NE(live, nullptr) << "the filter bus has no live FxBusProcessor";
    EXPECT_EQ(live->getFxType(), juce::String("filter"));
    EXPECT_FLOAT_EQ(live->getParam(0), 1000.0f) << "Cutoff default";
    EXPECT_FLOAT_EQ(live->getParam(1), 0.0f) << "Mode default = lowpass";
    EXPECT_FLOAT_EQ(live->getParam(2), 0.7f) << "Resonance default";

    // The read surface (list_bus_fx_params' payload) reports exactly the three
    // params, with the track filter's ranges.
    auto fxParsed = juce::JSON::parse(juce::String(HDAW::shapeBusFxParamsJson(busTree)));
    EXPECT_EQ(fxParsed.getProperty("fxType", "").toString(), juce::String("filter"));
    auto* params = fxParsed.getProperty("params", juce::var()).getArray();
    ASSERT_NE(params, nullptr);
    ASSERT_EQ(params->size(), 3);
    EXPECT_EQ((*params)[0].getProperty("name", "").toString(), juce::String("Cutoff"));
    EXPECT_NEAR(static_cast<double>((*params)[0].getProperty("minValue", 0.0)), 20.0, 1e-6);
    EXPECT_NEAR(static_cast<double>((*params)[0].getProperty("maxValue", 0.0)), 20000.0, 1e-6);
    EXPECT_EQ((*params)[1].getProperty("name", "").toString(), juce::String("Mode"));
    EXPECT_NEAR(static_cast<double>((*params)[1].getProperty("maxValue", 0.0)), 2.0, 1e-6);
    EXPECT_EQ((*params)[2].getProperty("name", "").toString(), juce::String("Resonance"));
    EXPECT_NEAR(static_cast<double>((*params)[2].getProperty("minValue", 0.0)), 0.1, 1e-6);

    std::string error;
    ASSERT_TRUE(cmds.setBusFxParam(bus.busID, 1, 9.0f, error)) << error;   // Mode above the enum
    EXPECT_FLOAT_EQ(live->getParam(1), 2.0f) << "Mode was not clamped to the def";
    EXPECT_NEAR(static_cast<double>(busTree.getProperty("param_1")), 2.0, 1e-6);
    ASSERT_TRUE(cmds.setBusFxParam(bus.busID, 0, 5.0f, error)) << error;   // Cutoff below the def
    EXPECT_FLOAT_EQ(live->getParam(0), 20.0f);
    ASSERT_TRUE(cmds.setBusFxParam(bus.busID, 2, 99.0f, error)) << error;  // Resonance above the def
    EXPECT_FLOAT_EQ(live->getParam(2), 10.0f);

    // Unknown paramIndex: rejected, and neither the tree nor the processor moves.
    error.clear();
    const int propsBefore = busTree.getNumProperties();
    EXPECT_FALSE(cmds.setBusFxParam(bus.busID, 9, 0.5f, error));
    EXPECT_NE(error.find("out of range"), std::string::npos) << error;
    EXPECT_EQ(busTree.getNumProperties(), propsBefore) << "a rejected index still wrote the tree";
    EXPECT_FLOAT_EQ(live->getParam(2), 10.0f) << "a rejected index still moved the processor";

    // Gate 1/6/10: the filter chain and its params survive a rebuild.
    engine.getMainProcessor()->rebuildRoutingGraph();
    live = liveFxBus(engine, bus.busID);
    ASSERT_NE(live, nullptr) << "the filter return was lost on rebuild";
    EXPECT_EQ(live->getFxType(), juce::String("filter"));
    EXPECT_FLOAT_EQ(live->getParam(0), 20.0f) << "Cutoff lost on rebuild";
    EXPECT_FLOAT_EQ(live->getParam(1), 2.0f) << "Mode lost on rebuild";
    EXPECT_FLOAT_EQ(live->getParam(2), 10.0f) << "Resonance lost on rebuild";
}

// G3 — the deliverable, end to end: delay bus (busTarget = filter bus) ->
// filter bus (Mode=HP, Cutoff 250 Hz) -> master, with a send into the delay bus.
// The live graph must have the filter bus as the delay bus's parent (busTarget
// already nests buses — no new routing), and what reaches the master must come
// out high-passed: the same chain in Lowpass keeps the low band, Highpass drops
// it, measured on the live processors in graph order.
TEST(BusFxParam, FilterBusChainedBehindADelayBusHighPassesTheReturn)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);
    auto& cmds = engine.getProjectCommands();
    auto busList = engine.getProjectModel().getBusListTree();

    // Create the buses BEFORE the graph is prepared, so the single prepare covers
    // them. A bus created after the last prepare is never prepared itself
    // (MainAudioProcessor::rebuildRoutingGraph only re-prepares when
    // getSampleRate() > 0), and processing it then indexes an unsized scratch
    // buffer and an unallocated delay line - measured 2026-09-23: channel-1 inf
    // followed by an access violation. This ordering is also what a real session
    // does: the tree exists when the device opens.
    const auto hpf = cmds.createBus("fx", "Dub HPF", "filter", 0);
    ASSERT_TRUE(hpf.ok) << hpf.error;
    const auto delay = cmds.createBus("fx", "Dub Delay", "delay", hpf.busID);
    ASSERT_TRUE(delay.ok) << delay.error;
    const auto send = cmds.createSend(0, delay.busID, 1.0f, false);
    ASSERT_TRUE(send.ok) << send.error;

    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    EXPECT_EQ(static_cast<int>(findBusInTree(busList, delay.busID).getProperty(IDs::busTarget)),
              hpf.busID) << "the delay bus must target the filter bus";
    EXPECT_EQ(findBusInTree(busList, hpf.busID).getProperty(IDs::fxType).toString(),
              juce::String("filter"));

    // Look the live processors up AFTER the prepare: a re-prepare rebuilds the
    // RoutingManager, so pointers captured earlier would dangle.
    auto* rm = engine.getMainProcessor()->getRoutingManager();
    ASSERT_NE(rm, nullptr);
    auto* liveHpf = rm->getFxBus(hpf.busID);
    auto* liveDelay = rm->getFxBus(delay.busID);
    ASSERT_NE(liveHpf, nullptr);
    ASSERT_NE(liveDelay, nullptr);
    EXPECT_EQ(liveDelay->getFxType(), juce::String("delay"));
    EXPECT_EQ(liveHpf->getFxType(), juce::String("filter"));

    // The live graph: send -> delay bus -> filter bus -> master. The graph runs
    // the buses in that order, so the filter is what the master hears.
    const auto hpfNode = nodeIdOfLiveProcessor(engine, liveHpf);
    const auto delayNode = nodeIdOfLiveProcessor(engine, liveDelay);
    const auto masterNode = nodeIdOfLiveProcessor(engine, rm->getMasterBus());
    ASSERT_NE(hpfNode.uid, 0u);
    ASSERT_NE(delayNode.uid, 0u);
    ASSERT_NE(masterNode.uid, 0u);
    EXPECT_TRUE(liveGraphHasConnection(engine, delayNode, 0, hpfNode, 0))
        << "the delay bus is not connected to the filter bus (L)";
    EXPECT_TRUE(liveGraphHasConnection(engine, delayNode, 1, hpfNode, 1))
        << "the delay bus is not connected to the filter bus (R)";
    EXPECT_TRUE(liveGraphHasConnection(engine, hpfNode, 0, masterNode, 0))
        << "the filter bus is not connected to the master";
    auto sends = liveSendProcessors(engine);
    ASSERT_EQ(sends.size(), 1u);
    EXPECT_TRUE(liveGraphHasConnection(engine, nodeIdOfLiveProcessor(engine, sends[0]), 0,
                                       delayNode, 0))
        << "the send does not feed the delay bus";

    // The chain's params, through the command surface: a pure-wet 20 ms repeat
    // (so the filter sees a delayed copy of the send) and a 250 Hz filter.
    std::string error;
    ASSERT_TRUE(cmds.setBusFxParam(delay.busID, 0, 0.02f, error)) << error;
    ASSERT_TRUE(cmds.setBusFxParam(delay.busID, 1, 0.0f, error)) << error;
    ASSERT_TRUE(cmds.setBusFxParam(delay.busID, 2, 1.0f, error)) << error;
    ASSERT_TRUE(cmds.setBusFxParam(hpf.busID, 0, 250.0f, error)) << error;
    ASSERT_TRUE(cmds.setBusFxParam(hpf.busID, 2, 0.7f, error)) << error;

    // Render the chain in graph order: send -> delay bus -> filter bus. The
    // send's own input path is a 100 Hz "bass" line; the filter's mode is the
    // only thing that changes between the two renders.
    auto renderBass = [&](float mode)
    {
        std::fprintf(stderr, "MARK render start mode=%g\n", (double) mode);
        EXPECT_TRUE(cmds.setBusFxParam(hpf.busID, 1, mode, error)) << error;
        std::fprintf(stderr, "MARK param set\n");
        EXPECT_FLOAT_EQ(liveHpf->getParam(1), mode) << "the command did not reach the live filter";
        HDAW::SendProcessor sendProc;
        sendProc.prepareToPlay(44100.0, 512);
        sendProc.setSendLevel(1.0f);
        std::fprintf(stderr, "MARK send prepared\n");
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        double sum = 0.0;
        int measured = 0;
        for (int b = 0; b < 12; ++b)
        {
            buffer.clear();
            for (int s = 0; s < 512; ++s)
            {
                const float v = 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi
                                                * 100.0 * (s + b * 512) / 44100.0);
                for (int ch = 0; ch < 2; ++ch)
                    buffer.setSample(ch, s, v);
            }
            sendProc.processBlock(buffer, midi);
            liveDelay->processBlock(buffer, midi);
            liveHpf->processBlock(buffer, midi);
            if (b < 4) continue;
            ++measured;
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 512; ++s)
                    sum += static_cast<double>(buffer.getSample(ch, s)) * buffer.getSample(ch, s);
            std::fprintf(stderr, "MARK b=%d sum=%g measured=%d\n", b, sum, measured);
        }
        std::fprintf(stderr, "MARK render end sum=%g measured=%d ret=%g\n", sum, measured,
                     std::sqrt(sum / (double) (measured * 2 * 512)));
        return std::sqrt(sum / (double) (measured * 2 * 512));
    };

    const double lowpass  = renderBass(0.0f);
    const double highpass = renderBass(1.0f);

    EXPECT_GT(lowpass, 0.15) << "the chain does not pass the send at all (lp=" << lowpass << ")";
    EXPECT_LT(highpass, lowpass * 0.35)
        << "the chained return is not high-passed (lp=" << lowpass << " hp=" << highpass << ")";
}

// ─── Bus RE-PARENTING (2026-09-23, set_bus_target) ───────────────────────────
// busTarget used to be writable only by createBus, so a return's parent was fixed
// at creation and a mis-ordered chain had to be torn down and recreated. These
// tests cover the three gates the plan names for the command itself: G1 (the live
// graph moves with the edit), G3 (no edit may close a parent cycle — however many
// hops deep — with the tree left byte-identical), G4 (one undo unit, one rebuild).

namespace {

// The graph node a busID is wired FROM (its live processor's node).
juce::AudioProcessorGraph::NodeID liveBusNodeId(AudioEngine& engine, int busID)
{
    return nodeIdOfLiveProcessor(engine, liveFxBus(engine, busID));
}

// The master node — what connectBusToParent resolves busTarget 0 to, so it is the
// node a bus parented to the master must actually be connected to.
juce::AudioProcessorGraph::NodeID liveMasterNodeId(AudioEngine& engine)
{
    auto* proc = engine.getMainProcessor();
    auto* rm = (proc != nullptr) ? proc->getRoutingManager() : nullptr;
    return (rm != nullptr) ? nodeIdOfLiveProcessor(engine, rm->getMasterBus()) : juce::AudioProcessorGraph::NodeID{};
}

// busTarget straight off the BUS node (-2 when there is no such bus, so a
// missing node cannot be mistaken for the master's -1).
int busTargetOf(AudioEngine& engine, int busID)
{
    return static_cast<int>(findBusInTree(engine.getProjectModel().getBusListTree(), busID)
                                .getProperty(IDs::busTarget, -2));
}

} // namespace

// G1 — the topology the gap blocked: the DEFAULT "Reverb" return (bus 1, created
// with busTarget 0) is re-parented behind a filter bus added later, with its own
// child bus following it, and then moved back. Every assertion is on the LIVE
// graph (a tree-only assertion would pass even if RoutingManager never moved).
TEST(BusSetTarget, ReparentRewiresTheLiveGraphAndCarriesTheSubtree)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);

    auto& cmds = engine.getProjectCommands();
    auto busList = engine.getProjectModel().getBusListTree();
    ASSERT_TRUE(busList.isValid());

    // Buses are created BEFORE the graph is prepared (same ordering rule as the
    // chained-filter test: a bus added after the last prepare is never prepared).
    const auto hpf = cmds.createBus("fx", "Dub HPF", "filter", 0);
    ASSERT_TRUE(hpf.ok) << hpf.error;
    const auto delay = cmds.createBus("fx", "Dub Delay", "delay", 1);   // 1 = default Reverb
    ASSERT_TRUE(delay.ok) << delay.error;

    // The default project's return, spelled out so a change to the shipped
    // project fails here with the reason rather than as a mystery graph diff.
    const auto reverb = findBusInTree(busList, 1);
    ASSERT_TRUE(reverb.isValid()) << "the default project must ship bus 1";
    EXPECT_EQ(reverb.getProperty(IDs::name).toString(), juce::String("Reverb"));
    ASSERT_EQ(static_cast<int>(reverb.getProperty(IDs::busTarget)), 0)
        << "the default Reverb return starts on the master";
    ASSERT_EQ(reverb.getProperty(IDs::fxType).toString(), juce::String("reverb"));

    ASSERT_TRUE(ensureLiveRoutingGraph(engine));
    const auto masterNode = liveMasterNodeId(engine);
    ASSERT_NE(masterNode.uid, 0u);

    // Starting topology: delay -> Reverb -> master, HPF -> master.
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, delay.busID), 0,
                                       liveBusNodeId(engine, 1), 0));
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, 1), 0, masterNode, 0));
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, hpf.busID), 0, masterNode, 0));

    // The re-parent: the EXISTING Reverb return now runs through the HPF bus.
    const auto retarget = cmds.setBusTarget(1, hpf.busID);
    ASSERT_TRUE(retarget.ok) << retarget.error;
    EXPECT_TRUE(retarget.error.empty());
    EXPECT_EQ(busTargetOf(engine, 1), hpf.busID) << "the BUS node did not take the new parent";

    // Live, after the rebuild the command issued: delay -> Reverb -> HPF -> master.
    // The child (the delay bus) is still feeding its parent — only the re-parented
    // bus's OWN edge moved — and the old Reverb -> master edge is gone, because a
    // rebuild replaces the graph rather than layering onto it.
    //
    // This edge is also the regression signal for a rebuild-ordering fix: the
    // Reverb (bus 1) now targets a bus that sits AFTER it in BUS_LIST, so a
    // create-then-connect rebuild (which resolves the parent through the nodes it
    // has made so far) fell back to the master and left the tree and the graph
    // disagreeing. Asserted, not assumed:
    EXPECT_LT(busList.indexOf(findBusInTree(busList, 1)),
              busList.indexOf(findBusInTree(busList, hpf.busID)))
        << "the re-parent below must be a forward reference in BUS_LIST order";
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, 1), 0,
                                       liveBusNodeId(engine, hpf.busID), 0))
        << "the Reverb return is not connected to the HPF bus (L)";
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, 1), 1,
                                       liveBusNodeId(engine, hpf.busID), 1))
        << "the Reverb return is not connected to the HPF bus (R)";
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, delay.busID), 0,
                                       liveBusNodeId(engine, 1), 0))
        << "re-parenting dropped the child that fed the moved bus";
    // Look master UP FRESH: setBusTarget rebuilt the graph above, so any NodeID
    // cached before that rebuild now names a node that no longer exists (a
    // stale id would make the next two assertions pass/fail vacuously).
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, hpf.busID), 0,
                                       liveMasterNodeId(engine), 0));
    EXPECT_FALSE(liveGraphHasConnection(engine, liveBusNodeId(engine, 1), 0,
                                        liveMasterNodeId(engine), 0))
        << "the Reverb return is still wired straight to the master";

    // ...and back: the parent is an ordinary editable edge, not a one-way change.
    const auto restored = cmds.setBusTarget(1, 0);
    ASSERT_TRUE(restored.ok) << restored.error;
    EXPECT_EQ(busTargetOf(engine, 1), 0);
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, 1), 0,
                                       liveMasterNodeId(engine), 0));
    EXPECT_FALSE(liveGraphHasConnection(engine, liveBusNodeId(engine, 1), 0,
                                        liveBusNodeId(engine, hpf.busID), 0))
        << "the old Reverb -> HPF edge survived the move back";
}

// G3 — the gate that matters. createBus cannot build a cycle (a new bus has no
// children); re-parenting can, and the loop can be several hops deep, so the
// check walks the PROPOSED PARENT's chain and refuses when it meets the bus being
// moved. Every refusal names its reason and leaves the tree byte-identical.
TEST(BusSetTarget, CyclesMasterAndUnknownIdsAreRefusedWithNoTreeChange)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);

    auto& cmds = engine.getProjectCommands();
    auto busList = engine.getProjectModel().getBusListTree();

    // HPF -> master, delay -> HPF, eq -> delay: so the delay is a descendant of
    // the HPF one hop away and the eq two hops away.
    const auto hpf = cmds.createBus("fx", "Dub HPF", "filter", 0);
    ASSERT_TRUE(hpf.ok) << hpf.error;
    const auto delay = cmds.createBus("fx", "Dub Delay", "delay", hpf.busID);
    ASSERT_TRUE(delay.ok) << delay.error;
    const auto eq = cmds.createBus("fx", "Dub EQ", "eq", delay.busID);
    ASSERT_TRUE(eq.ok) << eq.error;
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));

    auto* proc = engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* rmBefore = proc->getRoutingManager();
    ASSERT_NE(rmBefore, nullptr);

    // The refusal check is byte-level: the whole project tree, serialized, must
    // come back identical (a stray property write, a re-stamped node, any change
    // at all shows up here rather than passing as "no visible difference").
    const juce::String treeBefore = engine.getProjectModel().getTree().toXmlString();
    auto treeNow = [&] { return engine.getProjectModel().getTree().toXmlString(); };

    auto expectRefused = [&](int busID, int busTarget, const char* needle)
    {
        const auto r = cmds.setBusTarget(busID, busTarget);
        EXPECT_FALSE(r.ok) << "setBusTarget(" << busID << ", " << busTarget << ") was accepted";
        EXPECT_NE(r.error.find(needle), std::string::npos) << "the reason was: " << r.error;
        EXPECT_EQ(treeNow(), treeBefore)
            << "a refused setBusTarget changed the tree (" << busID << " -> " << busTarget << ")";
    };

    // Unknown bus being moved / unknown parent / itself / the master. The master's
    // busTarget is -1 by design: it is the root of every chain.
    expectRefused(999, 0, "no bus with id 999");
    expectRefused(hpf.busID, 999, "does not exist");
    expectRefused(hpf.busID, hpf.busID, "its own busTarget");
    expectRefused(0, hpf.busID, "master");
    // A DESCENDANT one hop away...
    expectRefused(hpf.busID, delay.busID, "descendant");
    // ...and one several hops away: the HPF cannot move under the eq, which is
    // reached only through the delay.
    expectRefused(hpf.busID, eq.busID, "descendant");
    // The same loop approached from the other end (moving the delay under itself
    // via the eq) is refused too.
    expectRefused(delay.busID, eq.busID, "descendant");
    expectRefused(delay.busID, delay.busID, "its own busTarget");

    // G4: every refusal above rejected BEFORE any edit, so not one of them issued
    // a rebuild — the same RoutingManager instance is still live.
    EXPECT_EQ(proc->getRoutingManager(), rmBefore)
        << "a refused setBusTarget rebuilt the routing graph";

    // ...and the chain those refusals were protecting is exactly where it was.
    EXPECT_EQ(busTargetOf(engine, hpf.busID), 0);
    EXPECT_EQ(busTargetOf(engine, delay.busID), hpf.busID);
    EXPECT_EQ(busTargetOf(engine, eq.busID), delay.busID);
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, eq.busID), 0,
                                       liveBusNodeId(engine, delay.busID), 0));
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, delay.busID), 0,
                                       liveBusNodeId(engine, hpf.busID), 0));
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, hpf.busID), 0,
                                       liveMasterNodeId(engine), 0));

    // The walk is not a blanket "no": it terminates at the master, so the deepest
    // bus may still move to busTarget 0 (0 is always the master, never looked up).
    const auto toMaster = cmds.setBusTarget(eq.busID, 0);
    ASSERT_TRUE(toMaster.ok) << toMaster.error;
    EXPECT_EQ(busTargetOf(engine, eq.busID), 0);
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, eq.busID), 0,
                                       liveMasterNodeId(engine), 0));
    EXPECT_FALSE(liveGraphHasConnection(engine, liveBusNodeId(engine, eq.busID), 0,
                                        liveBusNodeId(engine, delay.busID), 0));
}

// G4 — atomicity: a re-parent is ONE undo unit. A single undo restores the old
// parent, and the next rebuild projects the restored tree.
TEST(BusSetTarget, OneUndoRevertsAReparent)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedTrack(engine, 1), 0);

    auto& cmds = engine.getProjectCommands();
    auto busList = engine.getProjectModel().getBusListTree();
    const auto hpf = cmds.createBus("fx", "Dub HPF", "filter", 0);
    ASSERT_TRUE(hpf.ok) << hpf.error;
    const auto delay = cmds.createBus("fx", "Dub Delay", "delay", 0);
    ASSERT_TRUE(delay.ok) << delay.error;
    ASSERT_TRUE(ensureLiveRoutingGraph(engine));
    ASSERT_EQ(busTargetOf(engine, delay.busID), 0);

    const auto retarget = cmds.setBusTarget(delay.busID, hpf.busID);
    ASSERT_TRUE(retarget.ok) << retarget.error;
    ASSERT_EQ(busTargetOf(engine, delay.busID), hpf.busID);
    ASSERT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, delay.busID), 0,
                                       liveBusNodeId(engine, hpf.busID), 0));

    ASSERT_TRUE(cmds.canUndo());
    cmds.undo();

    // ONE step: the parent is back and the buses the test built are still there
    // (the unit is the property write, not the session).
    EXPECT_EQ(busTargetOf(engine, delay.busID), 0) << "one undo did not restore the parent";
    EXPECT_TRUE(findBusInTree(busList, hpf.busID).isValid()) << "the undo went too far";
    EXPECT_TRUE(findBusInTree(busList, delay.busID).isValid());
    EXPECT_EQ(busTargetOf(engine, hpf.busID), 0);

    // AudioEngineCommands::undo rewrites the tree but does not project it, so the
    // next rebuild is what carries the restored parent into the graph — and that
    // graph must then read exactly what the tree says.
    engine.getMainProcessor()->rebuildRoutingGraph();
    EXPECT_TRUE(liveGraphHasConnection(engine, liveBusNodeId(engine, delay.busID), 0,
                                       liveMasterNodeId(engine), 0));
    EXPECT_FALSE(liveGraphHasConnection(engine, liveBusNodeId(engine, delay.busID), 0,
                                        liveBusNodeId(engine, hpf.busID), 0))
        << "the undone edge is still live in the graph";
}