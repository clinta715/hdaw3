#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/RoutingManager.h"
#include "engine/SendProcessor.h"
#include "model/ProjectModel.h"
#include "common/ReadModel.h"

TEST(Send, ReadModelReturnsSends)
{
    AudioEngine engine;
    engine.initialize();

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