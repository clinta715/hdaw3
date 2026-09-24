// Design B3 (docs/plans/2026-09-23-durable-refs-to-stable-ids.md) slice S3:
// the load-time migration that moves the three durable track references from
// TRACK_LIST indices to stable track ids.
//
// Before B3 a folder's `childIds`, a child's `parentId` and a SONG_PLAN cell's
// `cellTrack` stored an INDEX. B3 stores identities (`childTrackIDs`,
// `parentTrackID`, `cellTrackID`); `HDAW::migrateDurableRefsToIDs`
// (src/engine/DurableRefMigration.h) is the ONE-TIME, load-only conversion for
// files written before B3, run from ProjectSerializer::load right after
// scanAndSyncTrackIDs. These tests drive the REAL save/load path — the project
// is built with the live commands, SAVED, then the saved XML is rewritten back
// into the legacy vocabulary and loaded into a fresh engine — so the migration
// is exercised exactly as a real legacy file would exercise it.

#include <gtest/gtest.h>

#include "common/TrackIdRefs.h"
#include "engine/AudioEngine.h"
#include "engine/ProjectSerializer.h"
#include "model/ProjectModel.h"

#include <juce_core/juce_core.h>

#include <map>
#include <string>
#include <vector>

namespace {

// The names a folder claims (its childTrackIDs CSV, resolved to entities).
std::vector<std::string> childNamesOf(const juce::ValueTree& trackList, const juce::ValueTree& folder)
{
    std::vector<std::string> names;
    for (int id : HDAW::parseIDList(folder, IDs::childTrackIDs))
    {
        const int idx = HDAW::trackIndexForID(trackList, id);
        names.push_back(idx < 0 ? std::string("<none>")
                                : trackList.getChild(idx).getProperty(IDs::name).toString().toStdString());
    }
    return names;
}

// The name a track's parentTrackID names; "<none>" when it resolves to nothing.
std::string parentNameOf(const juce::ValueTree& trackList, const juce::ValueTree& track)
{
    const int idx = HDAW::trackIndexForID(trackList,
        static_cast<int>(track.getProperty(IDs::parentTrackID, -1)));
    return idx < 0 ? std::string("<none>")
                   : trackList.getChild(idx).getProperty(IDs::name).toString().toStdString();
}

// The name a SONG_PLAN cell's cellTrackID names; "<none>" when it resolves to
// nothing.
std::string cellTargetName(const juce::ValueTree& trackList, const juce::ValueTree& cell)
{
    const int idx = HDAW::trackIndexForID(trackList,
        static_cast<int>(cell.getProperty(IDs::cellTrackID, -1)));
    return idx < 0 ? std::string("<none>")
                   : trackList.getChild(idx).getProperty(IDs::name).toString().toStdString();
}

int indexOfName(const juce::ValueTree& trackList, const char* name)
{
    for (int i = 0; i < trackList.getNumChildren(); ++i)
        if (trackList.getChild(i).getProperty(IDs::name).toString() == juce::String(name))
            return i;
    return -1;
}

juce::ValueTree cellsOf(const juce::ValueTree& root)
{
    auto plan = root.getChildWithName(IDs::SONG_PLAN);
    return plan.isValid() ? plan.getChildWithName(IDs::CELLS) : juce::ValueTree();
}

// Rewrite the three durable track refs back into the LEGACY vocabulary, exactly
// as a pre-B3 saved file spells them: `parentId` / `childIds` / `cellTrack` hold
// TRACK_LIST indices, and the new `parentTrackID` / `childTrackIDs` /
// `cellTrackID` properties are REMOVED. The indices are derived from THIS tree's
// own order (the saved file's order), which is what a real legacy file's values
// are relative to.
void denormalizeToLegacy(juce::ValueTree& root)
{
    auto trackList = root.getChildWithName(IDs::TRACK_LIST);
    std::map<int, int> idToIndex;
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        const int id = static_cast<int>(trackList.getChild(i).getProperty(IDs::trackID, 0));
        if (id > 0) idToIndex[id] = i;
    }
    auto indexOf = [&idToIndex](int id) {
        const auto it = idToIndex.find(id);
        return it == idToIndex.end() ? -1 : it->second;
    };

    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto track = trackList.getChild(t);
        if (track.hasProperty(IDs::parentTrackID))
        {
            const int parentID = static_cast<int>(track.getProperty(IDs::parentTrackID, -1));
            track.setProperty(IDs::parentId, parentID < 0 ? -1 : indexOf(parentID), nullptr);
            track.removeProperty(IDs::parentTrackID, nullptr);
        }
        if (track.hasProperty(IDs::childTrackIDs))
        {
            std::string csv;
            for (int id : HDAW::parseIDList(track, IDs::childTrackIDs))
            {
                const int idx = indexOf(id);
                if (idx < 0) continue;
                if (!csv.empty()) csv += ',';
                csv += std::to_string(idx);
            }
            track.setProperty(IDs::childIds, juce::String(csv), nullptr);
            track.removeProperty(IDs::childTrackIDs, nullptr);
        }
    }

    auto cells = cellsOf(root);
    if (cells.isValid())
        for (int c = 0; c < cells.getNumChildren(); ++c)
        {
            auto cell = cells.getChild(c);
            if (!cell.hasProperty(IDs::cellTrackID)) continue;
            const int id = static_cast<int>(cell.getProperty(IDs::cellTrackID, -1));
            cell.setProperty(IDs::cellTrack, id < 0 ? -1 : indexOf(id), nullptr);
            cell.removeProperty(IDs::cellTrackID, nullptr);
        }
}

// Writes `root` to `file` as the exact XML text the serializer would emit.
bool writeTree(const juce::ValueTree& root, const juce::File& file)
{
    return file.replaceWithText(root.toXmlString());
}

juce::ValueTree readTree(const juce::File& file)
{
    return juce::ValueTree::fromXml(file.loadFileAsString());
}

void setDefaultPlan(ProjectCommands& cmds)
{
    ProjectCommands::SongPlanData plan;
    plan.bpm = 138.0;
    plan.keyRoot = 5;
    plan.scaleMode = 7;
    plan.style = "test";
    plan.seed = 42;
    plan.totalBars = 8;
    plan.sections = { { "intro", "intro", 8, 0.0, 32.0 } };
    EXPECT_TRUE(cmds.setSongPlan(plan).ok);
}

ProjectCommands::CellRecipe cellOn(const char* role, int trackIndex)
{
    ProjectCommands::CellRecipe r;
    r.section = "intro";
    r.role = role;
    r.trackId = trackIndex;
    r.sourceKind = "phrase";
    r.paramsJson = "{}";
    r.seed = 1;
    return r;
}

// The scene every case builds: [Main, Folder(type 2), ChildA, ChildB, Other]
// with ChildA/ChildB filed under Folder, two SONG_PLAN cells (one on Main, one
// on ChildA), a lane bound to a send-level pid and an LFO aimed at the same pid.
struct Scene { int mainIdx, folderIdx, childAIdx, childBIdx, otherIdx; };

Scene buildScene(AudioEngine& engine)
{
    auto& cmds = engine.getProjectCommands();
    Scene s;
    s.mainIdx   = cmds.addTrack("Main");
    s.folderIdx = cmds.addTrack("Folder", -1, -1, 2);
    s.childAIdx = cmds.addTrack("ChildA");
    s.childBIdx = cmds.addTrack("ChildB");
    s.otherIdx  = cmds.addTrack("Other");
    engine.drainPendingRoutingRebuild();

    cmds.moveTrackIntoFolder(s.childAIdx, s.folderIdx);
    cmds.moveTrackIntoFolder(s.childBIdx, s.folderIdx);

    setDefaultPlan(cmds);
    std::string err;
    EXPECT_TRUE(cmds.setCellRecipe(cellOn("onMain", s.mainIdx), &err)) << err;
    EXPECT_TRUE(cmds.setCellRecipe(cellOn("onChildA", s.childAIdx), &err)) << err;

    // G5 lane: a send-level pid (2000 + sendIndex). Deliberately NOT migrated.
    EXPECT_TRUE(cmds.addAutomationLane(s.mainIdx, "SendLevel", 2001));
    // G5 LFO aimed at the same send-level pid. Deliberately NOT migrated.
    cmds.addLfo(s.mainIdx);
    cmds.setLfoParam(s.mainIdx, 0, "targetParamID", 2001.0);

    engine.drainPendingRoutingRebuild();
    return s;
}

int laneParamID(const juce::ValueTree& trackList, int trackIdx, const char* laneName)
{
    auto autoList = trackList.getChild(trackIdx).getChildWithName(IDs::AUTOMATION_LIST);
    for (int i = 0; i < autoList.getNumChildren(); ++i)
        if (autoList.getChild(i).getProperty(IDs::name).toString() == juce::String(laneName))
            return static_cast<int>(autoList.getChild(i).getProperty(IDs::paramID, -1));
    return -1;
}

int lfoTargetParamID(const juce::ValueTree& trackList, int trackIdx)
{
    auto modList = trackList.getChild(trackIdx).getChildWithName(IDs::MODULATION_LIST);
    if (!modList.isValid() || modList.getNumChildren() == 0) return -1;
    return static_cast<int>(modList.getChild(0).getProperty(IDs::targetParamID, -1));
}

} // namespace

// G1: a project saved by the current code, DE-NORMALIZED to the legacy
// vocabulary and loaded into a FRESH engine, must leave every folder link and
// every SONG_PLAN cell resolving to the SAME track it did before — and the
// legacy properties must be gone from the loaded tree.
TEST(DurableRefMigration, LegacyLoadMigratesFolderAndCellRefsToTheSameTracks)
{
    AudioEngine engine;
    engine.initialize();
    const Scene s = buildScene(engine);

    // Capture the ground truth from the live source project.
    auto srcList = engine.getProjectModel().getTrackListTree();
    const int srcFolderIdx = indexOfName(srcList, "Folder");
    ASSERT_GE(srcFolderIdx, 0);
    const auto folderChildIDsBefore =
        HDAW::parseIDList(srcList.getChild(srcFolderIdx), IDs::childTrackIDs);
    ASSERT_EQ(folderChildIDsBefore.size(), 2u);
    const int cellTrackIDBeforeMain = static_cast<int>(
        cellsOf(engine.getProjectModel().getTree()).getChild(0).getProperty(IDs::cellTrackID, -1));
    const int cellTrackIDBeforeChildA = static_cast<int>(
        cellsOf(engine.getProjectModel().getTree()).getChild(1).getProperty(IDs::cellTrackID, -1));

    // Save with the current code, then rewrite the saved XML into the legacy
    // vocabulary — a genuine pre-B3 file on disk.
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto legacyFile = dir.getNonexistentChildFile("hdaw_b3_legacy", ".hdaw", false);
    auto stableFile = dir.getNonexistentChildFile("hdaw_b3_stable", ".hdaw", false);
    ASSERT_TRUE(HDAW::ProjectSerializer::save(engine.getProjectModel(), stableFile));

    auto legacyTree = readTree(stableFile);
    ASSERT_TRUE(legacyTree.isValid());
    ASSERT_TRUE(legacyTree.getChildWithName(IDs::TRACK_LIST).isValid());
    denormalizeToLegacy(legacyTree);
    ASSERT_TRUE(writeTree(legacyTree, legacyFile));

    // Load the legacy file into a FRESH engine — the migration runs inside load.
    AudioEngine engine2;
    engine2.initialize();
    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine2.getProjectModel(), legacyFile));

    auto tl2 = engine2.getProjectModel().getTrackListTree();
    const int folderIdx = indexOfName(tl2, "Folder");
    ASSERT_GE(folderIdx, 0);

    // Every folder link resolves to the same track it did before.
    EXPECT_EQ(childNamesOf(tl2, tl2.getChild(folderIdx)),
              (std::vector<std::string>{ "ChildA", "ChildB" }));
    const int childAIdx = indexOfName(tl2, "ChildA");
    const int childBIdx = indexOfName(tl2, "ChildB");
    ASSERT_GE(childAIdx, 0);
    ASSERT_GE(childBIdx, 0);
    EXPECT_EQ(parentNameOf(tl2, tl2.getChild(childAIdx)), "Folder");
    EXPECT_EQ(parentNameOf(tl2, tl2.getChild(childBIdx)), "Folder");
    // ...and the ids are exactly the ones captured before the round trip.
    EXPECT_EQ(HDAW::parseIDList(tl2.getChild(folderIdx), IDs::childTrackIDs),
              folderChildIDsBefore);

    // Every cell still targets the same track.
    auto cells2 = cellsOf(engine2.getProjectModel().getTree());
    ASSERT_EQ(cells2.getNumChildren(), 2);
    EXPECT_EQ(cellTargetName(tl2, cells2.getChild(0)), "Main");
    EXPECT_EQ(cellTargetName(tl2, cells2.getChild(1)), "ChildA");
    EXPECT_EQ(static_cast<int>(cells2.getChild(0).getProperty(IDs::cellTrackID, -1)),
              cellTrackIDBeforeMain);
    EXPECT_EQ(static_cast<int>(cells2.getChild(1).getProperty(IDs::cellTrackID, -1)),
              cellTrackIDBeforeChildA);

    // The legacy vocabulary is gone from the loaded tree; only ids remain.
    for (int t = 0; t < tl2.getNumChildren(); ++t)
    {
        EXPECT_FALSE(tl2.getChild(t).hasProperty(IDs::parentId))
            << "the migration must drop the legacy parentId";
        EXPECT_FALSE(tl2.getChild(t).hasProperty(IDs::childIds))
            << "the migration must drop the legacy childIds";
    }
    EXPECT_TRUE(tl2.getChild(folderIdx).hasProperty(IDs::childTrackIDs));
    for (int c = 0; c < cells2.getNumChildren(); ++c)
    {
        EXPECT_FALSE(cells2.getChild(c).hasProperty(IDs::cellTrack));
        EXPECT_TRUE(cells2.getChild(c).hasProperty(IDs::cellTrackID));
    }

    legacyFile.deleteFile();
    stableFile.deleteFile();
}

// G3: save -> load -> save is byte-stable for the reference properties (the
// second save carries the SAME new property values), it never writes the legacy
// vocabulary, and a plain load performs no undoable mutation.
TEST(DurableRefMigration, SaveLoadSaveKeepsTheNewVocabularyByteStable)
{
    AudioEngine engine;
    engine.initialize();
    buildScene(engine);

    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto firstFile  = dir.getNonexistentChildFile("hdaw_b3_first", ".hdaw", false);
    auto secondFile = dir.getNonexistentChildFile("hdaw_b3_second", ".hdaw", false);
    ASSERT_TRUE(HDAW::ProjectSerializer::save(engine.getProjectModel(), firstFile));

    AudioEngine engine2;
    engine2.initialize();
    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine2.getProjectModel(), firstFile));

    // A plain load is not an undoable op: the history is empty afterwards.
    EXPECT_FALSE(engine2.getProjectModel().getUndoManager().canUndo())
        << "load must not leave an undo step (migration/backfill are nullptr-undo)";

    ASSERT_TRUE(HDAW::ProjectSerializer::save(engine2.getProjectModel(), secondFile));

    auto firstTree  = readTree(firstFile);
    auto secondTree = readTree(secondFile);
    ASSERT_TRUE(firstTree.isValid());
    ASSERT_TRUE(secondTree.isValid());
    auto firstList  = firstTree.getChildWithName(IDs::TRACK_LIST);
    auto secondList = secondTree.getChildWithName(IDs::TRACK_LIST);
    ASSERT_EQ(firstList.getNumChildren(), secondList.getNumChildren());

    // The reference properties are identical across the two saves, and the
    // second save carries ONLY the new vocabulary.
    bool sawFolderRefs = false;
    for (int t = 0; t < secondList.getNumChildren(); ++t)
    {
        auto a = firstList.getChild(t);
        auto b = secondList.getChild(t);
        EXPECT_EQ(a.getProperty(IDs::name).toString(), b.getProperty(IDs::name).toString());
        EXPECT_EQ(a.getProperty(IDs::childTrackIDs, "").toString(),
                  b.getProperty(IDs::childTrackIDs, "").toString());
        EXPECT_EQ(static_cast<int>(a.getProperty(IDs::parentTrackID, -999)),
                  static_cast<int>(b.getProperty(IDs::parentTrackID, -999)));
        EXPECT_FALSE(b.hasProperty(IDs::parentId));
        EXPECT_FALSE(b.hasProperty(IDs::childIds));
        if (b.hasProperty(IDs::childTrackIDs) && !b.getProperty(IDs::childTrackIDs, "").toString().isEmpty())
            sawFolderRefs = true;
    }
    EXPECT_TRUE(sawFolderRefs) << "the folder's childTrackIDs must be written";

    auto firstCells  = cellsOf(firstTree);
    auto secondCells = cellsOf(secondTree);
    ASSERT_EQ(firstCells.getNumChildren(), secondCells.getNumChildren());
    for (int c = 0; c < secondCells.getNumChildren(); ++c)
    {
        EXPECT_EQ(static_cast<int>(firstCells.getChild(c).getProperty(IDs::cellTrackID, -999)),
                  static_cast<int>(secondCells.getChild(c).getProperty(IDs::cellTrackID, -999)));
        EXPECT_FALSE(secondCells.getChild(c).hasProperty(IDs::cellTrack));
    }

    // Textual belt-and-braces: no legacy attribute spelling in the XML.
    const auto xml = secondFile.loadFileAsString();
    EXPECT_EQ(xml.indexOf("parentId=\""), -1) << "no legacy parentId attribute";
    EXPECT_EQ(xml.indexOf("childIds=\""), -1) << "no legacy childIds attribute";
    EXPECT_EQ(xml.indexOf("cellTrack=\""), -1) << "no legacy cellTrack attribute";

    firstFile.deleteFile();
    secondFile.deleteFile();
}

// G5 (as scoped by decision 3): lane `paramID` (2000 + sendIndex) and LFO
// `targetParamID` keep their positional send encoding — the migration must leave
// them UNTOUCHED.
TEST(DurableRefMigration, LegacySendTargetPidsSurviveTheMigration)
{
    AudioEngine engine;
    engine.initialize();
    const Scene s = buildScene(engine);

    auto srcList = engine.getProjectModel().getTrackListTree();
    ASSERT_EQ(laneParamID(srcList, s.mainIdx, "SendLevel"), 2001);
    ASSERT_EQ(lfoTargetParamID(srcList, s.mainIdx), 2001);

    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto legacyFile = dir.getNonexistentChildFile("hdaw_b3_pids", ".hdaw", false);
    ASSERT_TRUE(HDAW::ProjectSerializer::save(engine.getProjectModel(), legacyFile));

    auto legacyTree = readTree(legacyFile);
    ASSERT_TRUE(legacyTree.isValid());
    denormalizeToLegacy(legacyTree);
    ASSERT_TRUE(writeTree(legacyTree, legacyFile));

    AudioEngine engine2;
    engine2.initialize();
    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine2.getProjectModel(), legacyFile));

    auto tl2 = engine2.getProjectModel().getTrackListTree();
    const int mainIdx = indexOfName(tl2, "Main");
    ASSERT_GE(mainIdx, 0);
    EXPECT_EQ(laneParamID(tl2, mainIdx, "SendLevel"), 2001)
        << "a lane's send-level pid must survive the migration verbatim";
    EXPECT_EQ(lfoTargetParamID(tl2, mainIdx), 2001)
        << "an LFO's send-level target pid must survive the migration verbatim";

    // Sanity: the migration DID run on this file (the folder link is an id now).
    const int folderIdx = indexOfName(tl2, "Folder");
    ASSERT_GE(folderIdx, 0);
    EXPECT_FALSE(tl2.getChild(folderIdx).hasProperty(IDs::childIds));
    EXPECT_EQ(childNamesOf(tl2, tl2.getChild(folderIdx)),
              (std::vector<std::string>{ "ChildA", "ChildB" }));

    legacyFile.deleteFile();
}

// Idempotence: loading a file the migration already converted is a no-op — the
// tree comes back identical (same folder links, same cell targets).
TEST(DurableRefMigration, LoadingAnAlreadyMigratedFileIsIdempotent)
{
    AudioEngine engine;
    engine.initialize();
    buildScene(engine);

    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto savedFile = dir.getNonexistentChildFile("hdaw_b3_idem", ".hdaw", false);
    auto resavedFile = dir.getNonexistentChildFile("hdaw_b3_idem2", ".hdaw", false);
    ASSERT_TRUE(HDAW::ProjectSerializer::save(engine.getProjectModel(), savedFile));

    // First load (already new vocabulary -> migration is a no-op), then save.
    AudioEngine engine2;
    engine2.initialize();
    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine2.getProjectModel(), savedFile));
    ASSERT_TRUE(HDAW::ProjectSerializer::save(engine2.getProjectModel(), resavedFile));

    // Second load of the migrated file.
    AudioEngine engine3;
    engine3.initialize();
    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine3.getProjectModel(), resavedFile));

    auto tl2 = engine2.getProjectModel().getTrackListTree();
    auto tl3 = engine3.getProjectModel().getTrackListTree();
    ASSERT_EQ(tl2.getNumChildren(), tl3.getNumChildren());
    for (int t = 0; t < tl2.getNumChildren(); ++t)
    {
        EXPECT_EQ(tl2.getChild(t).getProperty(IDs::name).toString(),
                  tl3.getChild(t).getProperty(IDs::name).toString());
        EXPECT_EQ(tl2.getChild(t).getProperty(IDs::childTrackIDs, "").toString(),
                  tl3.getChild(t).getProperty(IDs::childTrackIDs, "").toString());
        EXPECT_EQ(static_cast<int>(tl2.getChild(t).getProperty(IDs::parentTrackID, -999)),
                  static_cast<int>(tl3.getChild(t).getProperty(IDs::parentTrackID, -999)));
    }
    auto c2 = cellsOf(engine2.getProjectModel().getTree());
    auto c3 = cellsOf(engine3.getProjectModel().getTree());
    ASSERT_EQ(c2.getNumChildren(), c3.getNumChildren());
    for (int c = 0; c < c2.getNumChildren(); ++c)
        EXPECT_EQ(static_cast<int>(c2.getChild(c).getProperty(IDs::cellTrackID, -999)),
                  static_cast<int>(c3.getChild(c).getProperty(IDs::cellTrackID, -999)));

    savedFile.deleteFile();
    resavedFile.deleteFile();
}
