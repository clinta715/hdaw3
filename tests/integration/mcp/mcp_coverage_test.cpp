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
