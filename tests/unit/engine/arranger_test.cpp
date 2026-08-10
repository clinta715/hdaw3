#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "common/ReadModel.h"

class ArrangerTest : public ::testing::Test {
protected:
    AudioEngine engine;
    ProjectCommands* cmds = nullptr;

    void SetUp() override {
        engine.initialize();
        cmds = &engine.getProjectCommands();
    }
};

// --- Region CRUD ---

TEST_F(ArrangerTest, AddRegion) {
    auto rid = cmds->addArrangerRegion("Intro", 0.0, 8.0, 0xFF0000FF);
    EXPECT_FALSE(rid.empty());
    auto regions = engine.getReadModel().getArrangerRegions();
    ASSERT_EQ(regions.size(), 1u);
    EXPECT_EQ(regions[0].name, "Intro");
    EXPECT_DOUBLE_EQ(regions[0].startTime, 0.0);
    EXPECT_DOUBLE_EQ(regions[0].duration, 8.0);
    EXPECT_EQ(regions[0].color, 0xFF0000FF);
}

TEST_F(ArrangerTest, RemoveRegion) {
    auto rid = cmds->addArrangerRegion("Intro", 0.0, 8.0);
    cmds->removeArrangerRegion(rid);
    EXPECT_TRUE(engine.getReadModel().getArrangerRegions().empty());
}

TEST_F(ArrangerTest, RenameRegion) {
    auto rid = cmds->addArrangerRegion("Intro", 0.0, 8.0);
    cmds->setArrangerRegionName(rid, "Verse");
    EXPECT_EQ(engine.getReadModel().getArrangerRegions()[0].name, "Verse");
}

TEST_F(ArrangerTest, MoveRegion) {
    auto rid = cmds->addArrangerRegion("Intro", 0.0, 8.0);
    cmds->setArrangerRegionBounds(rid, 4.0, 12.0);
    auto regions = engine.getReadModel().getArrangerRegions();
    EXPECT_DOUBLE_EQ(regions[0].startTime, 4.0);
    EXPECT_DOUBLE_EQ(regions[0].duration, 12.0);
}

TEST_F(ArrangerTest, RecolorRegion) {
    auto rid = cmds->addArrangerRegion("Intro", 0.0, 8.0);
    cmds->setArrangerRegionColor(rid, 0xFFFF0000);
    EXPECT_EQ(engine.getReadModel().getArrangerRegions()[0].color, 0xFFFF0000);
}

// --- Chain CRUD ---

TEST_F(ArrangerTest, AddChain) {
    auto cid = cmds->addArrangerChain("Arrangement A");
    EXPECT_FALSE(cid.empty());
    auto chains = engine.getReadModel().getArrangerChains();
    ASSERT_EQ(chains.size(), 1u);
    EXPECT_EQ(chains[0].name, "Arrangement A");
    EXPECT_TRUE(chains[0].isActive);
}

TEST_F(ArrangerTest, RemoveChain) {
    auto cid = cmds->addArrangerChain("A");
    cmds->removeArrangerChain(cid);
    EXPECT_TRUE(engine.getReadModel().getArrangerChains().empty());
}

TEST_F(ArrangerTest, SingleActiveChain) {
    auto cid1 = cmds->addArrangerChain("A");
    auto cid2 = cmds->addArrangerChain("B");
    cmds->setArrangerChainActive(cid2);
    auto chains = engine.getReadModel().getArrangerChains();
    EXPECT_FALSE(chains[0].isActive);
    EXPECT_TRUE(chains[1].isActive);
}

TEST_F(ArrangerTest, ActivateDeactivatesOthers) {
    auto cid1 = cmds->addArrangerChain("A");
    auto cid2 = cmds->addArrangerChain("B");
    cmds->setArrangerChainActive(cid1);
    auto chains = engine.getReadModel().getArrangerChains();
    EXPECT_TRUE(chains[0].isActive);
    EXPECT_FALSE(chains[1].isActive);
}

// --- Chain Entries ---

TEST_F(ArrangerTest, AddChainEntry) {
    auto rid = cmds->addArrangerRegion("Verse", 8.0, 16.0);
    auto cid = cmds->addArrangerChain("A");
    int idx = cmds->addChainEntry(cid, rid, 2);
    EXPECT_EQ(idx, 0);
    auto chains = engine.getReadModel().getArrangerChains();
    ASSERT_EQ(chains[0].entries.size(), 1u);
    EXPECT_EQ(chains[0].entries[0].regionID, rid);
    EXPECT_EQ(chains[0].entries[0].repeatCount, 2);
}

TEST_F(ArrangerTest, RemoveChainEntry) {
    auto rid = cmds->addArrangerRegion("Verse", 8.0, 16.0);
    auto cid = cmds->addArrangerChain("A");
    cmds->addChainEntry(cid, rid);
    cmds->removeChainEntry(cid, 0);
    EXPECT_TRUE(engine.getReadModel().getArrangerChains()[0].entries.empty());
}

TEST_F(ArrangerTest, ReorderChainEntry) {
    auto rid1 = cmds->addArrangerRegion("A", 0.0, 8.0);
    auto rid2 = cmds->addArrangerRegion("B", 8.0, 8.0);
    auto cid = cmds->addArrangerChain("X");
    cmds->addChainEntry(cid, rid1);
    cmds->addChainEntry(cid, rid2);
    cmds->reorderChainEntry(cid, 0, 1);
    auto chains = engine.getReadModel().getArrangerChains();
    EXPECT_EQ(chains[0].entries[0].regionID, rid2);
    EXPECT_EQ(chains[0].entries[1].regionID, rid1);
}

TEST_F(ArrangerTest, SetRepeatCount) {
    auto rid = cmds->addArrangerRegion("Chorus", 24.0, 16.0);
    auto cid = cmds->addArrangerChain("A");
    cmds->addChainEntry(cid, rid, 1);
    cmds->setChainEntryRepeat(cid, 0, 3);
    EXPECT_EQ(engine.getReadModel().getArrangerChains()[0].entries[0].repeatCount, 3);
}

TEST_F(ArrangerTest, RepeatMinOne) {
    auto rid = cmds->addArrangerRegion("Chorus", 24.0, 16.0);
    auto cid = cmds->addArrangerChain("A");
    cmds->addChainEntry(cid, rid, 5);
    cmds->setChainEntryRepeat(cid, 0, 0); // should clamp to 1
    EXPECT_EQ(engine.getReadModel().getArrangerChains()[0].entries[0].repeatCount, 1);
}

// --- Cascade ---

TEST_F(ArrangerTest, RemoveRegionCascadesToChains) {
    auto rid = cmds->addArrangerRegion("Verse", 8.0, 16.0);
    auto cid = cmds->addArrangerChain("A");
    cmds->addChainEntry(cid, rid);
    cmds->removeArrangerRegion(rid);
    EXPECT_TRUE(engine.getReadModel().getArrangerChains()[0].entries.empty());
}

// --- Undo/Redo ---

TEST_F(ArrangerTest, UndoRegionAdd) {
    auto rid = cmds->addArrangerRegion("Intro", 0.0, 8.0);
    engine.getProjectModel().getUndoManager().undo();
    EXPECT_TRUE(engine.getReadModel().getArrangerRegions().empty());
}

TEST_F(ArrangerTest, RedoRegionAdd) {
    auto rid = cmds->addArrangerRegion("Intro", 0.0, 8.0);
    engine.getProjectModel().getUndoManager().undo();
    engine.getProjectModel().getUndoManager().redo();
    EXPECT_EQ(engine.getReadModel().getArrangerRegions().size(), 1u);
}
