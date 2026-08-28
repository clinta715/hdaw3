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
#include <QStandardPaths>
#include <QDir>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

// --- Test helpers ---

QJsonObject parseOne(const QByteArray& buf) {
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

QString writeTestWav() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    dir = QDir::fromNativeSeparators(dir);
    QString path = QString("%1/hdaw_env_test_%2.wav")
                       .arg(dir)
                       .arg(QCoreApplication::applicationPid());
    QFile::remove(path);

    constexpr int sampleRate = 44100;
    constexpr int numChannels = 2;
    constexpr int numSeconds = 4;

    juce::AudioBuffer<float> buf(numChannels, sampleRate * numSeconds);
    buf.clear();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(juce::File(path.toStdString())),
                            sampleRate, numChannels, 16, {}, 0));
    if (writer == nullptr) return {};
    writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    writer->flush();
    return path;
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

// list_notes: full note read with range filters (handoff §7 item 5 — the
// only prior way to count notes in a range was remove_notes dryRun).
TEST_F(GuiFuncTest, ListNotesWithFilters) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 16.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    call("add_note", {{"clipId", clipId}, {"pitch", 60}, {"start", 0.0},
                       {"duration", 1.0}, {"velocity", 100}});
    call("add_note", {{"clipId", clipId}, {"pitch", 64}, {"start", 2.0},
                       {"duration", 1.0}, {"velocity", 90}});
    call("add_note", {{"clipId", clipId}, {"pitch", 67}, {"start", 4.0},
                       {"duration", 1.0}, {"velocity", 80}});
    call("add_note", {{"clipId", clipId}, {"pitch", 72}, {"start", 6.0},
                       {"duration", 1.0}, {"velocity", 70}});

    auto listNotes = [&](const QJsonObject& extra) -> QJsonObject {
        auto r = call("list_notes", extra);
        EXPECT_FALSE(isError(r)) << "list_notes failed";
        return QJsonDocument::fromJson(text(r).toUtf8()).object();
    };

    // Unfiltered: count + full field set on every note.
    auto all = listNotes({{"clipId", clipId}});
    EXPECT_EQ(all.value("count").toInt(), 4);
    auto notes = all.value("notes").toArray();
    ASSERT_EQ(notes.size(), 4);
    const auto firstNote = notes[0].toObject();
    for (const char* key : {"noteId", "pitch", "start", "duration", "velocity", "chance",
                            "repeatCount", "repeatRate", "repeatCurve", "occurrence",
                            "recurrence", "gain", "pan", "pitchOffset", "timbre", "pressure"})
        EXPECT_TRUE(firstNote.contains(key)) << "list_notes missing field: " << key;

    // Set one operator field (by noteId) so the read round-trips it.
    int pitch64Id = -1;
    for (const auto& n : notes)
        if (n.toObject().value("pitch").toInt() == 64)
            pitch64Id = n.toObject().value("noteId").toInt();
    ASSERT_GT(pitch64Id, 0);
    call("set_note_chance", {{"noteId", pitch64Id}, {"chance", 0.5}});

    // Range filter: startGte=1, startLt=5 → notes at beats 2 and 4.
    all = listNotes({{"clipId", clipId}, {"startGte", 1.0}, {"startLt", 5.0}});
    EXPECT_EQ(all.value("count").toInt(), 2);
    QSet<int> starts;
    for (const auto& n : all.value("notes").toArray())
        starts.insert(n.toObject().value("start").toInt());
    EXPECT_TRUE(starts.contains(2));
    EXPECT_TRUE(starts.contains(4));

    // Pitch filter.
    all = listNotes({{"clipId", clipId}, {"pitches", QJsonArray{64, 72}}});
    EXPECT_EQ(all.value("count").toInt(), 2);

    // noteId filter (single note).
    all = listNotes({{"clipId", clipId}, {"noteIds", QJsonArray{pitch64Id}}});
    EXPECT_EQ(all.value("count").toInt(), 1);
    EXPECT_EQ(all.value("notes").toArray()[0].toObject().value("noteId").toInt(), pitch64Id);

    // Operator field round-trip: pitch 64's note carries chance 0.5.
    EXPECT_NEAR(all.value("notes").toArray()[0].toObject().value("chance").toDouble(), 0.5, 1e-6);
}


TEST_F(GuiFuncTest, AddNoteToNonMidiClip) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto r = call("add_note", {{"clipId", clipId}, {"pitch", 60},
                                {"start", 0.0}, {"duration", 1.0}, {"velocity", 100}});
    EXPECT_FALSE(isError(r));
}

TEST_F(GuiFuncTest, CcPointAddGetSetRemove) {
    auto add = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int clipId = text(add).mid(text(add).indexOf('=') + 1).toInt();

    auto addCc = call("add_cc_point", {{"clipId", clipId}, {"controllerNumber", 74},
                                       {"beat", 1.0}, {"value", 100}});
    EXPECT_FALSE(isError(addCc));
    int ccId = text(addCc).mid(text(addCc).indexOf('=') + 1).toInt();

    auto getList = [&]() {
        auto r = call("get_cc_points", {{"clipId", clipId}});
        return QJsonDocument::fromJson(text(r).toUtf8()).array();
    };

    auto pts = getList();
    ASSERT_EQ(pts.size(), 1);
    auto p = pts[0].toObject();
    EXPECT_EQ(p.value("ccId").toInt(), ccId);
    EXPECT_EQ(p.value("controllerNumber").toInt(), 74);
    EXPECT_NEAR(p.value("beat").toDouble(), 1.0, 0.001);
    EXPECT_EQ(p.value("value").toInt(), 100);

    auto setR = call("set_cc_point", {{"ccId", ccId}, {"value", 64}});
    EXPECT_FALSE(isError(setR));
    EXPECT_EQ(getList()[0].toObject().value("value").toInt(), 64);

    auto dry = call("remove_cc_point", {{"ccId", ccId}, {"dryRun", true}});
    EXPECT_FALSE(isError(dry));
    EXPECT_EQ(getList().size(), 1);

    auto rm = call("remove_cc_point", {{"ccId", ccId}});
    EXPECT_FALSE(isError(rm));
    EXPECT_EQ(getList().size(), 0);
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

// add_automation_lane binds a lane to a target paramID (here a plugin FX param
// via the compound 100 + slotIndex*100 + paramIndex id). list_automation_lanes
// then reflects the name and paramID — the MCP parity surface for the UI's
// lane picker.
TEST_F(GuiFuncTest, AddAutomationLaneBindsParamID) {
    auto r = call("add_automation_lane", {
        {"trackId", 0}, {"laneName", "S0 Cutoff"}, {"paramID", 105}
    });
    EXPECT_FALSE(isError(r));

    auto lanes = QJsonDocument::fromJson(
        callText("list_automation_lanes", {{"trackId", 0}}).toString().toUtf8()).array();
    bool found = false;
    for (const auto& l : lanes) {
        auto obj = l.toObject();
        if (obj.value("name").toString() == "S0 Cutoff") {
            found = true;
            EXPECT_EQ(obj.value("paramID").toInt(), 105);
            break;
        }
    }
    EXPECT_TRUE(found);
}

// remove_automation_lane resolves the lane by paramID (integer) — the same
// int-or-string addressing findLane uses for add_automation_point.
TEST_F(GuiFuncTest, RemoveAutomationLaneByParamID) {
    call("add_automation_lane", {
        {"trackId", 0}, {"laneName", "S0 Gain"}, {"paramID", 100}
    });

    auto r = call("remove_automation_lane", {{"trackId", 0}, {"lane", 100}});
    EXPECT_FALSE(isError(r));

    auto lanes = QJsonDocument::fromJson(
        callText("list_automation_lanes", {{"trackId", 0}}).toString().toUtf8()).array();
    for (const auto& l : lanes) {
        EXPECT_NE(l.toObject().value("paramID").toInt(), 100);
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

    // Default project ships empty; undoing the added clip leaves zero clips
    EXPECT_EQ(clipCount(), 0);
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

// ============================================================================
// ENVELOPE GENERATION
// ============================================================================

TEST_F(GuiFuncTest, ListEnvelopeShapes) {
    auto r = call("list_envelope_shapes");
    EXPECT_FALSE(isError(r));
    auto content = text(r);
    auto doc = QJsonDocument::fromJson(content.toUtf8());
    auto shapes = doc.object().value("shapes").toArray();
    EXPECT_EQ(shapes.size(), 11);
    QStringList names;
    for (const auto& s : shapes)
        names << s.toObject().value("name").toString();
    EXPECT_TRUE(names.contains("ramp"));
    EXPECT_TRUE(names.contains("adsr"));
    EXPECT_TRUE(names.contains("sine"));
    EXPECT_TRUE(names.contains("triangle"));
    EXPECT_TRUE(names.contains("saw"));
    EXPECT_TRUE(names.contains("square"));
    EXPECT_TRUE(names.contains("pulse"));
    EXPECT_TRUE(names.contains("staircase"));
    EXPECT_TRUE(names.contains("sCurve"));
    EXPECT_TRUE(names.contains("randomWalk"));
    EXPECT_TRUE(names.contains("noise"));
}

TEST_F(GuiFuncTest, GenerateAutomationEnvelope) {
    auto r = call("generate_automation_envelope", {
        {"trackId", 0}, {"lane", "Volume"}, {"shape", "ramp"}
    });
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(GuiFuncTest, GenerateClipGainEnvelope) {
    QString wavPath = writeTestWav();
    ASSERT_FALSE(wavPath.isEmpty()) << "Failed to create test WAV";

    auto clipR = call("add_audio_clip", {
        {"trackId", 0}, {"start", 0.0}, {"length", 4.0},
        {"sourceFile", wavPath}, {"name", "EnvTest"}
    });
    ASSERT_FALSE(isError(clipR)) << text(clipR).toStdString();
    int clipId = text(clipR).mid(text(clipR).indexOf('=') + 1).toInt();

    auto r = call("generate_clip_gain_envelope", {{"clipId", clipId}, {"shape", "adsr"}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(GuiFuncTest, GenerateClipCcLane) {
    auto clipR = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    ASSERT_FALSE(isError(clipR));
    int clipId = text(clipR).mid(text(clipR).indexOf('=') + 1).toInt();

    auto r = call("generate_clip_cc_lane", {
        {"clipId", clipId}, {"controllerNumber", 1}, {"shape", "sine"}
    });
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(GuiFuncTest, GenerateEnvelopeInvalidShape) {
    auto r = call("generate_automation_envelope", {
        {"trackId", 0}, {"lane", "Volume"}, {"shape", "bogus"}
    });
    EXPECT_TRUE(isError(r));
    EXPECT_TRUE(text(r).contains("unknown shape"));
}

// ============================================================================
// SAMPLER RPC/MCP FAMILY
// ============================================================================

// MCP parity: the sampler_* tools promised by the sampler RPC/MCP family must
// be registered with the server, alongside the extended sampler_get_state.
TEST_F(GuiFuncTest, SamplerToolsRegistered) {
    for (const char* name : {"set_sampler_param", "set_sampler_mode",
                             "detect_sampler_slices", "trigger_sampler_slice",
                             "sampler_set_sample", "sampler_get_state"})
        EXPECT_TRUE(server->tools().contains(name)) << "missing tool: " << name;
}

// MCP round-trip for the sampler mode control: add a sampler slot, switch it
// to slice mode, and read the state back.
TEST_F(GuiFuncTest, SamplerSetModeRoundTrip) {
    auto add = call("add_fx", {{"trackId", 0}, {"fxType", "sampler"}});
    ASSERT_FALSE(isError(add)) << text(add).toStdString();
    QString addText = text(add);                    // "slot=N"
    int slot = addText.mid(addText.indexOf('=') + 1).toInt();

    auto setMode = call("set_sampler_mode", {{"trackId", 0}, {"slotIndex", slot}, {"mode", "slice"}});
    ASSERT_FALSE(isError(setMode)) << text(setMode).toStdString();

    auto state = QJsonDocument::fromJson(
        callText("sampler_get_state", {{"trackId", 0}, {"slotIndex", slot}}).toString().toUtf8()).object();
    EXPECT_EQ(state.value("mode").toString().toStdString(), "slice");
}

} // namespace
