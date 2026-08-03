// Project file metadata stamping: createdWithApp / savedWithApp /
// formatVersion / createdAt / lastSavedAt.
//
// These five properties live on the root ValueTree and serialize via the
// existing toXmlString() path (no XML envelope). They are surfaced through:
//   - the ReadModel ProjectSnapshot struct (engine in-process API)
//   - the toJson(ProjectSnapshot) overload (the wire shape read.snapshot emits)
//   - the project_info MCP tool
//
// Contract under test (the "provenance rule"):
//   - createDefaultProject stamps createdWithApp / formatVersion=1 / createdAt.
//   - save() refreshes savedWithApp / lastSavedAt and backfills any missing
//     provenance property, but NEVER overwrites existing createdWithApp /
//     createdAt.
//   - load() is READ-ONLY for provenance: it does not stamp the current app
//     version or formatVersion onto the tree. Legacy metadata-less files load
//     cleanly and read back with defaults.
//
// Modeled on rpc_surface_test.cpp (SaveAndLoadRoundTrip) and ProjectBackup.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "engine/ProjectSerializer.h"
#include "engine/ProjectBackup.h"
#include "model/ProjectModel.h"
#include "common/Version.h"
#include "frontend/FrontendRpc.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// A scoped temp .hdaw file that cleans itself up on destruction.
class TempProjectFile {
public:
    explicit TempProjectFile(const juce::String& name = "hdaw_metadata_test.hdaw")
    {
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        file_ = dir.getChildFile(name);
        file_.deleteFile();
    }
    ~TempProjectFile() { file_.deleteFile(); }
    TempProjectFile(const TempProjectFile&) = delete;
    TempProjectFile& operator=(const TempProjectFile&) = delete;
    juce::File file() const { return file_; }
    juce::String path() const { return file_.getFullPathName(); }

private:
    juce::File file_;
};

} // namespace

// ============================================================================
// NEW PROJECT
// ============================================================================

TEST(ProjectMetadata, NewProjectStampsMetadata)
{
    AudioEngine engine;
    engine.initialize();

    const auto& tree = engine.getProjectModel().getTree();

    // createdWithApp / formatVersion / createdAt are stamped by createDefaultProject.
    EXPECT_EQ(tree.getProperty(IDs::createdWithApp).toString(), juce::String(HDAW_VERSION));
    EXPECT_EQ(static_cast<int>(tree.getProperty(IDs::formatVersion, -1)), 1);
    EXPECT_FALSE(tree.getProperty(IDs::createdAt).toString().isEmpty());
}

// ============================================================================
// SAVE: WRITES METADATA AND PRESERVES PROVENANCE
// ============================================================================

TEST(ProjectMetadata, SaveWritesAndPreserves)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    TempProjectFile tmp("hdaw_metadata_save.hdaw");
    ASSERT_TRUE(cmds.saveProject(tmp.path().toStdString()));

    // Read the raw XML back and confirm the metadata attributes are present.
    const auto xml = tmp.file().loadFileAsString();
    EXPECT_TRUE(xml.contains("savedWithApp")) << "savedWithApp missing from XML";
    EXPECT_TRUE(xml.contains("lastSavedAt"))   << "lastSavedAt missing from XML";
    EXPECT_TRUE(xml.contains("createdWithApp")) << "createdWithApp missing from XML";

    // Parse the XML back into a tree and assert the created-with value is the
    // current app version (a freshly-created project's provenance == self).
    auto reparsed = juce::ValueTree::fromXml(xml);
    ASSERT_TRUE(reparsed.isValid());
    EXPECT_EQ(reparsed.getProperty(IDs::createdWithApp).toString(), juce::String(HDAW_VERSION));
    EXPECT_EQ(reparsed.getProperty(IDs::savedWithApp).toString(),   juce::String(HDAW_VERSION));

    // Now mutate createdWithApp to a fake older value, save again, and confirm
    // the save path PRESERVES it (provenance rule: save never overwrites an
    // existing createdWithApp), while savedWithApp tracks the current version.
    engine.getProjectModel().getTree().setProperty(IDs::createdWithApp, "0.1.0", nullptr);

    ASSERT_TRUE(cmds.saveProject(tmp.path().toStdString()));
    auto xml2 = tmp.file().loadFileAsString();
    auto reparsed2 = juce::ValueTree::fromXml(xml2);
    ASSERT_TRUE(reparsed2.isValid());
    EXPECT_EQ(reparsed2.getProperty(IDs::createdWithApp).toString(), juce::String("0.1.0"))
        << "save() must not overwrite existing createdWithApp (provenance rule)";
    EXPECT_EQ(reparsed2.getProperty(IDs::savedWithApp).toString(), juce::String(HDAW_VERSION))
        << "save() must refresh savedWithApp to the current app version";
}

// ============================================================================
// ROUND TRIP: LOAD PRESERVES ORIGINAL PROVENANCE
// ============================================================================

TEST(ProjectMetadata, RoundTripPreservesProvenance)
{
    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    // Capture original provenance from the freshly-created project.
    const auto originalCreatedWith = engine.getProjectModel().getTree()
        .getProperty(IDs::createdWithApp).toString();
    const auto originalCreatedAt   = engine.getProjectModel().getTree()
        .getProperty(IDs::createdAt).toString();
    ASSERT_EQ(originalCreatedWith, juce::String(HDAW_VERSION));
    ASSERT_FALSE(originalCreatedAt.isEmpty());

    TempProjectFile tmp("hdaw_metadata_roundtrip.hdaw");
    ASSERT_TRUE(cmds.saveProject(tmp.path().toStdString()));

    // A newProject() mints fresh metadata (new createdAt), so the in-memory
    // tree no longer matches the saved file's provenance.
    cmds.newProject();
    EXPECT_NE(engine.getProjectModel().getTree().getProperty(IDs::createdAt).toString(),
              originalCreatedAt);

    // Loading the saved file must restore the ORIGINAL provenance — load does
    // not restamp.
    ASSERT_TRUE(cmds.loadProject(tmp.path().toStdString()));
    EXPECT_EQ(engine.getProjectModel().getTree().getProperty(IDs::createdWithApp).toString(),
              originalCreatedWith);
    EXPECT_EQ(engine.getProjectModel().getTree().getProperty(IDs::createdAt).toString(),
              originalCreatedAt);
}

// ============================================================================
// LEGACY FILE: METADATA-LESS FILES LOAD CLEANLY
// ============================================================================

TEST(ProjectMetadata, LegacyFileLoadsClean)
{
    AudioEngine engine;
    engine.initialize();

    // Synthesize a legacy metadata-less project file: PROJECT root with a
    // TRACK_LIST child and a TRANSPORT child, but none of the five metadata
    // properties. This mirrors pre-0.15.0 .hdaw files.
    const juce::String legacyXml =
        "<PROJECT name=\"Legacy\" tempo=\"120.0\">"
        "  <TRACK_LIST/>"
        "  <TRANSPORT position=\"0.0\" isPlaying=\"0\" loopStart=\"0.0\" "
        "             loopEnd=\"8.0\" isLooping=\"0\" timeSigNumerator=\"4\" "
        "             timeSigDenominator=\"4\"/>"
        "</PROJECT>";

    TempProjectFile tmp("hdaw_metadata_legacy.hdaw");
    ASSERT_TRUE(tmp.file().replaceWithText(legacyXml));

    // Call the serializer directly to isolate the load/migration path from
    // the full command-layer routing-graph rebuild.
    bool ok = HDAW::ProjectSerializer::load(engine.getProjectModel(), tmp.file());
    ASSERT_TRUE(ok);

    const auto& tree = engine.getProjectModel().getTree();

    // Legacy defaults surface via getProperty(id, default). No crash, no stamp.
    EXPECT_EQ(tree.getProperty(IDs::createdWithApp, "unknown").toString(), juce::String("unknown"));
    EXPECT_EQ(tree.getProperty(IDs::savedWithApp,   "unknown").toString(), juce::String("unknown"));
    EXPECT_EQ(static_cast<int>(tree.getProperty(IDs::formatVersion, 0)), 0);
}

// ============================================================================
// MIGRATION HOOK: LOAD IS READ-ONLY FOR PROVENANCE
// ============================================================================

TEST(ProjectMetadata, MigrationHookFiresOnLegacyFormatVersion)
{
    AudioEngine engine;
    engine.initialize();

    // Legacy file: no formatVersion property at all.
    const juce::String legacyXml =
        "<PROJECT name=\"LegacyFmt\" tempo=\"120.0\">"
        "  <TRACK_LIST/>"
        "</PROJECT>";

    TempProjectFile tmp("hdaw_metadata_legacy_fmt.hdaw");
    ASSERT_TRUE(tmp.file().replaceWithText(legacyXml));

    ASSERT_TRUE(HDAW::ProjectSerializer::load(engine.getProjectModel(), tmp.file()));

    const auto& tree = engine.getProjectModel().getTree();

    // Lock in the load-is-read-only contract: formatVersion is NOT auto-stamped
    // onto the tree during load. It reads back as 0 (the legacy default) and
    // the property is genuinely absent (hasProperty == false). The migrate hook
    // logs but does not mutate provenance.
    EXPECT_FALSE(tree.hasProperty(IDs::formatVersion))
        << "load() must not stamp formatVersion (provenance rule)";
    EXPECT_EQ(static_cast<int>(tree.getProperty(IDs::formatVersion, 0)), 0);

    // createdWithApp is also absent — load did not backfill current app version.
    EXPECT_FALSE(tree.hasProperty(IDs::createdWithApp))
        << "load() must not stamp createdWithApp (provenance rule)";
}

// ============================================================================
// READ PATH: SNAPSHOT + toJson EXPOSE METADATA
// ============================================================================

TEST(ProjectMetadata, SnapshotExposesMetadata)
{
    AudioEngine engine;
    engine.initialize();

    auto snap = engine.getReadModel().snapshot();

    // The ReadModel snapshot struct carries the creation-time metadata.
    // createdWithApp / formatVersion are stamped by createDefaultProject.
    EXPECT_EQ(snap.createdWithApp, std::string(HDAW_VERSION));
    EXPECT_EQ(snap.formatVersion, 1);
    // savedWithApp is NOT stamped until the first save() — a fresh project
    // reads back the default "unknown" (the save-time fields are write-on-save).
    EXPECT_EQ(snap.savedWithApp, std::string("unknown"));

    // The toJson(ProjectSnapshot) overload — the wire shape emitted by the
    // read.snapshot RPC (FrontendRouter.cpp:584) — must include the three keys.
    // This is the Gate 2 read-side check: the property has a consumer.
    const QJsonObject json = frontend::toJson(snap);
    EXPECT_EQ(json.value("createdWithApp").toString().toStdString(), std::string(HDAW_VERSION));
    EXPECT_EQ(json.value("savedWithApp").toString().toStdString(),   std::string("unknown"));
    EXPECT_EQ(json.value("formatVersion").toInt(), 1);

    // After a save, savedWithApp tracks the current app version.
    auto& cmds = engine.getProjectCommands();
    TempProjectFile tmp("hdaw_metadata_snapshot.hdaw");
    ASSERT_TRUE(cmds.saveProject(tmp.path().toStdString()));
    auto snap2 = engine.getReadModel().snapshot();
    EXPECT_EQ(snap2.savedWithApp, std::string(HDAW_VERSION));
    EXPECT_EQ(snap2.createdWithApp, std::string(HDAW_VERSION)); // provenance unchanged

    // Full RPC-level (WebSocket) round-trip of read.snapshot is covered
    // indirectly here: toJson is the exact function FrontendRouter calls on
    // the result of snapshot() before sending it over the wire. A live
    // WebSocket assertion lives in the frontend_server_test suite which
    // already exercises read.snapshot end-to-end (SnapshotRoundTrip).
}
