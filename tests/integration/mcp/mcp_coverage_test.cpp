#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QVector>
#include <QTemporaryDir>
#include <algorithm>

namespace {

QJsonObject parseOne(const QByteArray& buf) {
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

class McpCoverageTest : public ::testing::Test {
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

    QJsonArray trackList() {
        auto t = callText("list_tracks");
        return QJsonDocument::fromJson(t.toString().toUtf8()).array();
    }

    QJsonArray clipList() {
        auto t = callText("list_clips");
        return QJsonDocument::fromJson(t.toString().toUtf8()).array();
    }

    int trackCount() { return trackList().size(); }
    int clipCount() { return clipList().size(); }

    QJsonObject findClip(int clipId) {
        auto clips = clipList();
        for (const auto& c : clips) {
            if (c.toObject().value("id").toInt() == clipId)
                return c.toObject();
        }
        return {};
    }

    QJsonObject findTrack(int index) {
        auto tracks = trackList();
        for (const auto& t : tracks) {
            if (t.toObject().value("id").toInt() == index)
                return t.toObject();
        }
        return {};
    }

    QJsonArray getNotes(int clipId) {
        auto r = callText("get_clip", {{"clipId", clipId}});
        auto obj = QJsonDocument::fromJson(r.toString().toUtf8()).object();
        return obj.value("notes").toArray();
    }

    QJsonArray toolList() {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = nextId_++;
        req["method"] = "tools/list";
        loopback->drainOutgoing();
        loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (!loopback->waitForOutgoing(500, &out)) return {};
        auto resp = parseOne(out);
        return resp.value("result").toObject().value("tools").toArray();
    }

    int addMidiClip(int trackId, double start, double length, const QString& name = {}) {
        QJsonObject args{{"trackId", trackId}, {"start", start}, {"length", length}};
        if (!name.isEmpty()) args["name"] = name;
        auto r = call("add_midi_clip", args);
        if (isError(r)) return -1;
        QString resp = text(r);
        return resp.mid(resp.indexOf('=') + 1).toInt();
    }

    int addNote(int clipId, int pitch, double start, double duration, int velocity = 100) {
        auto r = call("add_note", {{"clipId", clipId}, {"pitch", pitch},
                                   {"start", start}, {"duration", duration}, {"velocity", velocity}});
        if (isError(r)) return -1;
        QString resp = text(r);
        return resp.mid(resp.indexOf('=') + 1).toInt();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
    int nextId_ = 1;
};

// ============================================================================
// REGION OPS
// ============================================================================

TEST_F(McpCoverageTest, RippleDelete) {
    int clip1 = addMidiClip(0, 0.0, 4.0, "A");
    int clip2 = addMidiClip(0, 4.0, 4.0, "B");
    ASSERT_GT(clip1, 0);
    ASSERT_GT(clip2, 0);

    auto r = call("ripple_delete", {{"startBeat", 0.0}, {"endBeat", 4.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    EXPECT_TRUE(findClip(clip1).isEmpty());

    auto c = findClip(clip2);
    EXPECT_FALSE(c.isEmpty());
    EXPECT_NEAR(c.value("start").toDouble(), 0.0, 0.01);
}

TEST_F(McpCoverageTest, InsertSilence) {
    int clip1 = addMidiClip(0, 0.0, 4.0, "A");
    ASSERT_GT(clip1, 0);

    auto r = call("insert_silence", {{"startBeat", 0.0}, {"endBeat", 4.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto c = findClip(clip1);
    EXPECT_FALSE(c.isEmpty());
    EXPECT_NEAR(c.value("start").toDouble(), 4.0, 0.01);
}

TEST_F(McpCoverageTest, DuplicateRegion) {
    int clip1 = addMidiClip(0, 0.0, 4.0, "A");
    ASSERT_GT(clip1, 0);

    int before = clipCount();
    auto r = call("duplicate_region", {{"startBeat", 0.0}, {"endBeat", 4.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    EXPECT_GE(clipCount(), before + 1);
    auto clips = clipList();
    bool foundDupe = false;
    for (const auto& c : clips) {
        if (c.toObject().value("start").toDouble() >= 3.9 && c.toObject().value("start").toDouble() <= 4.1) {
            foundDupe = true;
            break;
        }
    }
    EXPECT_TRUE(foundDupe) << "Expected a duplicated clip near beat 4";
}

TEST_F(McpCoverageTest, LoopClip) {
    int clipId = addMidiClip(0, 0.0, 4.0, "Loop");
    ASSERT_GT(clipId, 0);
    addNote(clipId, 60, 0.0, 1.0);

    auto r = call("loop_clip", {{"clipId", clipId}, {"repetitions", 3}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto c = findClip(clipId);
    EXPECT_FALSE(c.isEmpty());
    EXPECT_NEAR(c.value("duration").toDouble(), 12.0, 0.01);

    auto notes = getNotes(clipId);
    EXPECT_EQ(notes.size(), 3);

    QVector<double> noteStarts;
    for (const auto& n : notes)
        noteStarts.append(n.toObject().value("start").toDouble());
    std::sort(noteStarts.begin(), noteStarts.end());
    ASSERT_EQ(noteStarts.size(), 3);
    EXPECT_NEAR(noteStarts[0], 0.0, 0.01);
    EXPECT_NEAR(noteStarts[1], 4.0, 0.01);
    EXPECT_NEAR(noteStarts[2], 8.0, 0.01);
}

// ============================================================================
// TRACK OPS
// ============================================================================

TEST_F(McpCoverageTest, MoveTrack) {
    call("add_track", {{"name", "A"}});
    call("add_track", {{"name", "B"}});
    call("add_track", {{"name", "C"}});
    int count = trackCount();

    auto r = call("move_track", {{"trackId", count - 1}, {"newIndex", 0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto t = findTrack(0);
    EXPECT_EQ(t.value("name").toString().toStdString(), "C");
}

TEST_F(McpCoverageTest, DuplicateTrack) {
    call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    int before = trackCount();

    auto r = call("duplicate_track", {{"trackId", 0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
    EXPECT_EQ(trackCount(), before + 1);

    auto t = findTrack(trackCount() - 1);
    EXPECT_EQ(t.value("clipCount").toInt(), 1);
}

TEST_F(McpCoverageTest, AddTrackWithFx) {
    int before = trackCount();
    auto r = call("add_track_with_fx", {{"name", "EQ Track"}, {"fxType", "eq"}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
    EXPECT_EQ(trackCount(), before + 1);

    auto fxText = callText("list_fx", {{"trackId", trackCount() - 1}});
    auto fxArr = QJsonDocument::fromJson(fxText.toString().toUtf8()).array();
    EXPECT_GE(fxArr.size(), 1);
}

// ============================================================================
// NOTE OPERATORS
// ============================================================================

TEST_F(McpCoverageTest, SetNoteChance) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_chance", {{"noteId", noteId}, {"chance", 0.5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteRepeatCount) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_repeat_count", {{"noteId", noteId}, {"repeatCount", 4}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteRepeatRate) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_repeat_rate", {{"noteId", noteId}, {"repeatRate", 0.25}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteRepeatCurve) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_repeat_curve", {{"noteId", noteId}, {"repeatCurve", 0.5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteOccurrence) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_occurrence", {{"noteId", noteId}, {"occurrence", 5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteRecurrence) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_recurrence", {{"noteId", noteId}, {"recurrence", 1}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteGain) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_gain", {{"noteId", noteId}, {"gain", 1.5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNotePan) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_pan", {{"noteId", noteId}, {"pan", -0.5}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNotePitchOffset) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_pitch_offset", {{"noteId", noteId}, {"pitchOffset", 2.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteTimbre) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_timbre", {{"noteId", noteId}, {"timbre", 0.7}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNotePressure) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    int noteId = addNote(clipId, 60, 0.0, 1.0);
    ASSERT_GT(noteId, 0);
    auto r = call("set_note_pressure", {{"noteId", noteId}, {"pressure", 0.8}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetClipSeed) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    ASSERT_GT(clipId, 0);
    auto r = call("set_clip_seed", {{"clipId", clipId}, {"seed", 42}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetNoteVelocitiesAbsolute) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    addNote(clipId, 60, 0.0, 1.0, 80);
    addNote(clipId, 62, 1.0, 1.0, 90);
    addNote(clipId, 64, 2.0, 1.0, 70);

    auto r = call("set_note_velocities", {{"clipId", clipId}, {"velocity", 100}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto notes = getNotes(clipId);
    for (const auto& n : notes)
        EXPECT_EQ(n.toObject().value("velocity").toInt(), 100);
}

TEST_F(McpCoverageTest, SetNoteVelocitiesRelative) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    addNote(clipId, 60, 0.0, 1.0, 80);

    auto r = call("set_note_velocities", {{"clipId", clipId}, {"velocityOffset", 10}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto notes = getNotes(clipId);
    EXPECT_EQ(notes[0].toObject().value("velocity").toInt(), 90);
}

TEST_F(McpCoverageTest, SetNoteVelocitiesRandom) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    addNote(clipId, 60, 0.0, 1.0, 80);

    auto r = call("set_note_velocities", {
        {"clipId", clipId}, {"velocityMin", 60}, {"velocityMax", 120}
    });
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto notes = getNotes(clipId);
    int vel = notes[0].toObject().value("velocity").toInt();
    EXPECT_GE(vel, 60);
    EXPECT_LE(vel, 120);
}

// ============================================================================
// TEMPO POINTS
// ============================================================================

TEST_F(McpCoverageTest, AddTempoPoint) {
    auto r = call("add_tempo_point", {{"timeSeconds", 2.0}, {"bpm", 140.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, RemoveTempoPoint) {
    call("add_tempo_point", {{"timeSeconds", 2.0}, {"bpm", 140.0}});
    auto r = call("remove_tempo_point", {{"index", 1}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetTempoPointBpm) {
    call("add_tempo_point", {{"timeSeconds", 2.0}, {"bpm", 140.0}});
    auto r = call("set_tempo_point_bpm", {{"index", 1}, {"bpm", 160.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, SetTempoPointTime) {
    call("add_tempo_point", {{"timeSeconds", 2.0}, {"bpm", 140.0}});
    auto r = call("set_tempo_point_time", {{"index", 1}, {"timeSeconds", 4.0}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

// ============================================================================
// ARRANGER
// ============================================================================

TEST_F(McpCoverageTest, ArrangerRegionCrud) {
    // Add region
    auto addR = call("add_arranger_region", {{"name", "Intro"}, {"startTime", 0.0}, {"duration", 8.0}});
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    QString regionId = text(addR); // "regionID=<id>"
    regionId = regionId.mid(regionId.indexOf('=') + 1);

    // List regions
    auto listR = QJsonDocument::fromJson(
        callText("get_arranger_regions").toString().toUtf8()).array();
    ASSERT_EQ(listR.size(), 1);
    EXPECT_EQ(listR[0].toObject().value("name").toString().toStdString(), "Intro");

    // Rename
    auto nameR = call("set_arranger_region_name", {{"regionID", regionId}, {"name", "Verse"}});
    EXPECT_FALSE(isError(nameR)) << text(nameR).toStdString();

    // Move/resize
    auto boundsR = call("set_arranger_region_bounds", {
        {"regionID", regionId}, {"startTime", 4.0}, {"duration", 16.0}
    });
    EXPECT_FALSE(isError(boundsR)) << text(boundsR).toStdString();

    // Recolor
    auto colorR = call("set_arranger_region_color", {{"regionID", regionId}, {"color", 0xFF0000}});
    EXPECT_FALSE(isError(colorR)) << text(colorR).toStdString();

    // Verify all changes
    listR = QJsonDocument::fromJson(
        callText("get_arranger_regions").toString().toUtf8()).array();
    EXPECT_EQ(listR[0].toObject().value("name").toString().toStdString(), "Verse");
    EXPECT_NEAR(listR[0].toObject().value("startTime").toDouble(), 4.0, 0.01);
    EXPECT_NEAR(listR[0].toObject().value("duration").toDouble(), 16.0, 0.01);

    // Remove
    auto removeR = call("remove_arranger_region", {{"regionID", regionId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
    listR = QJsonDocument::fromJson(
        callText("get_arranger_regions").toString().toUtf8()).array();
    EXPECT_EQ(listR.size(), 0);
}

TEST_F(McpCoverageTest, ArrangerChainCrud) {
    // Add chain
    auto addR = call("add_arranger_chain", {{"name", "Arrangement A"}});
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    QString chainId = text(addR); // "chainID=<id>"
    chainId = chainId.mid(chainId.indexOf('=') + 1);

    // List chains
    auto listR = QJsonDocument::fromJson(
        callText("get_arranger_chains").toString().toUtf8()).array();
    ASSERT_EQ(listR.size(), 1);
    EXPECT_EQ(listR[0].toObject().value("name").toString().toStdString(), "Arrangement A");

    // Rename
    auto nameR = call("set_arranger_chain_name", {{"chainID", chainId}, {"name", "Main"}});
    EXPECT_FALSE(isError(nameR)) << text(nameR).toStdString();

    // Add second chain, activate it
    auto addR2 = call("add_arranger_chain", {{"name", "Alt"}});
    QString chainId2 = text(addR2);
    chainId2 = chainId2.mid(chainId2.indexOf('=') + 1);
    auto actR = call("set_arranger_chain_active", {{"chainID", chainId2}});
    EXPECT_FALSE(isError(actR)) << text(actR).toStdString();

    // Verify only one active
    listR = QJsonDocument::fromJson(
        callText("get_arranger_chains").toString().toUtf8()).array();
    int activeCount = 0;
    for (const auto& c : listR)
        if (c.toObject().value("isActive").toBool()) activeCount++;
    EXPECT_EQ(activeCount, 1);

    // Remove first chain
    auto removeR = call("remove_arranger_chain", {{"chainID", chainId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
}

TEST_F(McpCoverageTest, ArrangerChainEntries) {
    // Setup: region + chain
    auto regR = call("add_arranger_region", {{"name", "A"}, {"startTime", 0.0}, {"duration", 4.0}});
    QString regionId = text(regR);
    regionId = regionId.mid(regionId.indexOf('=') + 1);
    auto chainR = call("add_arranger_chain", {{"name", "C"}});
    QString chainId = text(chainR);
    chainId = chainId.mid(chainId.indexOf('=') + 1);

    // Add entry
    auto addEntry = call("add_chain_entry", {{"chainID", chainId}, {"regionID", regionId}});
    EXPECT_FALSE(isError(addEntry)) << text(addEntry).toStdString();

    // Add second entry
    auto regR2 = call("add_arranger_region", {{"name", "B"}, {"startTime", 4.0}, {"duration", 4.0}});
    QString regionId2 = text(regR2);
    regionId2 = regionId2.mid(regionId2.indexOf('=') + 1);
    call("add_chain_entry", {{"chainID", chainId}, {"regionID", regionId2}});

    // Verify 2 entries
    auto chains = QJsonDocument::fromJson(
        callText("get_arranger_chains").toString().toUtf8()).array();
    auto entries = chains[0].toObject().value("entries").toArray();
    ASSERT_EQ(entries.size(), 2);

    // Reorder
    auto reorderR = call("reorder_chain_entry", {
        {"chainID", chainId}, {"fromIndex", 0}, {"toIndex", 1}
    });
    EXPECT_FALSE(isError(reorderR)) << text(reorderR).toStdString();

    // Set repeat
    auto repeatR = call("set_chain_entry_repeat", {
        {"chainID", chainId}, {"entryIndex", 0}, {"repeatCount", 3}
    });
    EXPECT_FALSE(isError(repeatR)) << text(repeatR).toStdString();

    // Remove entry
    auto removeEntry = call("remove_chain_entry", {{"chainID", chainId}, {"entryIndex", 0}});
    EXPECT_FALSE(isError(removeEntry)) << text(removeEntry).toStdString();
}

TEST_F(McpCoverageTest, FlattenArranger) {
    // Need clips on the timeline for flatten to copy
    auto addClip = call("add_midi_clip", {{"trackId", 0}, {"start", 0.0}, {"length", 4.0}});
    EXPECT_FALSE(isError(addClip)) << text(addClip).toStdString();
    int clipsBefore = clipCount();
    ASSERT_GT(clipsBefore, 0);

    // Add region + chain + entry
    auto regR = call("add_arranger_region", {{"name", "A"}, {"startTime", 0.0}, {"duration", 4.0}});
    EXPECT_FALSE(isError(regR)) << text(regR).toStdString();
    QString regionId = text(regR);
    regionId = regionId.mid(regionId.indexOf('=') + 1);
    auto chainR = call("add_arranger_chain", {{"name", "C"}});
    EXPECT_FALSE(isError(chainR)) << text(chainR).toStdString();
    QString chainId = text(chainR);
    chainId = chainId.mid(chainId.indexOf('=') + 1);
    auto entryR = call("add_chain_entry", {{"chainID", chainId}, {"regionID", regionId}});
    EXPECT_FALSE(isError(entryR)) << text(entryR).toStdString();

    // Make chain active (first chain is active by default, but be explicit)
    call("set_arranger_chain_active", {{"chainID", chainId}});

    auto r = call("flatten_arranger");
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    // Should have created clips from the chain (flatten replaces all clips)
    EXPECT_GE(clipCount(), 1);
}

// ============================================================================
// SENDS
// ============================================================================

TEST_F(McpCoverageTest, SendReadEmpty) {
    auto sendsR = callText("get_track_sends", {{"trackId", 0}});
    auto sends = QJsonDocument::fromJson(sendsR.toString().toUtf8()).array();
    EXPECT_EQ(sends.size(), 0);
}

// ============================================================================
// MIDI FX
// ============================================================================

TEST_F(McpCoverageTest, MidiFxAddListBypassSetRemove) {
    // Add arpeggiator
    auto addR = call("add_midi_fx", {{"trackId", 0}, {"fxType", "arpeggiator"}});
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    int slot = 0; // add_midi_fx returns "ok"; first slot is index 0

    // List params — response is {fxType, bypassed, params:[...]}
    auto paramsCallR = call("list_midi_fx_params", {{"trackId", 0}, {"slotIndex", slot}});
    EXPECT_FALSE(isError(paramsCallR)) << "list_midi_fx_params error: " << text(paramsCallR).toStdString();
    auto paramsR = text(paramsCallR);
    auto paramsObj = QJsonDocument::fromJson(paramsR.toUtf8()).object();
    ASSERT_FALSE(paramsObj.isEmpty()) << "list_midi_fx_params returned: " << paramsR.toStdString();
    auto params = paramsObj.value("params").toArray();
    EXPECT_GT(params.size(), 0) << "params response: " << paramsR.toStdString();

    // Bypass
    auto bypassR = call("set_midi_fx_bypass", {
        {"trackId", 0}, {"slotIndex", slot}, {"bypassed", true}
    });
    EXPECT_FALSE(isError(bypassR)) << text(bypassR).toStdString();

    // Set param by name — use first param name from the list
    if (params.size() > 0) {
        QString paramName = params[0].toObject().value("name").toString();
        auto setR = call("set_midi_fx_param", {
            {"trackId", 0}, {"slotIndex", slot}, {"paramName", paramName}, {"value", 0.5}
        });
        EXPECT_FALSE(isError(setR)) << text(setR).toStdString();
    }

    // Set param normalized
    auto setNormR = call("set_midi_fx_param_normalized", {
        {"trackId", 0}, {"slotIndex", slot}, {"paramIndex", 0}, {"value", 0.75}
    });
    EXPECT_FALSE(isError(setNormR)) << text(setNormR).toStdString();

    // Remove
    auto removeR = call("remove_midi_fx", {{"trackId", 0}, {"slotIndex", slot}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
}

// ============================================================================
// SESSION
// ============================================================================

TEST_F(McpCoverageTest, SessionClipLifecycle) {
    // Create a session clip
    auto createR = call("session_create_clip", {{"trackIndex", 0}, {"sceneIndex", 0}});
    EXPECT_FALSE(isError(createR)) << text(createR).toStdString();

    // Extract clipId from "created clip <id> in scene <scene>"
    QString createText = text(createR);
    int clipStart = createText.indexOf("clip ") + 5;
    int clipEnd = createText.indexOf(" in", clipStart);
    int clipId = createText.mid(clipStart, clipEnd - clipStart).toInt();
    EXPECT_GT(clipId, 0);

    // Get clip states
    auto statesR = callText("session_get_clip_states");
    auto states = QJsonDocument::fromJson(statesR.toString().toUtf8()).array();
    EXPECT_GT(states.size(), 0);

    // Set clip scene
    auto setSceneR = call("session_set_clip_scene", {{"clipId", clipId}, {"sceneIndex", 1}});
    EXPECT_FALSE(isError(setSceneR)) << text(setSceneR).toStdString();

    // Launch scene
    auto launchR = call("session_launch_scene", {{"sceneIndex", 1}});
    EXPECT_FALSE(isError(launchR)) << text(launchR).toStdString();

    // Stop all
    auto stopR = call("session_stop_all");
    EXPECT_FALSE(isError(stopR)) << text(stopR).toStdString();
}

// ============================================================================
// LIBRARY
// ============================================================================

TEST_F(McpCoverageTest, LibraryAddListSearchRemove) {
    // Create a temp dir for the library
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // Add library
    auto addR = call("add_library", {
        {"name", "TestLib"}, {"path", dir.path()}, {"type", "midi"}
    });
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    // add_library returns JSON {"id":"..."} — parse the id
    auto addObj = QJsonDocument::fromJson(text(addR).toUtf8()).object();
    QString libId = addObj.value("id").toString();
    ASSERT_FALSE(libId.isEmpty()) << "add_library response: " << text(addR).toStdString();

    // List libraries
    auto listR = QJsonDocument::fromJson(
        callText("list_libraries").toString().toUtf8()).array();
    EXPECT_GT(listR.size(), 0);

    // Set autoscan
    auto autoscanR = call("set_library_autoscan", {{"id", libId}, {"enabled", true}});
    EXPECT_FALSE(isError(autoscanR)) << text(autoscanR).toStdString();

    // Scan
    auto scanR = call("scan_library", {{"id", libId}});
    EXPECT_FALSE(isError(scanR)) << text(scanR).toStdString();

    // Search (empty library — should return empty, not error)
    auto searchR = call("search_library", {{"query", "test"}});
    EXPECT_FALSE(isError(searchR)) << text(searchR).toStdString();

    // Remove
    auto removeR = call("remove_library", {{"id", libId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();

    // Verify removed
    listR = QJsonDocument::fromJson(
        callText("list_libraries").toString().toUtf8()).array();
    for (const auto& l : listR)
        EXPECT_NE(l.toObject().value("id").toString(), libId);
}

// TimbreLib sidecar ingestion end-to-end through the MCP surface:
// add_library(audio) -> scan_library -> search_library with a tag word as query,
// and get_library_entry — both must carry tags/description from the sidecar.
TEST_F(McpCoverageTest, LibrarySidecarSearchAndGetEntry) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // Write a WAV plus a TimbreLib sidecar (<file>.wav.timbre.json) into the dir.
    juce::File wavFile(juce::String(dir.path().toStdString()) + "/beat.wav");
    {
        auto outStream = wavFile.createOutputStream();
        ASSERT_NE(outStream, nullptr);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(outStream.get(), 44100.0, 1, 16, {}, 0));
        ASSERT_NE(writer, nullptr);
        outStream.release(); // writer takes ownership
        juce::AudioBuffer<float> buffer(1, 44100);
        buffer.clear();
        writer->writeFromAudioSampleBuffer(buffer, 0, 44100);
        writer.reset();
    }
    juce::File sidecar(juce::String(dir.path().toStdString()) + "/beat.wav.timbre.json");
    sidecar.replaceWithText(
        "{\"dsp_words\":\"dark gritty pad\","
        "\"prose\":\"A dark gritty atmospheric pad with a slow attack.\","
        "\"captions\":[[\"dark pad\",0.95],[\"gritty atmosphere\",0.90]],"
        "\"tags\":[[\"dark\",0.98],[\"gritty\",0.93],[\"pad\",0.88]]}");

    // Add an AUDIO library pointing at the temp dir.
    auto addR = call("add_library", {
        {"name", "TimbreLib"}, {"path", dir.path()}, {"type", "audio"}
    });
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    auto addObj = QJsonDocument::fromJson(text(addR).toUtf8()).object();
    QString libId = addObj.value("id").toString();
    ASSERT_FALSE(libId.isEmpty()) << "add_library response: " << text(addR).toStdString();

    // Scan (async — runs on the manager's threadpool; poll until done).
    auto scanR = call("scan_library", {{"id", libId}});
    EXPECT_FALSE(isError(scanR)) << text(scanR).toStdString();
    for (int i = 0; i < 100 && engine->getFileLibraryManager().isScanning(); ++i)
        QThread::msleep(100);
    ASSERT_FALSE(engine->getFileLibraryManager().isScanning())
        << "scan did not complete in time";

    // search_library with a tag word ("gritty" exists only in tags/dsp_words —
    // not in the file name or path) must return the entry with tags+description.
    // Scoped by libraryId so other libraries (e.g. appdata-registry leftovers)
    // cannot add unexpected results.
    auto searchText = callText("search_library", {{"query", "gritty"}, {"libraryId", libId}});
    ASSERT_FALSE(searchText.isUndefined()) << "search_library returned no text";
    auto arr = QJsonDocument::fromJson(searchText.toString().toUtf8()).array();
    ASSERT_EQ(arr.size(), 1) << "expected exactly one search result";
    auto entry = arr[0].toObject();
    EXPECT_EQ(entry.value("name").toString().toStdString(), "beat.wav");
    EXPECT_TRUE(entry.contains("tags")) << "search result must carry tags";
    EXPECT_TRUE(entry.value("tags").toString().contains("dark gritty pad"));
    EXPECT_TRUE(entry.value("tags").toString().contains("gritty"));
    EXPECT_TRUE(entry.contains("description")) << "search result must carry description";
    EXPECT_EQ(entry.value("description").toString().toStdString(),
              "A dark gritty atmospheric pad with a slow attack.");

    // get_library_entry must return the same sidecar data.
    auto getText = callText("get_library_entry", {
        {"libraryId", libId}, {"path", entry.value("path").toString()}
    });
    auto getObj = QJsonDocument::fromJson(getText.toString().toUtf8()).object();
    EXPECT_EQ(getObj.value("name").toString().toStdString(), "beat.wav");
    EXPECT_TRUE(getObj.contains("tags")) << "get_library_entry must carry tags";
    EXPECT_TRUE(getObj.value("tags").toString().contains("dark gritty pad"));
    EXPECT_TRUE(getObj.contains("description")) << "get_library_entry must carry description";
    EXPECT_EQ(getObj.value("description").toString().toStdString(),
              "A dark gritty atmospheric pad with a slow attack.");

    // Clean up: remove the library so the appdata registry is not polluted
    // (mirrors LibraryAddListSearchRemove). Without this, the persisted entry
    // file survives the temp dir and leaks tags into later unscoped searches.
    auto removeR = call("remove_library", {{"id", libId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
}

// ── cluster_library / related_samples (clustering v1.1) ──────────────────────
// docs/plans/2026-08-25-library-clustering.md, G4. Temp WAVs + TimbreLib
// sidecars with numeric `dsp` dicts; two timbre "families" (dark vs bright).

namespace {

// Sidecar with a complete 20-key `dsp` dict. Three dims carry the family
// signal (centroid / mel_low / mel_high); the rest are constants.
QString dspSidecar(const char* words, const char* prose,
                   double centroid, double melLow, double melHigh) {
    return QStringLiteral(
        "{\"dsp_words\":\"%1\",\"prose\":\"%2\","
        "\"captions\":[[\"%1\",0.9]],\"tags\":[[\"%1\",0.9]],"
        "\"dsp\":{\"duration\":2.0,\"rms\":0.1,\"peak\":0.5,\"crest_dB\":12.0,\"zcr\":0.1,"
        "\"centroid\":%3,\"bandwidth\":800.0,\"rolloff85\":500.0,\"rolloff95\":2000.0,"
        "\"flatness\":0.05,\"spectral_crest\":300.0,\"spec_irregularity\":0.2,"
        "\"mel_low\":%4,\"mel_mid\":0.2,\"mel_high\":%5,\"attack_s\":0.01,\"decay_s\":0.5,"
        "\"f0_hz\":0.0,\"tonal_fraction\":0.0,\"f0_sweep\":9.0}}")
        .arg(QString::fromUtf8(words), QString::fromUtf8(prose))
        .arg(QString::number(centroid, 'f', 6),
             QString::number(melLow, 'f', 6),
             QString::number(melHigh, 'f', 6));
}

bool writeWavWithSidecar(const QString& dir, const char* name,
                         const char* words, const char* prose,
                         double centroid, double melLow, double melHigh) {
    const std::string base = dir.toStdString() + "/" + name;
    juce::File wav(base);
    auto outStream = wav.createOutputStream();
    if (outStream == nullptr) return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(outStream.get(), 44100.0, 1, 16, {}, 0));
    if (writer == nullptr) return false;
    outStream.release(); // writer takes ownership
    juce::AudioBuffer<float> buffer(1, 44100);
    buffer.clear();
    writer->writeFromAudioSampleBuffer(buffer, 0, 44100);
    writer.reset();
    juce::File sidecar(base + ".timbre.json");
    sidecar.replaceWithText(dspSidecar(words, prose, centroid, melLow, melHigh).toStdString());
    return true;
}

} // namespace

TEST_F(McpCoverageTest, LibraryClusterTwoSidecarFamilies) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "dark1.wav", "dark, low", "a dark low texture",
                                    150.0, 0.75, 0.05));
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "dark2.wav", "dark, low", "a dark low texture",
                                    160.0, 0.73, 0.06));
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "bright1.wav", "bright, high", "a bright high texture",
                                    5200.0, 0.05, 0.70));
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "bright2.wav", "bright, high", "a bright high texture",
                                    5400.0, 0.04, 0.72));

    auto addR = call("add_library", {{"name", "ClusterLib"}, {"path", dir.path()}, {"type", "audio"}});
    ASSERT_FALSE(isError(addR)) << text(addR).toStdString();
    QString libId = QJsonDocument::fromJson(text(addR).toUtf8()).object().value("id").toString();
    ASSERT_FALSE(libId.isEmpty());

    auto scanR = call("scan_library", {{"id", libId}});
    EXPECT_FALSE(isError(scanR));
    for (int i = 0; i < 100 && engine->getFileLibraryManager().isScanning(); ++i)
        QThread::msleep(100);
    ASSERT_FALSE(engine->getFileLibraryManager().isScanning());

    QJsonArray ids{libId};
    auto clusterR = call("cluster_library", {{"libraryIds", ids}, {"k", 2}});
    ASSERT_FALSE(isError(clusterR)) << text(clusterR).toStdString();
    auto obj = QJsonDocument::fromJson(text(clusterR).toUtf8()).object();
    EXPECT_EQ(obj.value("method").toString().toStdString(), "hybrid");
    EXPECT_EQ(obj.value("k").toInt(), 2);
    auto clusters = obj.value("clusters").toArray();
    ASSERT_EQ(clusters.size(), 2) << "two sidecar families -> two clusters";

    QStringList darkMembers, brightMembers;
    int totalMembers = 0;
    for (const auto& c : clusters) {
        auto co = c.toObject();
        EXPECT_TRUE(co.value("id").toString().startsWith("c"));
        EXPECT_FALSE(co.value("label").toString().isEmpty());
        EXPECT_GT(co.value("size").toInt(), 0);
        EXPECT_EQ(co.value("size").toInt(), co.value("members").toArray().size());
        for (const auto& m : co.value("members").toArray()) {
            auto mo = m.toObject();
            totalMembers++;
            // Members carry the sidecar tags (G4: "members carry tags").
            EXPECT_TRUE(mo.contains("tags"));
            EXPECT_TRUE(mo.value("tags").toString().contains("dark")
                        || mo.value("tags").toString().contains("bright"));
            EXPECT_TRUE(mo.contains("similarity"));
            EXPECT_GE(mo.value("similarity").toDouble(), 0.0);
            if (mo.value("name").toString().startsWith("dark")) darkMembers << mo.value("name").toString();
            else brightMembers << mo.value("name").toString();
        }
    }
    EXPECT_EQ(totalMembers, 4) << "all entries clustered";
    EXPECT_EQ(darkMembers.size(), 2) << "dark family in one cluster";
    EXPECT_EQ(brightMembers.size(), 2) << "bright family in the other cluster";
    EXPECT_TRUE(obj.value("unassigned").toArray().isEmpty());

    auto removeR = call("remove_library", {{"id", libId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
}

TEST_F(McpCoverageTest, LibraryRelatedSamplesByFilePathAndQuery) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "dark1.wav", "dark, low", "a dark low texture",
                                    150.0, 0.75, 0.05));
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "dark2.wav", "dark, low", "a dark low texture",
                                    160.0, 0.73, 0.06));
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "bright1.wav", "bright, high", "a bright high texture",
                                    5200.0, 0.05, 0.70));
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "bright2.wav", "bright, high", "a bright high texture",
                                    5400.0, 0.04, 0.72));

    auto addR = call("add_library", {{"name", "RelatedLib"}, {"path", dir.path()}, {"type", "audio"}});
    ASSERT_FALSE(isError(addR)) << text(addR).toStdString();
    QString libId = QJsonDocument::fromJson(text(addR).toUtf8()).object().value("id").toString();
    ASSERT_FALSE(libId.isEmpty());

    auto scanR = call("scan_library", {{"id", libId}});
    EXPECT_FALSE(isError(scanR));
    for (int i = 0; i < 100 && engine->getFileLibraryManager().isScanning(); ++i)
        QThread::msleep(100);
    ASSERT_FALSE(engine->getFileLibraryManager().isScanning());

    // Seed path comes straight from the indexed entries.
    auto searchR = call("search_library", {{"libraryId", libId}});
    auto entries = QJsonDocument::fromJson(text(searchR).toUtf8()).array();
    ASSERT_EQ(entries.size(), 4);
    QString dark1Path;
    for (const auto& e : entries)
        if (e.toObject().value("name").toString() == "dark1.wav")
            dark1Path = e.toObject().value("path").toString();
    ASSERT_FALSE(dark1Path.isEmpty());

    QJsonArray ids{libId};
    auto relR = call("related_samples", {{"libraryIds", ids}, {"filePath", dark1Path}});
    ASSERT_FALSE(isError(relR)) << text(relR).toStdString();
    auto obj = QJsonDocument::fromJson(text(relR).toUtf8()).object();
    EXPECT_EQ(obj.value("method").toString().toStdString(), "hybrid");
    EXPECT_EQ(obj.value("seed").toObject().value("name").toString().toStdString(), "dark1.wav");
    auto results = obj.value("results").toArray();
    ASSERT_EQ(results.size(), 3) << "seed excludes itself";
    // Same-family neighbor ranked first.
    EXPECT_EQ(results[0].toObject().value("name").toString().toStdString(), "dark2.wav");
    EXPECT_TRUE(results[0].toObject().contains("tags"));
    EXPECT_TRUE(results[0].toObject().value("tags").toString().contains("dark"));
    for (const auto& r : results)
        EXPECT_NE(r.toObject().value("name").toString().toStdString(), "dark1.wav")
            << "the seed never appears in results";
    // Monotone ranking.
    EXPECT_GE(results[0].toObject().value("similarity").toDouble(),
              results[1].toObject().value("similarity").toDouble());

    // Text query seed: "dark" ranks the dark family first.
    auto qR = call("related_samples", {{"libraryIds", ids}, {"query", "dark"}, {"limit", 2}});
    ASSERT_FALSE(isError(qR)) << text(qR).toStdString();
    auto qObj = QJsonDocument::fromJson(text(qR).toUtf8()).object();
    EXPECT_FALSE(qObj.contains("seed")) << "a query has no file seed";
    auto qResults = qObj.value("results").toArray();
    ASSERT_EQ(qResults.size(), 2) << "limit is respected";
    EXPECT_TRUE(qResults[0].toObject().value("name").toString().startsWith("dark"));
    EXPECT_TRUE(qResults[1].toObject().value("name").toString().startsWith("dark"));

    auto removeR = call("remove_library", {{"id", libId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
}

TEST_F(McpCoverageTest, LibraryClusterRelatedErrorCases) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "solo.wav", "dark, low", "a dark low texture",
                                    150.0, 0.75, 0.05));

    auto addR = call("add_library", {{"name", "ErrLib"}, {"path", dir.path()}, {"type", "audio"}});
    ASSERT_FALSE(isError(addR)) << text(addR).toStdString();
    QString libId = QJsonDocument::fromJson(text(addR).toUtf8()).object().value("id").toString();
    ASSERT_FALSE(libId.isEmpty());
    auto scanR = call("scan_library", {{"id", libId}});
    EXPECT_FALSE(isError(scanR));
    for (int i = 0; i < 100 && engine->getFileLibraryManager().isScanning(); ++i)
        QThread::msleep(100);
    ASSERT_FALSE(engine->getFileLibraryManager().isScanning());

    QJsonArray ids{libId};

    // Unknown library id -> tool error listing the offender, never a silent skip.
    QJsonArray badIds{libId, "nosuchlib"};
    auto r1 = call("cluster_library", {{"libraryIds", badIds}, {"k", 2}});
    EXPECT_TRUE(isError(r1)) << "unknown libraryIds must be a tool error";
    EXPECT_TRUE(text(r1).contains("nosuchlib")) << "error must list the unknown id";

    auto r1b = call("related_samples", {{"libraryIds", badIds}, {"query", "dark"}});
    EXPECT_TRUE(isError(r1b)) << "unknown libraryIds must be a tool error (related_samples)";

    // Bad method -> tool error (whitelist comparison, no stoi).
    auto r2 = call("cluster_library", {{"libraryIds", ids}, {"k", 2}, {"method", "bogus"}});
    EXPECT_TRUE(isError(r2));
    EXPECT_TRUE(text(r2).contains("method")) << "error should name the bad method";

    // Exactly one of filePath/query is required.
    auto r3 = call("related_samples", {{"libraryIds", ids}});
    EXPECT_TRUE(isError(r3)) << "missing both filePath and query must be a tool error";
    auto r3b = call("related_samples", {{"libraryIds", ids},
                                        {"filePath", "C:/no/such/file.wav"},
                                        {"query", "dark"}});
    EXPECT_TRUE(isError(r3b)) << "providing both filePath and query must be a tool error";

    // filePath that exists in no selected library -> error, not an empty result.
    auto r4 = call("related_samples", {{"libraryIds", ids}, {"filePath", "C:/no/such/file.wav"}});
    EXPECT_TRUE(isError(r4));

    // Happy control: same call with valid params is NOT an error.
    auto okR = call("cluster_library", {{"libraryIds", ids}, {"k", 1}});
    EXPECT_FALSE(isError(okR)) << text(okR).toStdString();

    auto removeR = call("remove_library", {{"id", libId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
}

TEST_F(McpCoverageTest, LibraryClusterTwoLibraryScope) {
    QTemporaryDir dirA, dirB;
    ASSERT_TRUE(dirA.isValid());
    ASSERT_TRUE(dirB.isValid());
    // Lib A: two dark + one bright.
    ASSERT_TRUE(writeWavWithSidecar(dirA.path(), "darkA1.wav", "dark, low", "a dark low texture",
                                    150.0, 0.75, 0.05));
    ASSERT_TRUE(writeWavWithSidecar(dirA.path(), "darkA2.wav", "dark, low", "a dark low texture",
                                    160.0, 0.73, 0.06));
    ASSERT_TRUE(writeWavWithSidecar(dirA.path(), "brightA1.wav", "bright, high", "a bright high texture",
                                    5200.0, 0.05, 0.70));
    // Lib B: two dark (same family as A) + one unrelated metallic.
    ASSERT_TRUE(writeWavWithSidecar(dirB.path(), "darkB1.wav", "dark, low", "a dark low texture",
                                    140.0, 0.77, 0.04));
    ASSERT_TRUE(writeWavWithSidecar(dirB.path(), "darkB2.wav", "dark, low", "a dark low texture",
                                    155.0, 0.74, 0.06));
    ASSERT_TRUE(writeWavWithSidecar(dirB.path(), "metalB1.wav", "metallic, ringing", "a metallic ringing texture",
                                    9000.0, 0.01, 0.95));

    QString idA, idB;
    auto addA = call("add_library", {{"name", "ScopeA"}, {"path", dirA.path()}, {"type", "audio"}});
    ASSERT_FALSE(isError(addA)) << text(addA).toStdString();
    idA = QJsonDocument::fromJson(text(addA).toUtf8()).object().value("id").toString();
    auto addB = call("add_library", {{"name", "ScopeB"}, {"path", dirB.path()}, {"type", "audio"}});
    ASSERT_FALSE(isError(addB)) << text(addB).toStdString();
    idB = QJsonDocument::fromJson(text(addB).toUtf8()).object().value("id").toString();
    ASSERT_FALSE(idA.isEmpty());
    ASSERT_FALSE(idB.isEmpty());

    for (const QString& id : {idA, idB}) {
        auto scanR = call("scan_library", {{"id", id}});
        EXPECT_FALSE(isError(scanR));
    }
    for (int i = 0; i < 100 && engine->getFileLibraryManager().isScanning(); ++i)
        QThread::msleep(100);
    ASSERT_FALSE(engine->getFileLibraryManager().isScanning());

    QJsonArray scope{idA, idB};
    auto clusterR = call("cluster_library", {{"libraryIds", scope}, {"k", 3}});
    ASSERT_FALSE(isError(clusterR)) << text(clusterR).toStdString();
    auto obj = QJsonDocument::fromJson(text(clusterR).toUtf8()).object();
    EXPECT_EQ(obj.value("k").toInt(), 3);
    auto clusters = obj.value("clusters").toArray();

    // The dark family must form ONE cluster spanning BOTH libraries; the
    // unrelated metallic entry must NOT be in it.
    int totalMembers = 0;
    bool foundCrossLib = false;
    for (const auto& c : clusters) {
        auto members = c.toObject().value("members").toArray();
        totalMembers += members.size();
        QStringList names;
        for (const auto& m : members) names << m.toObject().value("name").toString();
        const QString joined = names.join(";");
        if (joined.contains("darkA") && joined.contains("darkB")) {
            foundCrossLib = true;
            EXPECT_FALSE(joined.contains("metalB1"))
                << "the unrelated metallic entry must be excluded from the dark cluster";
            EXPECT_EQ(names.size(), 4) << "both dark pairs in one cluster";
        }
    }
    EXPECT_TRUE(foundCrossLib) << "a cluster must span BOTH scoped libraries";
    EXPECT_EQ(totalMembers, 6) << "all entries from A+B participate";

    auto removeA = call("remove_library", {{"id", idA}});
    EXPECT_FALSE(isError(removeA));
    auto removeB = call("remove_library", {{"id", idB}});
    EXPECT_FALSE(isError(removeB));
}


// ── cluster presets (docs/plans/2026-08-25-cluster-presets.md, G3) ──────────
// cluster_library(saveAs) -> presetId; list/get/refresh/delete round-trip;
// single-cluster save keeps only that cluster's members.

namespace {

// Canonical cluster fingerprint for equality checks: fixed-precision
// similarities so QJsonDocument double formatting cannot cause false diffs.
QString clusterFingerprint(const QJsonValue& c) {
    auto co = c.toObject();
    QString s = co.value("id").toString() + "|" + co.value("label").toString()
              + "|" + QString::number(co.value("size").toInt()) + "|";
    const auto members = co.value("members").toArray();
    for (const auto& m : members) {
        auto mo = m.toObject();
        s += mo.value("name").toString() + "@"
           + QString::number(mo.value("similarity").toDouble(), 'f', 9) + ";";
    }
    return s;
}

QString snapshotFingerprint(const QJsonObject& o) {
    QString s = o.value("method").toString() + "|" + QString::number(o.value("k").toInt())
              + "|" + QString::number(o.value("entryCount").toInt()) + "|"
              + o.value("clusterId").toString() + "|";
    const auto clusters = o.value("clusters").toArray();
    for (const auto& c : clusters) s += clusterFingerprint(c);
    const auto unassigned = o.value("unassigned").toArray();
    s += "U:";
    for (const auto& u : unassigned) {
        auto uo = u.toObject();
        s += uo.value("name").toString() + "," + uo.value("path").toString() + ";";
    }
    return s;
}

} // namespace

TEST_F(McpCoverageTest, ClusterPresetSaveListGetRefreshDelete) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "dark1.wav", "dark, low", "a dark low texture",
                                    150.0, 0.75, 0.05));
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "dark2.wav", "dark, low", "a dark low texture",
                                    160.0, 0.73, 0.06));
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "bright1.wav", "bright, high", "a bright high texture",
                                    5200.0, 0.05, 0.70));
    ASSERT_TRUE(writeWavWithSidecar(dir.path(), "bright2.wav", "bright, high", "a bright high texture",
                                    5400.0, 0.04, 0.72));

    auto addR = call("add_library", {{"name", "PresetMcpLib"}, {"path", dir.path()}, {"type", "audio"}});
    ASSERT_FALSE(isError(addR)) << text(addR).toStdString();
    QString libId = QJsonDocument::fromJson(text(addR).toUtf8()).object().value("id").toString();
    ASSERT_FALSE(libId.isEmpty());
    auto scanR = call("scan_library", {{"id", libId}});
    EXPECT_FALSE(isError(scanR));
    for (int i = 0; i < 100 && engine->getFileLibraryManager().isScanning(); ++i)
        QThread::msleep(100);
    ASSERT_FALSE(engine->getFileLibraryManager().isScanning());

    QJsonArray ids{libId};
    QString presetId;

    // 1. cluster_library with saveAs -> presetId in the response.
    auto saveR = call("cluster_library", {{"libraryIds", ids}, {"k", 2}, {"saveAs", "Mcp Preset"}});
    ASSERT_FALSE(isError(saveR)) << text(saveR).toStdString();
    auto saveObj = QJsonDocument::fromJson(text(saveR).toUtf8()).object();
    EXPECT_EQ(saveObj.value("k").toInt(), 2);
    presetId = saveObj.value("presetId").toString();
    ASSERT_FALSE(presetId.isEmpty()) << "saveAs must produce a presetId";
    EXPECT_TRUE(presetId.startsWith("cp_"));

    // 2. list_cluster_presets shows it with the recipe + counts.
    auto listR = call("list_cluster_presets");
    ASSERT_FALSE(isError(listR)) << text(listR).toStdString();
    auto listArr = QJsonDocument::fromJson(text(listR).toUtf8()).object().value("presets").toArray();
    bool found = false;
    for (const auto& p : listArr) {
        auto po = p.toObject();
        if (po.value("id").toString() != presetId) continue;
        found = true;
        EXPECT_EQ(po.value("name").toString().toStdString(), "Mcp Preset");
        EXPECT_EQ(po.value("libraryIds").toArray().size(), 1);
        EXPECT_EQ(po.value("libraryIds").toArray()[0].toString(), libId);
        EXPECT_EQ(po.value("method").toString().toStdString(), "hybrid");
        EXPECT_EQ(po.value("k").toInt(), 2);
        EXPECT_TRUE(po.value("clusterId").isNull()) << "whole-result save has null clusterId";
        EXPECT_EQ(po.value("clusterCount").toInt(), 2);
        EXPECT_EQ(po.value("entryCount").toInt(), 4);
    }
    EXPECT_TRUE(found) << "list_cluster_presets must contain the saved preset";

    // 3. get_cluster_preset (refresh=false) returns the snapshot + staleness probe.
    auto getR = call("get_cluster_preset", {{"id", presetId}});
    ASSERT_FALSE(isError(getR)) << text(getR).toStdString();
    auto getObj = QJsonDocument::fromJson(text(getR).toUtf8()).object();
    EXPECT_EQ(getObj.value("name").toString().toStdString(), "Mcp Preset");
    EXPECT_EQ(getObj.value("k").toInt(), 2);
    EXPECT_EQ(getObj.value("entryCount").toInt(), 4);
    EXPECT_EQ(getObj.value("missingMemberCount").toInt(), 0)
        << "fixture files still exist -> no missing members";
    int totalMembers = 0;
    for (const auto& c : getObj.value("clusters").toArray()) {
        auto co = c.toObject();
        EXPECT_TRUE(co.value("id").toString().startsWith("c"));
        totalMembers += co.value("members").toArray().size();
        for (const auto& m : co.value("members").toArray())
            EXPECT_TRUE(m.toObject().contains("tags"));
    }
    EXPECT_EQ(totalMembers, 4);
    EXPECT_TRUE(getObj.value("unassigned").toArray().isEmpty());

    // 4. get_cluster_preset refresh=true recomputes equal when the library is
    // unchanged (determinism bridge); adds computedAt.
    auto refreshR = call("get_cluster_preset", {{"id", presetId}, {"refresh", true}});
    ASSERT_FALSE(isError(refreshR)) << text(refreshR).toStdString();
    auto freshObj = QJsonDocument::fromJson(text(refreshR).toUtf8()).object();
    EXPECT_FALSE(freshObj.value("computedAt").toString().isEmpty());
    EXPECT_EQ(snapshotFingerprint(freshObj), snapshotFingerprint(getObj))
        << "refresh=true must equal the stored snapshot when the library is unchanged";

    // 5. delete removes it; get after delete is a tool error; list is empty of it.
    auto delR = call("delete_cluster_preset", {{"id", presetId}});
    ASSERT_FALSE(isError(delR)) << text(delR).toStdString();
    EXPECT_TRUE(QJsonDocument::fromJson(text(delR).toUtf8()).object().value("deleted").toBool());
    auto delGetR = call("get_cluster_preset", {{"id", presetId}});
    EXPECT_TRUE(isError(delGetR)) << "get after delete must be a tool error";
    auto listAfter = QJsonDocument::fromJson(
        text(call("list_cluster_presets")).toUtf8()).object().value("presets").toArray();
    for (const auto& p : listAfter)
        EXPECT_NE(p.toObject().value("id").toString(), presetId);

    // 6. Unknown id -> tool error (get/refresh/delete all error paths).
    auto unknownR = call("get_cluster_preset", {{"id", "cp_00000000"}});
    EXPECT_TRUE(isError(unknownR));
    EXPECT_TRUE(text(unknownR).contains("cp_00000000"));
    auto unknownRefresh = call("get_cluster_preset", {{"id", "cp_00000000"}, {"refresh", true}});
    EXPECT_TRUE(isError(unknownRefresh));
    auto unknownDel = call("delete_cluster_preset", {{"id", "cp_00000000"}});
    EXPECT_TRUE(isError(unknownDel));

    // 7. saveAs on a failed cluster (bogus clusterId) -> tool error, no preset.
    auto badSave = call("cluster_library", {{"libraryIds", ids}, {"k", 2},
                                            {"saveAs", "Bad Save"}, {"clusterId", "c99"}});
    EXPECT_TRUE(isError(badSave));
    EXPECT_TRUE(text(badSave).contains("c99"));

    // 8. Single-cluster save: clusterId narrows the stored snapshot.
    auto firstClusterId = saveObj.value("clusters").toArray()[0].toObject().value("id").toString();
    ASSERT_FALSE(firstClusterId.isEmpty());
    auto singleR = call("cluster_library", {{"libraryIds", ids}, {"k", 2},
                                            {"saveAs", "Single Scope"}, {"clusterId", firstClusterId}});
    ASSERT_FALSE(isError(singleR)) << text(singleR).toStdString();
    auto singleObj = QJsonDocument::fromJson(text(singleR).toUtf8()).object();
    QString singleId = singleObj.value("presetId").toString();
    ASSERT_FALSE(singleId.isEmpty());
    // The LIVE response still carries all clusters — narrowing only affects the save.
    EXPECT_EQ(singleObj.value("clusters").toArray().size(), 2);

    auto singleGet = call("get_cluster_preset", {{"id", singleId}});
    ASSERT_FALSE(isError(singleGet)) << text(singleGet).toStdString();
    auto sObj = QJsonDocument::fromJson(text(singleGet).toUtf8()).object();
    EXPECT_EQ(sObj.value("clusterId").toString(), firstClusterId);
    auto sClusters = sObj.value("clusters").toArray();
    ASSERT_EQ(sClusters.size(), 1) << "only the saved cluster is stored";
    EXPECT_EQ(sClusters[0].toObject().value("id").toString(), firstClusterId);
    EXPECT_EQ(sClusters[0].toObject().value("members").toArray().size(), 2)
        << "single-cluster save stores only that cluster's members";
    EXPECT_TRUE(sObj.value("unassigned").isNull()) << "unassigned omitted when clusterId set";
    EXPECT_EQ(sObj.value("entryCount").toInt(), 2);

    // Refresh of the single-cluster preset stays narrow.
    auto singleRefresh = call("get_cluster_preset", {{"id", singleId}, {"refresh", true}});
    ASSERT_FALSE(isError(singleRefresh)) << text(singleRefresh).toStdString();
    auto srObj = QJsonDocument::fromJson(text(singleRefresh).toUtf8()).object();
    EXPECT_EQ(srObj.value("clusters").toArray().size(), 1);
    EXPECT_EQ(srObj.value("entryCount").toInt(), 2);

    // 9. Clean up presets + library (appdata registry must not be polluted).
    call("delete_cluster_preset", {{"id", singleId}});
    auto removeR = call("remove_library", {{"id", libId}});
    EXPECT_FALSE(isError(removeR)) << text(removeR).toStdString();
}
// ============================================================================
// REMAINING TOOLS
// ============================================================================

TEST_F(McpCoverageTest, SetAutomationPoints) {
    // Add automation lane with unique name (track 0 already has built-in "Volume")
    auto laneR = call("add_automation_lane", {{"trackId", 0}, {"laneName", "Test Volume"}, {"paramID", 100}});
    EXPECT_FALSE(isError(laneR)) << text(laneR).toStdString();

    // Set points (replace mode)
    QJsonArray points;
    points.append(QJsonObject{{"time", 0.0}, {"value", 0.8}});
    points.append(QJsonObject{{"time", 4.0}, {"value", 0.5}});
    auto setR = call("set_automation_points", {
        {"trackId", 0}, {"lane", "Test Volume"}, {"points", points}
    });
    EXPECT_FALSE(isError(setR)) << text(setR).toStdString();

    // Append mode
    QJsonArray morePoints;
    morePoints.append(QJsonObject{{"time", 8.0}, {"value", 0.3}});
    auto appendR = call("set_automation_points", {
        {"trackId", 0}, {"lane", "Test Volume"}, {"points", morePoints}, {"mode", "append"}
    });
    EXPECT_FALSE(isError(appendR)) << text(appendR).toStdString();
}

TEST_F(McpCoverageTest, GenerateArrangement) {
    auto r = call("generate_arrangement", {{"bars", 8}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    // Should have created tracks beyond the default 3
    EXPECT_GT(trackCount(), 3);

    // Response should be JSON with seed, noteCount, and parts array
    auto doc = QJsonDocument::fromJson(text(r).toUtf8());
    auto obj = doc.object();
    EXPECT_TRUE(obj.contains("seed"));
    EXPECT_TRUE(obj.contains("noteCount"));
    EXPECT_TRUE(obj.contains("parts"));
    auto parts = obj["parts"].toArray();
    EXPECT_GT(parts.size(), 0);
    for (const auto& p : parts) {
        auto part = p.toObject();
        EXPECT_TRUE(part.contains("role"));
        EXPECT_TRUE(part.contains("trackIndex"));
        EXPECT_TRUE(part.contains("clipId"));
        EXPECT_FALSE(part["role"].toString().isEmpty());
        EXPECT_GE(part["trackIndex"].toInt(), 0);
        EXPECT_GT(part["clipId"].toInt(), 0);
    }
}

TEST_F(McpCoverageTest, ProjectInfo) {
    auto r = call("project_info");
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
}

TEST_F(McpCoverageTest, AddNotesBulk) {
    int clipId = addMidiClip(0, 0.0, 4.0);
    ASSERT_GT(clipId, 0);

    QJsonArray notes;
    notes.append(QJsonObject{{"pitch", 60}, {"start", 0.0}, {"duration", 0.5}, {"velocity", 100}});
    notes.append(QJsonObject{{"pitch", 64}, {"start", 0.5}, {"duration", 0.5}, {"velocity", 90}});
    notes.append(QJsonObject{{"pitch", 67}, {"start", 1.0}, {"duration", 0.5}, {"velocity", 80}});

    auto r = call("add_notes", {{"clipId", clipId}, {"notes", notes}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto result = QJsonDocument::fromJson(text(r).toUtf8()).object();
    EXPECT_EQ(result.value("added").toInt(), 3);

    auto getNotesResult = getNotes(clipId);
    EXPECT_EQ(getNotesResult.size(), 3);
}

TEST_F(McpCoverageTest, AddNotesAbsoluteBeatsConvertToClipLocal) {
    // Clip on track 0 spanning timeline beats [32, 40): add_midi_clip stores
    // start in SECONDS (32 beats @ 120 BPM = 16 s); add_notes ABSOLUTE mode must
    // convert back with the project tempo at call time (clipStartBeats = 32).
    int clipId = addMidiClip(0, 32.0, 8.0);
    ASSERT_GT(clipId, 0);

    // (b) absolute start before the clip's own start -> whole batch rejected,
    // nothing added.
    QJsonArray badNotes;
    badNotes.append(QJsonObject{{"pitch", 60}, {"start", 8.0}, {"duration", 0.5}});
    auto bad = call("add_notes", {{"clipId", clipId}, {"notes", badNotes}, {"relative", false}});
    EXPECT_TRUE(isError(bad)) << text(bad).toStdString();
    EXPECT_TRUE(text(bad).contains("absolute start 8 < clip start 32"))
        << text(bad).toStdString();
    {
        auto countR = call("list_notes", {{"clipId", clipId}});
        EXPECT_FALSE(isError(countR)) << text(countR).toStdString();
        auto notes = QJsonDocument::fromJson(text(countR).toUtf8()).object();
        EXPECT_EQ(notes.value("count").toInt(), 0);
        EXPECT_EQ(notes.value("notes").toArray().size(), 0);
    }

    // (a) absolute starts >= clip start get converted to clip-local beats:
    // 96 -> 64, 32 -> 0.
    QJsonArray notes;
    notes.append(QJsonObject{{"pitch", 60}, {"start", 96.0}, {"duration", 0.5}, {"velocity", 100}});
    notes.append(QJsonObject{{"pitch", 64}, {"start", 32.0}, {"duration", 0.5}, {"velocity", 90}});
    auto r = call("add_notes", {{"clipId", clipId}, {"notes", notes}, {"relative", false}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto result = QJsonDocument::fromJson(text(r).toUtf8()).object();
    EXPECT_EQ(result.value("added").toInt(), 2);
    EXPECT_EQ(result.value("noteIds").toArray().size(), 2);

    auto listR = call("list_notes", {{"clipId", clipId}});
    EXPECT_FALSE(isError(listR)) << text(listR).toStdString();
    auto listed = QJsonDocument::fromJson(text(listR).toUtf8()).object();
    EXPECT_EQ(listed.value("count").toInt(), 2);
    auto arr = listed.value("notes").toArray();
    EXPECT_NEAR(arr[0].toObject().value("start").toDouble(), 64.0, 0.0001);
    EXPECT_NEAR(arr[1].toObject().value("start").toDouble(), 0.0, 0.0001);
    EXPECT_EQ(arr[0].toObject().value("pitch").toInt(), 60);
    EXPECT_EQ(arr[1].toObject().value("pitch").toInt(), 64);
}

TEST_F(McpCoverageTest, AddNotesRelativeDefaultUnchanged) {
    // Regression guard: with relative omitted (default true), starts are written
    // verbatim as clip-local beats regardless of the clip's own start.
    int clipId = addMidiClip(0, 32.0, 8.0);
    ASSERT_GT(clipId, 0);

    QJsonArray notes;
    notes.append(QJsonObject{{"pitch", 67}, {"start", 2.0}, {"duration", 0.5}});
    auto r = call("add_notes", {{"clipId", clipId}, {"notes", notes}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto listR = call("list_notes", {{"clipId", clipId}});
    EXPECT_FALSE(isError(listR)) << text(listR).toStdString();
    auto listed = QJsonDocument::fromJson(text(listR).toUtf8()).object();
    EXPECT_EQ(listed.value("count").toInt(), 1);
    auto arr = listed.value("notes").toArray();
    EXPECT_NEAR(arr[0].toObject().value("start").toDouble(), 2.0, 0.0001);

    // Explicit relative:true behaves identically.
    QJsonArray more;
    more.append(QJsonObject{{"pitch", 69}, {"start", 3.0}, {"duration", 0.5}});
    auto r2 = call("add_notes", {{"clipId", clipId}, {"notes", more}, {"relative", true}});
    EXPECT_FALSE(isError(r2)) << text(r2).toStdString();
    auto listR2 = call("list_notes", {{"clipId", clipId}});
    EXPECT_FALSE(isError(listR2)) << text(listR2).toStdString();
    auto listed2 = QJsonDocument::fromJson(text(listR2).toUtf8()).object();
    EXPECT_EQ(listed2.value("count").toInt(), 2);
    auto arr2 = listed2.value("notes").toArray();
    EXPECT_NEAR(arr2[1].toObject().value("start").toDouble(), 3.0, 0.0001);
}

TEST_F(McpCoverageTest, AddNotesDescriptionDocumentsAbsoluteMode) {
    auto tools = toolList();
    QString desc;
    for (const auto& t : tools) {
        if (t.toObject().value("name").toString() == "add_notes") {
            desc = t.toObject().value("description").toString();
            break;
        }
    }
    ASSERT_FALSE(desc.isEmpty()) << "add_notes tool not found in tools/list";
    EXPECT_TRUE(desc.contains("ABSOLUTE")) << desc.toStdString();
    EXPECT_TRUE(desc.contains("relative")) << desc.toStdString();
}

TEST_F(McpCoverageTest, GenerateRhythmPatternLongBars) {
    // 32 bars should work (was capped at 16 before)
    auto r = call("generate_rhythm_pattern", {
        {"trackId", 0}, {"bars", 32}, {"grid", 16},
        {"pulseA", 4}, {"pulseB", 3}
    });
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    // Verify the clip was created with the right duration
    QString txt = text(r);
    EXPECT_TRUE(txt.contains("clipId="));
    EXPECT_TRUE(txt.contains("notes="));
}

} // namespace
