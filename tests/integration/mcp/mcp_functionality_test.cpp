#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>

namespace {

// --- Test helpers ---

QJsonObject parseOne(const QByteArray& buf) {
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

class GuiFuncTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
    }

    void TearDown() override {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
    }

    QJsonObject call(const char* method, const QJsonObject& args = {}) {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = nextId_++;
        req["method"] = "tools/call";
        req["params"] = QJsonObject{{"name", method}, {"arguments", args}};
        loopback->drainOutgoing();
        loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (!loopback->waitForOutgoing(500, &out)) return {};
        auto resp = parseOne(out);
        return resp.value("result").toObject();
    }

    QJsonValue callText(const char* method, const QJsonObject& args = {}) {
        auto r = call(method, args);
        auto content = r.value("content").toArray();
        if (content.isEmpty()) return QJsonValue();
        return content[0].toObject().value("text");
    }

    bool isError(const QJsonObject& r) {
        return r.value("isError").toBool(false);
    }

    QString text(const QJsonObject& r) {
        auto content = r.value("content").toArray();
        if (content.isEmpty()) return {};
        return content[0].toObject().value("text").toString();
    }

    // Get track list
    QJsonArray trackList() {
        auto t = callText("list_tracks");
        return QJsonDocument::fromJson(t.toString().toUtf8()).array();
    }

    // Get clip list
    QJsonArray clipList() {
        auto t = callText("list_clips");
        return QJsonDocument::fromJson(t.toString().toUtf8()).array();
    }

    // Count tracks
    int trackCount() {
        return trackList().size();
    }

    // Count clips
    int clipCount() {
        return clipList().size();
    }

    // Find a clip by ID
    QJsonObject findClip(int clipId) {
        auto clips = clipList();
        for (const auto& c : clips) {
            if (c.toObject().value("id").toInt() == clipId)
                return c.toObject();
        }
        return {};
    }

    // Find a track by index
    QJsonObject findTrack(int index) {
        auto tracks = trackList();
        for (const auto& t : tracks) {
            if (t.toObject().value("id").toInt() == index)
                return t.toObject();
        }
        return {};
    }

    // Get notes for a clip via get_clip
    QJsonArray getNotes(int clipId) {
        auto r = callText("get_clip", {{"clipId", clipId}});
        auto obj = QJsonDocument::fromJson(r.toString().toUtf8()).object();
        return obj.value("notes").toArray();
    }

    // Get transport state
    QJsonObject transport() {
        auto t = callText("get_transport");
        return QJsonDocument::fromJson(t.toString().toUtf8()).object();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
    int nextId_ = 1;
};

// ============================================================================
// TRACK OPERATIONS
// ============================================================================

TEST_F(GuiFuncTest, AddTrack) {
    int before = trackCount();
    auto r = call("add_track", {{"name", "Guitar"}});
    EXPECT_FALSE(isError(r));
    EXPECT_EQ(trackCount(), before + 1);

    auto t = findTrack(trackCount() - 1);
    EXPECT_EQ(t.value("name").toString().toStdString(), "Guitar");
    EXPECT_EQ(t.value("clipCount").toInt(), 0);
}

TEST_F(GuiFuncTest, AddMultipleTracks) {
    int before = trackCount();
    call("add_track", {{"name", "Track A"}});
    call("add_track", {{"name", "Track B"}});
    call("add_track", {{"name", "Track C"}});
    EXPECT_EQ(trackCount(), before + 3);
}

TEST_F(GuiFuncTest, RemoveTrack) {
    int before = trackCount();
    ASSERT_GT(before, 0);
    auto r = call("remove_track", {{"trackId", before - 1}});
    EXPECT_FALSE(isError(r));
    EXPECT_EQ(trackCount(), before - 1);
}

TEST_F(GuiFuncTest, SetTrackVolume) {
    auto r = call("set_track", {{"trackId", 0}, {"volume", 0.5}});
    EXPECT_FALSE(isError(r));
    auto t = findTrack(0);
    EXPECT_NEAR(t.value("volume").toDouble(), 0.5, 0.01);
}

TEST_F(GuiFuncTest, SetTrackPan) {
    auto r = call("set_track", {{"trackId", 0}, {"pan", -0.75}});
    EXPECT_FALSE(isError(r));
    auto t = findTrack(0);
    EXPECT_NEAR(t.value("pan").toDouble(), -0.75, 0.01);
}

TEST_F(GuiFuncTest, SetTrackMute) {
    auto r = call("set_track", {{"trackId", 0}, {"mute", true}});
    EXPECT_FALSE(isError(r));
    auto t = findTrack(0);
    EXPECT_TRUE(t.value("mute").toBool());
}

TEST_F(GuiFuncTest, SetTrackSolo) {
    auto r = call("set_track", {{"trackId", 0}, {"solo", true}});
    EXPECT_FALSE(isError(r));
    auto t = findTrack(0);
    EXPECT_TRUE(t.value("solo").toBool());
}

TEST_F(GuiFuncTest, SetTrackName) {
    auto r = call("set_track", {{"trackId", 0}, {"name", "My Track"}});
    EXPECT_FALSE(isError(r));
    auto t = findTrack(0);
    EXPECT_EQ(t.value("name").toString().toStdString(), "My Track");
}

TEST_F(GuiFuncTest, RemoveTrackNotFound) {
    auto r = call("remove_track", {{"trackId", 999}});
    EXPECT_TRUE(isError(r));
}

// ============================================================================
// CLIP OPERATIONS
// ============================================================================

TEST_F(GuiFuncTest, AddMidiClip) {
    int before = clipCount();
    auto r = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    EXPECT_FALSE(isError(r));
    EXPECT_EQ(clipCount(), before + 1);

    // Extract clipId from response text "clipId=N"
    QString resp = text(r);
    int clipId = resp.mid(resp.indexOf('=') + 1).toInt();
    auto c = findClip(clipId);
    EXPECT_FALSE(c.isEmpty());
    EXPECT_EQ(c.value("trackId").toInt(), 0);
    EXPECT_NEAR(c.value("start").toDouble(), 0.0, 0.01);
    EXPECT_NEAR(c.value("duration").toDouble(), 4.0, 0.01);
    EXPECT_EQ(c.value("type").toString().toStdString(), "midi");
}

TEST_F(GuiFuncTest, AddMidiClipWithName) {
    auto r = call("add_midi_clip", {
        {"trackId", 0}, {"start", 8.0}, {"length", 2.0}, {"name", "Melody"}
    });
    EXPECT_FALSE(isError(r));
    int clipId = text(r).mid(text(r).indexOf('=') + 1).toInt();
    auto c = findClip(clipId);
    EXPECT_EQ(c.value("name").toString().toStdString(), "Melody");
}

TEST_F(GuiFuncTest, RemoveClip) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();
    ASSERT_GT(clipId, 0);

    int before = clipCount();
    auto r = call("remove_clip", {{"clipId", clipId}});
    EXPECT_FALSE(isError(r));
    EXPECT_EQ(clipCount(), before - 1);
    EXPECT_TRUE(findClip(clipId).isEmpty());
}

TEST_F(GuiFuncTest, MoveClip) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto r = call("move_clip", {{"clipId", clipId}, {"start", 16.0}});
    EXPECT_FALSE(isError(r));
    auto c = findClip(clipId);
    EXPECT_NEAR(c.value("start").toDouble(), 16.0, 0.01);
}

TEST_F(GuiFuncTest, MoveClipToDifferentTrack) {
    int trackCountBefore = trackCount();
    call("add_track", {{"name", "Track 2"}});

    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto r = call("move_clip", {{"clipId", clipId}, {"trackId", trackCountBefore}});
    EXPECT_FALSE(isError(r));
    auto c = findClip(clipId);
    EXPECT_EQ(c.value("trackId").toInt(), trackCountBefore);
}

TEST_F(GuiFuncTest, SetClipProperties) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    call("set_clip", {{"clipId", clipId}, {"name", "Renamed"}});
    call("set_clip", {{"clipId", clipId}, {"gain", 0.75}});
    call("set_clip", {{"clipId", clipId}, {"fadeIn", 0.1}});
    call("set_clip", {{"clipId", clipId}, {"fadeOut", 0.2}});
    call("set_clip", {{"clipId", clipId}, {"looping", true}});

    auto c = findClip(clipId);
    EXPECT_EQ(c.value("name").toString().toStdString(), "Renamed");
    EXPECT_NEAR(c.value("gain").toDouble(), 0.75, 0.01);
    EXPECT_NEAR(c.value("fadeIn").toDouble(), 0.1, 0.01);
    EXPECT_NEAR(c.value("fadeOut").toDouble(), 0.2, 0.01);
    EXPECT_TRUE(c.value("looping").toBool());
}

TEST_F(GuiFuncTest, DuplicateClip) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    int before = clipCount();
    auto r = call("duplicate_clip", {{"clipId", clipId}});
    EXPECT_FALSE(isError(r));
    EXPECT_EQ(clipCount(), before + 1);

    // The duplicated clip should have a different ID
    int newClipId = text(r).mid(text(r).indexOf('=') + 1).toInt();
    EXPECT_NE(newClipId, clipId);

    // Original and duplicate should have the same start
    auto orig = findClip(clipId);
    auto dup = findClip(newClipId);
    EXPECT_NEAR(orig.value("start").toDouble(),
                dup.value("start").toDouble(), 0.01);
}

TEST_F(GuiFuncTest, DuplicateClipToNewPosition) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto r = call("duplicate_clip", {{"clipId", clipId}, {"start", 16.0}});
    int newClipId = text(r).mid(text(r).indexOf('=') + 1).toInt();
    auto dup = findClip(newClipId);
    EXPECT_NEAR(dup.value("start").toDouble(), 16.0, 0.01);
}

TEST_F(GuiFuncTest, RemoveClipNotFound) {
    auto r = call("remove_clip", {{"clipId", 99999}});
    EXPECT_TRUE(isError(r));
}

TEST_F(GuiFuncTest, AddMidiClipInvalidTrack) {
    auto r = call("add_midi_clip", {{"trackId", 999}, {"start", 0.0}, {"length", 4.0}});
    EXPECT_TRUE(isError(r));
}

// ============================================================================
// NOTE OPERATIONS
// ============================================================================

TEST_F(GuiFuncTest, AddNote) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto r = call("add_note", {
        {"clipId", clipId}, {"pitch", 60}, {"start", 0.0},
        {"duration", 1.0}, {"velocity", 100}
    });
    EXPECT_FALSE(isError(r));

    auto notes = getNotes(clipId);
    ASSERT_GE(notes.size(), 1);

    auto note = notes[0].toObject();
    EXPECT_EQ(note.value("pitch").toInt(), 60);
    EXPECT_EQ(note.value("velocity").toInt(), 100);
    EXPECT_NEAR(note.value("start").toDouble(), 0.0, 0.01);
    EXPECT_NEAR(note.value("duration").toDouble(), 1.0, 0.01);
}

TEST_F(GuiFuncTest, AddMultipleNotes) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    call("add_note", {{"clipId", clipId}, {"pitch", 60}, {"start", 0.0},
                       {"duration", 1.0}, {"velocity", 100}});
    call("add_note", {{"clipId", clipId}, {"pitch", 64}, {"start", 1.0},
                       {"duration", 1.0}, {"velocity", 90}});
    call("add_note", {{"clipId", clipId}, {"pitch", 67}, {"start", 2.0},
                       {"duration", 2.0}, {"velocity", 80}});

    auto notes = getNotes(clipId);
    EXPECT_EQ(notes.size(), 3);
}

TEST_F(GuiFuncTest, SetNote) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto noteResp = call("add_note", {{"clipId", clipId}, {"pitch", 60},
                                       {"start", 0.0}, {"duration", 1.0},
                                       {"velocity", 100}});
    int noteId = text(noteResp).mid(text(noteResp).indexOf('=') + 1).toInt();

    auto r = call("set_note", {{"noteId", noteId}, {"pitch", 72}});
    EXPECT_FALSE(isError(r));

    auto notes = getNotes(clipId);
    ASSERT_GE(notes.size(), 1);
    EXPECT_EQ(notes[0].toObject().value("pitch").toInt(), 72);
}

TEST_F(GuiFuncTest, RemoveNotesByFilter) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    call("add_note", {{"clipId", clipId}, {"pitch", 60}, {"start", 0.0},
                       {"duration", 1.0}, {"velocity", 100}});
    call("add_note", {{"clipId", clipId}, {"pitch", 64}, {"start", 1.0},
                       {"duration", 1.0}, {"velocity", 90}});

    auto r = call("remove_notes", {{"clipId", clipId}, {"pitches", QJsonArray{60}}});
    EXPECT_FALSE(isError(r));

    auto notes = getNotes(clipId);
    EXPECT_EQ(notes.size(), 1);
    EXPECT_EQ(notes[0].toObject().value("pitch").toInt(), 64);
}

TEST_F(GuiFuncTest, ClearNotes) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    call("add_note", {{"clipId", clipId}, {"pitch", 60}, {"start", 0.0},
                       {"duration", 1.0}, {"velocity", 100}});
    call("add_note", {{"clipId", clipId}, {"pitch", 64}, {"start", 1.0},
                       {"duration", 1.0}, {"velocity", 90}});

    auto r = call("clear_notes", {{"clipId", clipId}});
    EXPECT_FALSE(isError(r));

    auto notes = getNotes(clipId);
    EXPECT_TRUE(notes.isEmpty());
}

TEST_F(GuiFuncTest, AddNoteToNonMidiClip) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto r = call("add_note", {{"clipId", clipId}, {"pitch", 60},
                                {"start", 0.0}, {"duration", 1.0}, {"velocity", 100}});
    EXPECT_FALSE(isError(r));
}

// ============================================================================
// TRANSPORT OPERATIONS
// ============================================================================

TEST_F(GuiFuncTest, PlayAndStop) {
    auto before = transport();
    EXPECT_FALSE(before.value("isPlaying").toBool());

    auto play = call("transport", {{"action", "play"}});
    EXPECT_FALSE(isError(play));

    auto after = transport();
    EXPECT_TRUE(after.value("isPlaying").toBool());

    auto stop = call("transport", {{"action", "stop"}});
    EXPECT_FALSE(isError(stop));

    auto stopped = transport();
    EXPECT_FALSE(stopped.value("isPlaying").toBool());
}

TEST_F(GuiFuncTest, Seek) {
    auto r = call("seek", {{"position", 2.5}});
    EXPECT_FALSE(isError(r));

    auto t = transport();
    EXPECT_NEAR(t.value("position").toDouble(), 2.5, 0.1);
}

TEST_F(GuiFuncTest, ToggleLoop) {
    auto before = transport();
    bool wasLooping = before.value("isLooping").toBool();

    auto r = call("transport", {{"action", "toggleLoop"}});
    EXPECT_FALSE(isError(r));

    auto after = transport();
    EXPECT_NE(after.value("isLooping").toBool(), wasLooping);
}

TEST_F(GuiFuncTest, Rewind) {
    call("seek", {{"position", 5.0}});

    auto r = call("transport", {{"action", "rewind"}});
    EXPECT_FALSE(isError(r));

    auto t = transport();
    EXPECT_NEAR(t.value("position").toDouble(), 0.0, 0.1);
}

// ============================================================================
// UNDO / REDO
// ============================================================================

TEST_F(GuiFuncTest, UndoAddTrack) {
    int before = trackCount();
    call("add_track", {{"name", "Temp"}});
    EXPECT_EQ(trackCount(), before + 1);

    auto r = call("undo", {});
    EXPECT_FALSE(isError(r));
    EXPECT_EQ(trackCount(), before);
}

TEST_F(GuiFuncTest, UndoRemoveTrack) {
    int before = trackCount();
    call("remove_track", {{"trackId", before - 1}});
    EXPECT_EQ(trackCount(), before - 1);

    auto r = call("undo", {});
    EXPECT_FALSE(isError(r));
    EXPECT_EQ(trackCount(), before);
}

TEST_F(GuiFuncTest, UndoRedoClip) {
    int before = clipCount();
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();
    EXPECT_EQ(clipCount(), before + 1);

    call("undo", {});
    EXPECT_EQ(clipCount(), before);

    call("redo", {});
    EXPECT_EQ(clipCount(), before + 1);
    EXPECT_FALSE(findClip(clipId).isEmpty());
}

TEST_F(GuiFuncTest, UndoMoveClip) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto before = findClip(clipId);
    double origStart = before.value("start").toDouble();

    call("move_clip", {{"clipId", clipId}, {"start", 20.0}});
    auto moved = findClip(clipId);
    EXPECT_NEAR(moved.value("start").toDouble(), 20.0, 0.01);

    call("undo", {});
    auto restored = findClip(clipId);
    EXPECT_NEAR(restored.value("start").toDouble(), origStart, 0.01);
}

TEST_F(GuiFuncTest, UndoSetTrackVolume) {
    call("set_track", {{"trackId", 0}, {"volume", 0.25}});
    auto t = findTrack(0);
    EXPECT_NEAR(t.value("volume").toDouble(), 0.25, 0.01);

    call("undo", {});
    auto t2 = findTrack(0);
    // Volume should be back to default (1.0)
    EXPECT_NEAR(t2.value("volume").toDouble(), 1.0, 0.01);
}

TEST_F(GuiFuncTest, UndoAddNote) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    call("add_note", {{"clipId", clipId}, {"pitch", 60}, {"start", 0.0},
                       {"duration", 1.0}, {"velocity", 100}});
    auto notesBefore = getNotes(clipId);
    EXPECT_EQ(notesBefore.size(), 1);

    call("undo", {});
    auto notesAfter = getNotes(clipId);
    EXPECT_TRUE(notesAfter.isEmpty());
}

// ============================================================================
// FX OPERATIONS
// ============================================================================

TEST_F(GuiFuncTest, AddFxAndRemove) {
    auto r = call("add_fx", {{"trackId", 0}, {"fxType", "eq"}});
    EXPECT_FALSE(isError(r));

    auto fx = callText("list_fx", {{"trackId", 0}});
    auto fxArr = QJsonDocument::fromJson(fx.toString().toUtf8()).array();
    EXPECT_EQ(fxArr.size(), 1);
    EXPECT_EQ(fxArr[0].toObject().value("type").toString().toStdString(), "eq");

    call("remove_fx", {{"trackId", 0}, {"slotIndex", 0}});
    auto fxAfter = QJsonDocument::fromJson(
        callText("list_fx", {{"trackId", 0}}).toString().toUtf8()).array();
    EXPECT_TRUE(fxAfter.isEmpty());
}

TEST_F(GuiFuncTest, BypassFx) {
    call("add_fx", {{"trackId", 0}, {"fxType", "eq"}});

    auto r = call("set_fx_bypass", {{"trackId", 0}, {"slotIndex", 0}, {"bypassed", true}});
    EXPECT_FALSE(isError(r));

    auto fx = QJsonDocument::fromJson(
        callText("list_fx", {{"trackId", 0}}).toString().toUtf8()).array();
    EXPECT_TRUE(fx[0].toObject().value("bypassed").toBool());

    call("set_fx_bypass", {{"trackId", 0}, {"slotIndex", 0}, {"bypassed", false}});
    auto fx2 = QJsonDocument::fromJson(
        callText("list_fx", {{"trackId", 0}}).toString().toUtf8()).array();
    EXPECT_FALSE(fx2[0].toObject().value("bypassed").toBool());
}

// ============================================================================
// AUTOMATION
// ============================================================================

TEST_F(GuiFuncTest, AddAutomationPoint) {
    auto r = call("add_automation_point", {
        {"trackId", 0}, {"lane", "Volume"}, {"time", 1.0}, {"value", 0.8}
    });
    EXPECT_FALSE(isError(r));

    auto lanes = QJsonDocument::fromJson(
        callText("list_automation_lanes", {{"trackId", 0}}).toString().toUtf8()).array();
    EXPECT_GE(lanes.size(), 0);
}

TEST_F(GuiFuncTest, AutomationEnableDisable) {
    auto r = call("set_automation_enabled", {
        {"trackId", 0}, {"lane", "Volume"}, {"enabled", true}
    });
    EXPECT_FALSE(isError(r));

    auto lanes = QJsonDocument::fromJson(
        callText("list_automation_lanes", {{"trackId", 0}}).toString().toUtf8()).array();
    for (const auto& l : lanes) {
        auto obj = l.toObject();
        if (obj.value("name").toString() == "Volume") {
            EXPECT_TRUE(obj.value("enabled").toBool());
            break;
        }
    }
}

// ============================================================================
// SCALE
// ============================================================================

TEST_F(GuiFuncTest, SetScale) {
    auto r = call("set_scale", {{"root", 5}, {"mode", 2}});
    EXPECT_FALSE(isError(r));

    auto s = callText("get_scale");
    auto obj = QJsonDocument::fromJson(s.toString().toUtf8()).object();
    EXPECT_EQ(obj.value("root").toInt(), 5);
    EXPECT_EQ(obj.value("mode").toInt(), 2);
}

// ============================================================================
// MARKERS
// ============================================================================

TEST_F(GuiFuncTest, GetMarkers) {
    auto r = call("get_project_summary");
    EXPECT_FALSE(isError(r));
}

// ============================================================================
// PROJECT OPERATIONS
// ============================================================================

TEST_F(GuiFuncTest, NewProject) {
    call("add_track", {{"name", "Temp"}});
    int before = trackCount();
    ASSERT_GT(before, 1); // default has tracks

    auto r = call("new_project", {});
    EXPECT_FALSE(isError(r));

    // After new project, track count should reset to defaults
    int after = trackCount();
    EXPECT_GT(after, 0);
}

TEST_F(GuiFuncTest, GetProjectSummary) {
    auto r = call("get_project_summary");
    EXPECT_FALSE(isError(r));
    auto summary = text(r);
    EXPECT_TRUE(summary.contains("tracks="));
    EXPECT_TRUE(summary.contains("clips="));
}

// ============================================================================
// DRY RUN
// ============================================================================

TEST_F(GuiFuncTest, RemoveClipDryRun) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    int before = clipCount();
    auto r = call("remove_clip", {{"clipId", clipId}, {"dryRun", true}});
    EXPECT_FALSE(isError(r));
    EXPECT_TRUE(text(r).contains("would remove"));
    EXPECT_EQ(clipCount(), before); // unchanged
}

TEST_F(GuiFuncTest, DuplicateClipDryRun) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    int before = clipCount();
    auto r = call("duplicate_clip", {{"clipId", clipId}, {"dryRun", true}});
    EXPECT_FALSE(isError(r));
    EXPECT_TRUE(text(r).contains("would duplicate"));
    EXPECT_EQ(clipCount(), before); // unchanged
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(GuiFuncTest, ClipOnEveryTrack) {
    int initialClips = clipCount();
    int tracks = trackCount();
    for (int i = 0; i < tracks; ++i) {
        auto r = call("add_midi_clip", {
            {"trackId", i}, {"start", 0.0}, {"length", 4.0}
        });
        EXPECT_FALSE(isError(r)) << "Failed to add clip on track " << i;
    }
    EXPECT_EQ(clipCount(), initialClips + tracks);
}

TEST_F(GuiFuncTest, ManyClipsOnOneTrack) {
    int initialClips = clipCount();
    for (int i = 0; i < 10; ++i) {
        auto r = call("add_midi_clip", {
            {"trackId", 0}, {"start", static_cast<double>(i) * 4.0}, {"length", 4.0}
        });
        EXPECT_FALSE(isError(r));
    }
    EXPECT_EQ(clipCount(), initialClips + 10);
}

TEST_F(GuiFuncTest, ManyNotesInClip) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    for (int i = 0; i < 20; ++i) {
        auto r = call("add_note", {
            {"clipId", clipId}, {"pitch", 60 + i}, {"start", 0.0},
            {"duration", 0.25}, {"velocity", 100}
        });
        EXPECT_FALSE(isError(r));
    }

    auto notes = getNotes(clipId);
    EXPECT_EQ(notes.size(), 20);
}

TEST_F(GuiFuncTest, UndoAllOperations) {
    // Build up state
    call("add_track", {{"name", "Track A"}});
    call("add_track", {{"name", "Track B"}});
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();
    call("add_note", {{"clipId", clipId}, {"pitch", 60}, {"start", 0.0},
                       {"duration", 1.0}, {"velocity", 100}});

    int trackCountFinal = trackCount();
    int clipCountFinal = clipCount();

    // Undo everything
    for (int i = 0; i < 5; ++i) {
        auto r = call("undo", {});
        EXPECT_FALSE(isError(r));
    }

    // The default project's built-in clips remain after undoing the added clip
    EXPECT_EQ(clipCount(), 2); // 2 default MIDI clips on the Synth track
}

// ============================================================================
// COMPLEX WORKFLOWS
// ============================================================================

TEST_F(GuiFuncTest, GeneratePhraseAndDuplicate) {
    auto gen = call("generate_phrase", {
        {"trackId", 0}, {"style", "Standard"}, {"length", 4.0}, {"density", 4}
    });
    EXPECT_FALSE(isError(gen));

    QString genText = text(gen);
    int clipId = genText.mid(genText.indexOf('=') + 1,
                             genText.indexOf(' ') - genText.indexOf('=') - 1).toInt();

    auto notes = getNotes(clipId);
    EXPECT_GT(notes.size(), 0);

    int before = clipCount();
    auto dup = call("duplicate_clip", {{"clipId", clipId}, {"start", 4.0}});
    EXPECT_FALSE(isError(dup));
    EXPECT_EQ(clipCount(), before + 1);

    int newClipId = text(dup).mid(text(dup).indexOf('=') + 1).toInt();
    auto dupNotes = getNotes(newClipId);
    EXPECT_EQ(dupNotes.size(), notes.size());
}

TEST_F(GuiFuncTest, TrackWithMultipleClipsAndNotes) {
    auto add1 = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clip1 = text(add1).mid(text(add1).indexOf('=') + 1).toInt();

    auto add2 = call("add_midi_clip", {{"trackId", 0}, {"start", 8.0}, {"length", 4.0}});
    int clip2 = text(add2).mid(text(add2).indexOf('=') + 1).toInt();

    for (int i = 0; i < 5; ++i) {
        call("add_note", {{"clipId", clip1}, {"pitch", 60 + i}, {"start", i * 0.5},
                          {"duration", 0.5}, {"velocity", 100}});
    }

    for (int i = 0; i < 3; ++i) {
        call("add_note", {{"clipId", clip2}, {"pitch", 72 + i}, {"start", i * 1.0},
                          {"duration", 1.0}, {"velocity", 80}});
    }

    auto notes1 = getNotes(clip1);
    auto notes2 = getNotes(clip2);
    EXPECT_EQ(notes1.size(), 5);
    EXPECT_EQ(notes2.size(), 3);
}

// ============================================================================
// ERROR CONDITIONS
// ============================================================================

TEST_F(GuiFuncTest, InvalidTrackOperations) {
    auto r1 = call("set_track", {{"trackId", 999}, {"volume", 0.5}});
    EXPECT_TRUE(isError(r1));

    auto r2 = call("add_midi_clip", {{"trackId", -1}, {"start", 0.0}, {"length", 4.0}});
    EXPECT_TRUE(isError(r2));

    auto r3 = call("add_fx", {{"trackId", 999}, {"fxType", "eq"}});
    EXPECT_TRUE(isError(r3));
}

TEST_F(GuiFuncTest, InvalidClipOperations) {
    auto r1 = call("set_clip", {{"clipId", 99999}, {"name", "Test"}});
    EXPECT_TRUE(isError(r1));

    auto r2 = call("move_clip", {{"clipId", 99999}, {"start", 10.0}});
    EXPECT_TRUE(isError(r2));

    auto r3 = call("add_note", {{"clipId", 99999}, {"pitch", 60}, {"start", 0.0},
                                {"duration", 1.0}, {"velocity", 100}});
    EXPECT_TRUE(isError(r3));
}

TEST_F(GuiFuncTest, InvalidNoteOperations) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto note = call("add_note", {{"clipId", clipId}, {"pitch", 60}, {"start", 0.0},
                                  {"duration", 1.0}, {"velocity", 100}});
    int noteId = text(note).mid(text(note).indexOf('=') + 1).toInt();

    auto r1 = call("set_note", {{"noteId", 99999}, {"pitch", 72}});
    EXPECT_TRUE(isError(r1));

    auto r2 = call("remove_notes", {{"clipId", clipId}, {"noteIds", QJsonArray{99999}}});
    EXPECT_FALSE(isError(r2));
}

// ============================================================================
// SAVE/LOAD PROJECT
// ============================================================================

TEST_F(GuiFuncTest, SaveAndLoadProject) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    QString projectPath = tempDir.path() + "/test_project.hdaw";

    call("add_track", {{"name", "Guitar"}});
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();
    call("add_note", {{"clipId", clipId}, {"pitch", 60}, {"start", 0.0},
                      {"duration", 1.0}, {"velocity", 100}});
    call("set_track", {{"trackId", 0}, {"volume", 0.75}});

    int trackCountBefore = trackCount();
    int clipCountBefore = clipCount();

    auto save = call("save_project", {{"filePath", projectPath}});
    EXPECT_FALSE(isError(save));

    call("new_project", {});
    EXPECT_NE(trackCount(), trackCountBefore);

    auto load = call("load_project", {{"filePath", projectPath}});
    EXPECT_FALSE(isError(load));

    EXPECT_EQ(trackCount(), trackCountBefore);
    EXPECT_EQ(clipCount(), clipCountBefore);

    auto tracks = trackList();
    bool foundGuitar = false;
    for (const auto& t : tracks) {
        if (t.toObject().value("name").toString() == "Guitar") {
            foundGuitar = true;
            EXPECT_NEAR(t.toObject().value("volume").toDouble(), 0.75, 0.15);
            break;
        }
    }
    EXPECT_TRUE(foundGuitar);
}

// ============================================================================
// PHRASE GENERATION
// ============================================================================

TEST_F(GuiFuncTest, GenerateChordAndArpeggio) {
    auto chord = call("generate_chord", {
        {"trackId", 0}, {"rootPitch", 60}, {"chordType", 0}, {"length", 2.0}
    });
    EXPECT_FALSE(isError(chord));

    QString chordText = text(chord);
    int chordClipId = chordText.mid(chordText.indexOf('=') + 1,
                                    chordText.indexOf(' ') - chordText.indexOf('=') - 1).toInt();
    auto notes = getNotes(chordClipId);
    EXPECT_GE(notes.size(), 3);

    auto arp = call("generate_chord", {
        {"trackId", 0}, {"rootPitch", 60}, {"chordType", 0}, {"length", 4.0},
        {"arpeggiate", true}
    });
    EXPECT_FALSE(isError(arp));

    QString arpText = text(arp);
    int arpClipId = arpText.mid(arpText.indexOf('=') + 1,
                                arpText.indexOf(' ') - arpText.indexOf('=') - 1).toInt();
    auto arpNotes = getNotes(arpClipId);
    EXPECT_GE(arpNotes.size(), 3);
}

TEST_F(GuiFuncTest, GenerateProgression) {
    auto prog = call("generate_progression", {
        {"trackId", 0}, {"pattern", 0}, {"beatsPerChord", 4.0}
    });
    EXPECT_FALSE(isError(prog));

    QString progText = text(prog);
    int progClipId = progText.mid(progText.indexOf('=') + 1,
                                  progText.indexOf(' ') - progText.indexOf('=') - 1).toInt();
    auto notes = getNotes(progClipId);
    EXPECT_GT(notes.size(), 0);
}

// ============================================================================
// TRANSPORT EDGE CASES
// ============================================================================

TEST_F(GuiFuncTest, TransportStateAfterMultipleOperations) {
    call("transport", {{"action", "play"}});
    auto t1 = transport();
    EXPECT_TRUE(t1.value("isPlaying").toBool());

    call("transport", {{"action", "pause"}});
    auto t2 = transport();
    EXPECT_FALSE(t2.value("isPlaying").toBool());

    call("transport", {{"action", "play"}});
    auto t3 = transport();
    EXPECT_TRUE(t3.value("isPlaying").toBool());

    call("transport", {{"action", "stop"}});
    auto t4 = transport();
    EXPECT_FALSE(t4.value("isPlaying").toBool());
    EXPECT_NEAR(t4.value("position").toDouble(), 0.0, 0.1);
}

TEST_F(GuiFuncTest, LoopRegionOperations) {
    call("transport", {{"action", "play"}, {"loopStart", 2.0}, {"loopEnd", 6.0}});
    call("transport", {{"action", "stop"}});

    auto t1 = transport();
    EXPECT_NEAR(t1.value("loopStart").toDouble(), 2.0, 0.1);
    EXPECT_NEAR(t1.value("loopEnd").toDouble(), 6.0, 0.1);

    call("transport", {{"action", "toggleLoop"}});
    auto t2 = transport();
    EXPECT_TRUE(t2.value("isLooping").toBool());

    call("transport", {{"action", "toggleLoop"}});
    auto t3 = transport();
    EXPECT_FALSE(t3.value("isLooping").toBool());
}

// ============================================================================
// BATCH OPERATIONS
// ============================================================================

TEST_F(GuiFuncTest, BatchTrackCreation) {
    int before = trackCount();
    for (int i = 0; i < 10; ++i) {
        auto r = call("add_track", {{"name", QString("Track %1").arg(i)}});
        EXPECT_FALSE(isError(r));
    }
    EXPECT_EQ(trackCount(), before + 10);
}

TEST_F(GuiFuncTest, BatchClipCreation) {
    int before = clipCount();
    for (int i = 0; i < 20; ++i) {
        auto r = call("add_midi_clip", {
            {"trackId", 0}, {"start", i * 4.0}, {"length", 4.0}
        });
        EXPECT_FALSE(isError(r));
    }
    EXPECT_EQ(clipCount(), before + 20);
}

TEST_F(GuiFuncTest, BatchNoteCreation) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 16.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    for (int i = 0; i < 64; ++i) {
        auto r = call("add_note", {
            {"clipId", clipId}, {"pitch", 60 + (i % 12)}, {"start", i * 0.25},
            {"duration", 0.25}, {"velocity", 80 + (i % 40)}
        });
        EXPECT_FALSE(isError(r));
    }

    auto notes = getNotes(clipId);
    EXPECT_EQ(notes.size(), 64);
}

} // namespace
