#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/RoutingManager.h"
#include "engine/SendProcessor.h"
#include "engine/Track.h"
#include "engine/FxBusProcessor.h"
#include "engine/GroupBusProcessor.h"
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

// Bootstraps a live RoutingManager on a deviceless session (no-op when a device
// already ran prepareToPlay). Mirrors the incremental-routing test harness.
bool ensureLiveRoutingGraph(AudioEngine& engine)
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

    const auto badFxType = cmds.createBus("fx", "Nope", "filter", 0);
    EXPECT_FALSE(badFxType.ok);
    EXPECT_NE(badFxType.error.find("reverb"), std::string::npos)
        << "the error must name the accepted fx types: " << badFxType.error;

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
    bogus.setProperty(IDs::name, "Filter Bus", nullptr);
    bogus.setProperty(IDs::busID, 42, nullptr);
    bogus.setProperty(IDs::busType, "fx", nullptr);
    bogus.setProperty(IDs::busTarget, 0, nullptr);
    bogus.setProperty(IDs::fxType, "filter", nullptr);
    busList.addChild(bogus, -1, nullptr);
    error.clear();
    EXPECT_FALSE(cmds.setBusFxParam(42, 0, 0.5f, error));
    EXPECT_NE(error.find("unsupported fxType \"filter\""), std::string::npos) << error;
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

// The delay bus is the full 5-param delay (the shared InternalDelay DSP backs
// all five), the line's capacity is sized so the def's 5 s top is reachable
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
    for (const char* type : { "reverb", "eq", "compressor", "delay" })
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
    // shared InternalDelay DSP applies all five, so nothing here is a fake
    // param (G5) — including the 0.99 feedback top that guards the recursion.
    const auto trackDelayDefs = HDAW::TrackFXSlot::getParamDefsForType("delay");
    const auto& busDelayDefs = HDAW::busFxParamDefs("delay");
    ASSERT_EQ(trackDelayDefs.size(), 5u) << "the track delay is the 5-param one";
    ASSERT_EQ(busDelayDefs.size(), 5u) << "the delay return must expose the same 5 params";
    EXPECT_EQ(juce::String(busDelayDefs[0].name), juce::String("Delay Time"));
    EXPECT_EQ(juce::String(busDelayDefs[1].name), juce::String("Feedback"));
    EXPECT_EQ(juce::String(busDelayDefs[2].name), juce::String("Mix"));
    EXPECT_EQ(juce::String(busDelayDefs[3].name), juce::String("SyncToTempo"));
    EXPECT_EQ(juce::String(busDelayDefs[4].name), juce::String("Division"));
    EXPECT_FLOAT_EQ(busDelayDefs[1].max, 0.99f);
    EXPECT_FLOAT_EQ(busDelayDefs[4].max, 6.0f);

    EXPECT_TRUE(HDAW::busFxParamDefs("filter").empty());
    EXPECT_TRUE(HDAW::busFxParamDefs("").empty());
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("reverb", 0, 9.0f), 1.0f);
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("reverb", 9, 9.0f), 9.0f) << "unknown index is not a clamp";
    EXPECT_FLOAT_EQ(HDAW::clampBusFxParam("delay", 1, 4.0f), 0.99f);
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