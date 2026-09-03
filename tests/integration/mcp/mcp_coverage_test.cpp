#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/Track.h"
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
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

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

    int parseClipId(const QString& resp) {
        QString s = resp.trimmed();
        if (s.startsWith('{')) {
            auto doc = QJsonDocument::fromJson(s.toUtf8());
            if (doc.isObject() && doc.object().contains("clipId"))
                return doc.object().value("clipId").toInt();
        }
        int idx = s.indexOf("clipId");
        if (idx >= 0) {
            int eq = s.indexOf('=', idx);
            int colon = s.indexOf(':', idx);
            int pos = -1;
            if (eq >= 0 && colon >= 0) pos = std::min(eq, colon);
            else if (eq >= 0) pos = eq;
            else if (colon >= 0) pos = colon;
            if (pos >= 0) {
                int start = pos + 1;
                while (start < s.size() && !s[start].isDigit() && s[start] != '-') ++start;
                int end = start;
                while (end < s.size() && s[end].isDigit()) ++end;
                if (end > start) return s.mid(start, end - start).toInt();
            }
        }
        for (int i = 0; i < s.size(); ++i) if (s[i].isDigit()) { int j=i; while(j < s.size() && s[j].isDigit()) ++j; return s.mid(i, j-i).toInt(); }
        return s.mid(s.indexOf('=') + 1).toInt();
    }

    int addMidiClip(int trackId, double start, double length, const QString& name = {}) {
        QJsonObject args{{"trackId", trackId}, {"start", start}, {"length", length}};
        if (!name.isEmpty()) args["name"] = name;
        auto r = call("add_midi_clip", args);
        if (isError(r)) return -1;
        QString resp = text(r);
        return parseClipId(resp);
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

TEST_F(McpCoverageTest, AddTrackReturnsJson) {
    // P3-2: add_track must return compact JSON (was plain "trackId=N routed=1").
    int before = trackCount();
    auto r = call("add_track", {{"name", "JsonTrack"}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto result = QJsonDocument::fromJson(text(r).toUtf8()).object();
    EXPECT_FALSE(result.isEmpty()) << text(r).toStdString();
    int trackId = result.value("trackId").toInt();
    EXPECT_GT(trackId, 0) << text(r).toStdString();
    EXPECT_EQ(result.value("routed").toInt(), 1);
    EXPECT_EQ(trackCount(), before + 1);

    // The returned id really exists in list_tracks with the requested name.
    auto t = findTrack(trackId);
    EXPECT_FALSE(t.isEmpty());
    EXPECT_EQ(t.value("name").toString().toStdString(), "JsonTrack");
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

    // P3-3: every hit carries the library it belongs to, within the requested
    // scope.
    QJsonArray requestedIds{libId};
    for (const auto& r : results) {
        auto hit = r.toObject();
        EXPECT_FALSE(hit.value("libraryId").toString().isEmpty())
            << "related_samples hit missing libraryId";
        EXPECT_TRUE(requestedIds.contains(QJsonValue(hit.value("libraryId").toString())))
            << "hit libraryId outside the requested scope";
    }

    // Text query seed: "dark" ranks the dark family first.
    auto qR = call("related_samples", {{"libraryIds", ids}, {"query", "dark"}, {"limit", 2}});
    ASSERT_FALSE(isError(qR)) << text(qR).toStdString();
    auto qObj = QJsonDocument::fromJson(text(qR).toUtf8()).object();
    EXPECT_FALSE(qObj.contains("seed")) << "a query has no file seed";
    auto qResults = qObj.value("results").toArray();
    ASSERT_EQ(qResults.size(), 2) << "limit is respected";
    EXPECT_TRUE(qResults[0].toObject().value("name").toString().startsWith("dark"));
    EXPECT_TRUE(qResults[1].toObject().value("name").toString().startsWith("dark"));

    // P3-3: query-seeded hits also carry libraryId within the requested scope.
    for (const auto& r : qResults) {
        auto hit = r.toObject();
        EXPECT_FALSE(hit.value("libraryId").toString().isEmpty())
            << "query hit missing libraryId";
        EXPECT_TRUE(requestedIds.contains(QJsonValue(hit.value("libraryId").toString())))
            << "query hit libraryId outside the requested scope";
    }

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


// P2-3 automation preset bank: tool -> applyAutomationPreset command -> lane
// ValueTree -> read path. Windows are beats at the boundary, seconds in the
// tree, values normalized 0..1; the lane must exist (add_automation_lane)
// and is enabled by the apply so renders honor the points.
TEST_F(McpCoverageTest, AutomationPresetWindows) {
    // Lane must exist first — dedicated lane on track 0 (paramID 200 is free;
    // the built-ins are Volume/Pan/Mute).
    auto createR = call("add_automation_lane", {{"trackId", 0}, {"laneName", "CutPump"}, {"paramID", 200}});
    EXPECT_FALSE(isError(createR)) << text(createR).toStdString();

    // Two section windows: pump over beats [0,16), macro over [16,32) with
    // explicit 0.2 -> 0.6 values (per-window overrides win).
    QJsonArray sections;
    sections.append(QJsonObject{{"start", 0.0}, {"end", 16.0}, {"preset", "pump"}});
    sections.append(QJsonObject{{"start", 16.0}, {"end", 32.0}, {"preset", "macro"},
                                {"startValue", 0.2}, {"endValue", 0.6}});
    auto r = call("automation_preset",
                  {{"trackId", 0}, {"lane", "CutPump"}, {"sections", sections}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();
    auto res = QJsonDocument::fromJson(text(r).toUtf8()).object();
    EXPECT_EQ(res.value("lane").toString(), QString("CutPump"));
    EXPECT_EQ(res.value("presets").toArray().size(), 2);
    EXPECT_EQ(res.value("presets").toArray()[0].toString(), QString("pump"));
    EXPECT_EQ(res.value("presets").toArray()[1].toString(), QString("macro"));
    const int pointsAdded = res.value("pointsAdded").toInt(0);
    // 2 windows x 16 beats x 4 points/beat = 128 (generator emits exactly one
    // point per grid step; endTime lands on the grid).
    EXPECT_GE(pointsAdded, 100);
    EXPECT_LE(pointsAdded, 200);

    // Lane lists the points and ended up enabled by the apply.
    auto lanes = QJsonDocument::fromJson(
        callText("list_automation_lanes", {{"trackId", 0}}).toString().toUtf8()).array();
    bool found = false;
    for (const auto& lv : lanes)
    {
        auto lane = lv.toObject();
        if (lane.value("name").toString() == "CutPump")
        {
            found = true;
            EXPECT_GT(lane.value("pointCount").toInt(), 0);
            EXPECT_TRUE(lane.value("enabled").toBool());
        }
    }
    EXPECT_TRUE(found);

    // Read points back through the read path (the same getAutomationPoints the
    // frontend router serves): beats domain, normalized values.
    auto pts = engine->getReadModel().getAutomationPoints(0, "CutPump");
    ASSERT_GE(pts.size(), 100u);
    const auto nearestAt = [&](double beat) {
        double bestT = -1e9;
        double bestV = 0.0;
        for (const auto& p : pts)
        {
            if (std::fabs(p.time - beat) < std::fabs(bestT - beat))
            {
                bestT = p.time;
                bestV = p.value;
            }
        }
        return bestV;
    };

    // Pump region bounces inside [0.70, 1.00] (values at beats 2 and 8 land on
    // the band) and actually bounces: min near the trough, max near the peak.
    const double v2 = nearestAt(2.0);
    const double v8 = nearestAt(8.0);
    EXPECT_GE(v2, 0.70 - 1e-3);
    EXPECT_LE(v2, 1.00 + 1e-3);
    EXPECT_GE(v8, 0.70 - 1e-3);
    EXPECT_LE(v8, 1.00 + 1e-3);
    float minV = 2.0f, maxV = 0.0f;
    for (const auto& p : pts)
    {
        if (p.time >= 0.0 && p.time < 16.0)
        {
            minV = std::min(minV, p.value);
            maxV = std::max(maxV, p.value);
        }
    }
    EXPECT_LT(minV, 0.75f);
    EXPECT_GT(maxV, 0.95f);

    // Macro region mid-window (beat 24) sits at the midpoint of the 0.2->0.6
    // ramp: value ≈ 0.4.
    EXPECT_NEAR(nearestAt(24.0), 0.4, 0.05);

    // Error cases (Gate 9: actionable messages, atomic no-ops).
    auto badPreset = call("automation_preset",
        {{"trackId", 0}, {"lane", "CutPump"}, {"preset", "bogus"}, {"start", 0}, {"end", 4}});
    EXPECT_TRUE(isError(badPreset));
    EXPECT_TRUE(text(badPreset).contains("unknown preset"));

    auto badLane = call("automation_preset",
        {{"trackId", 0}, {"lane", "NoSuchLane"}, {"preset", "pump"}, {"start", 0}, {"end", 4}});
    EXPECT_TRUE(isError(badLane));
    EXPECT_TRUE(text(badLane).contains("add_automation_lane"));

    auto badWindow = call("automation_preset",
        {{"trackId", 0}, {"lane", "CutPump"}, {"preset", "pump"}, {"start", 8}, {"end", 4}});
    EXPECT_TRUE(isError(badWindow));
    EXPECT_TRUE(text(badWindow).contains("bad window"));
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

TEST_F(McpCoverageTest, AddNotesDeduplicatesBatchInternal) {
    // P3-1: batch-internal exact-dup guard. Identical (pitch, start, duration)
    // triples keep only the FIRST occurrence; velocity differences are NOT a
    // dedupe key. Duplicates against existing clip notes are NOT deduped.
    int clipId = addMidiClip(0, 0.0, 8.0);
    ASSERT_GT(clipId, 0);

    QJsonArray notes;
    // 3 exact duplicates of (60, 0.0, 0.5) — different velocities on purpose.
    notes.append(QJsonObject{{"pitch", 60}, {"start", 0.0}, {"duration", 0.5}, {"velocity", 100}});
    notes.append(QJsonObject{{"pitch", 60}, {"start", 0.0}, {"duration", 0.5}, {"velocity", 90}});
    notes.append(QJsonObject{{"pitch", 60}, {"start", 0.0}, {"duration", 0.5}, {"velocity", 120}});
    // 2 unique notes.
    notes.append(QJsonObject{{"pitch", 64}, {"start", 0.5}, {"duration", 0.5}});
    notes.append(QJsonObject{{"pitch", 67}, {"start", 1.0}, {"duration", 0.5}});

    auto r = call("add_notes", {{"clipId", clipId}, {"notes", notes}});
    EXPECT_FALSE(isError(r)) << text(r).toStdString();

    auto result = QJsonDocument::fromJson(text(r).toUtf8()).object();
    EXPECT_EQ(result.value("added").toInt(), 3);
    EXPECT_EQ(result.value("duplicatesSkipped").toInt(), 2);

    // list_notes sees exactly the 3 kept notes (5 - 2 skipped).
    auto listR = call("list_notes", {{"clipId", clipId}});
    EXPECT_FALSE(isError(listR)) << text(listR).toStdString();
    auto listed = QJsonDocument::fromJson(text(listR).toUtf8()).object();
    EXPECT_EQ(listed.value("count").toInt(), 3);
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


// ============================================================================
// MODULATION (LFO) TOOLS - docs/plans/2026-08-29-jungle-dnb-feature-gaps.md P1-1
// ============================================================================

// add_lfo / set_lfo_param / list_lfos / remove_lfo end-to-end through the MCP
// surface: defaults, per-param reflection, error rejection on bad param name
// and out-of-range indices (the commands no-op silently - Gate 9 says the MCP
// layer must reject), and remove-to-empty lifecycle.
TEST_F(McpCoverageTest, AddLfoLifecycle) {
    // The four tools are registered with descriptions (G1).
    QStringList toolNames;
    for (const auto& t : toolList())
        toolNames << t.toObject().value("name").toString();
    EXPECT_TRUE(toolNames.contains("add_lfo"));
    EXPECT_TRUE(toolNames.contains("set_lfo_param"));
    EXPECT_TRUE(toolNames.contains("list_lfos"));
    EXPECT_TRUE(toolNames.contains("remove_lfo"));

    // add_lfo -> returns {lfoIndex} (0-based list size BEFORE the append).
    auto addR = call("add_lfo", {{"trackId", 0}});
    EXPECT_FALSE(isError(addR)) << text(addR).toStdString();
    auto addObj = QJsonDocument::fromJson(text(addR).toUtf8()).object();
    EXPECT_EQ(addObj.value("lfoIndex").toInt(), 0);

    // list_lfos shows the engine defaults.
    auto list1 = QJsonDocument::fromJson(
        callText("list_lfos", {{"trackId", 0}}).toString().toUtf8()).array();
    ASSERT_EQ(list1.size(), 1);
    auto lfo = list1[0].toObject();
    EXPECT_EQ(lfo.value("index").toInt(), 0);
    EXPECT_EQ(lfo.value("name").toString().toStdString(), "LFO 1");
    EXPECT_EQ(lfo.value("waveform").toInt(), 0);
    EXPECT_NEAR(lfo.value("rate").toDouble(), 1.0, 1e-9);
    EXPECT_TRUE(lfo.value("rateSync").toBool());
    EXPECT_NEAR(lfo.value("depth").toDouble(), 0.3, 1e-9);
    EXPECT_FALSE(lfo.value("bipolar").toBool());
    EXPECT_NEAR(lfo.value("phaseOffset").toDouble(), 0.0, 1e-9);
    EXPECT_EQ(lfo.value("targetParamID").toInt(), 1);
    EXPECT_TRUE(lfo.value("enabled").toBool());

    // A second add_lfo appends at index 1 (defaults, name "LFO 2").
    auto addR2 = call("add_lfo", {{"trackId", 0}});
    EXPECT_FALSE(isError(addR2)) << text(addR2).toStdString();
    auto addObj2 = QJsonDocument::fromJson(text(addR2).toUtf8()).object();
    EXPECT_EQ(addObj2.value("lfoIndex").toInt(), 1);

    // set_lfo_param: every supported param is reflected by list_lfos.
    EXPECT_FALSE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 1}, {"param", "waveform"}, {"value", 2.0}})));
    EXPECT_FALSE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 1}, {"param", "rate"}, {"value", 4.0}})));
    EXPECT_FALSE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 1}, {"param", "rateSync"}, {"value", 0.0}})));
    EXPECT_FALSE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 1}, {"param", "depth"}, {"value", 0.75}})));
    EXPECT_FALSE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 1}, {"param", "bipolar"}, {"value", 1.0}})));
    EXPECT_FALSE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 1}, {"param", "phaseOffset"}, {"value", 90.0}})));
    EXPECT_FALSE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 1}, {"param", "targetParamID"}, {"value", 3.0}})));
    EXPECT_FALSE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 1}, {"param", "enabled"}, {"value", 0.0}})));

    auto list2 = QJsonDocument::fromJson(
        callText("list_lfos", {{"trackId", 0}}).toString().toUtf8()).array();
    ASSERT_EQ(list2.size(), 2);
    auto lfo1 = list2[1].toObject();
    EXPECT_EQ(lfo1.value("waveform").toInt(), 2);
    EXPECT_NEAR(lfo1.value("rate").toDouble(), 4.0, 1e-9);
    EXPECT_FALSE(lfo1.value("rateSync").toBool());
    EXPECT_NEAR(lfo1.value("depth").toDouble(), 0.75, 1e-9);
    EXPECT_TRUE(lfo1.value("bipolar").toBool());
    EXPECT_NEAR(lfo1.value("phaseOffset").toDouble(), 90.0, 1e-9);
    EXPECT_EQ(lfo1.value("targetParamID").toInt(), 3);
    EXPECT_FALSE(lfo1.value("enabled").toBool());
    // The untouched first LFO still carries defaults.
    auto lfo0 = list2[0].toObject();
    EXPECT_EQ(lfo0.value("waveform").toInt(), 0);
    EXPECT_EQ(lfo0.value("targetParamID").toInt(), 1);

    // Gate 9: unknown param and out-of-range indices are errors, never no-ops.
    EXPECT_TRUE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 0}, {"param", "bogus"}, {"value", 1.0}})));
    EXPECT_TRUE(isError(call("set_lfo_param", {{"trackId", 99}, {"lfoIndex", 0}, {"param", "rate"}, {"value", 2.0}})));
    EXPECT_TRUE(isError(call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 9}, {"param", "rate"}, {"value", 2.0}})));
    EXPECT_TRUE(isError(call("remove_lfo", {{"trackId", 0}, {"lfoIndex", 9}})));
    EXPECT_TRUE(isError(call("remove_lfo", {{"trackId", 99}, {"lfoIndex", 0}})));
    EXPECT_TRUE(isError(call("add_lfo", {{"trackId", 99}})));
    EXPECT_TRUE(isError(call("list_lfos", {{"trackId", 99}})));

    // remove_lfo -> list empty.
    EXPECT_FALSE(isError(call("remove_lfo", {{"trackId", 0}, {"lfoIndex", 0}})));
    EXPECT_FALSE(isError(call("remove_lfo", {{"trackId", 0}, {"lfoIndex", 0}})));
    auto list3 = QJsonDocument::fromJson(
        callText("list_lfos", {{"trackId", 0}}).toString().toUtf8()).array();
    EXPECT_EQ(list3.size(), 0);
}

// Gate 1/10: LFOs created via MCP must survive a full routing rebuild on the
// LIVE processor (ReadModel-only is NOT sufficient - RoutingManager::addTrack
// restores modulation from MODULATION_LIST on every rebuild, Track.cpp:248).
// Mirrors the envelope_generation/audio_pool_dedup rebuild pattern:
// rebuildRoutingGraph + drainPendingRoutingRebuild.
TEST_F(McpCoverageTest, AddLfoSurvivesRoutingRebuild) {
    auto addR = call("add_lfo", {{"trackId", 0}});
    ASSERT_FALSE(isError(addR)) << text(addR).toStdString();
    auto setR = call("set_lfo_param", {{"trackId", 0}, {"lfoIndex", 0}, {"param", "targetParamID"}, {"value", 3.0}});
    ASSERT_FALSE(isError(setR)) << text(setR).toStdString();

    // Settle any coalesced async routing work, then force a full rebuild.
    engine->drainPendingRoutingRebuild();
    engine->getMainProcessor()->rebuildRoutingGraph();
    engine->drainPendingRoutingRebuild();

    // LIVE processor assertion: the rebuilt Track still owns the LFO source
    // with its targetParamID.
    auto* track = engine->getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    EXPECT_EQ(track->getNumModulations(), 1);
    EXPECT_EQ(track->getModulationSourceParamID(0), 3);

    // ReadModel agrees (parity check, not the primary Gate 1/10 evidence).
    auto list = QJsonDocument::fromJson(
        callText("list_lfos", {{"trackId", 0}}).toString().toUtf8()).array();
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list[0].toObject().value("targetParamID").toInt(), 3);
    EXPECT_EQ(list[0].toObject().value("waveform").toInt(), 0);
}

// P1-2 (2026-08-29): internal "filter" FX — add_fx -> list_fx_params in REAL
// units -> set_internal_fx_param; the slot survives a full routing rebuild
// with its params restored on the LIVE processor (Gate 1/10, MCP surface).
TEST_F(McpCoverageTest, FilterFxParamsRealUnitsAndRebuildSurvival) {
    auto addR = call("add_fx", {{"trackId", 0}, {"fxType", "filter"}});
    ASSERT_FALSE(isError(addR)) << text(addR).toStdString();

    // list_fx_params: 3 params in real units.
    auto listObj = QJsonDocument::fromJson(
        callText("list_fx_params", {{"trackId", 0}, {"slotIndex", 0}}).toString().toUtf8()).object();
    auto params = listObj.value("params").toArray();
    ASSERT_EQ(params.size(), 3);
    EXPECT_EQ(params.at(0).toObject().value("name").toString().toStdString(), "Cutoff");
    EXPECT_EQ(params.at(0).toObject().value("minValue").toDouble(), 20.0);
    EXPECT_EQ(params.at(0).toObject().value("maxValue").toDouble(), 20000.0);
    EXPECT_EQ(params.at(0).toObject().value("paramID").toInt(), 100);
    EXPECT_EQ(params.at(1).toObject().value("name").toString().toStdString(), "Mode");
    EXPECT_EQ(params.at(1).toObject().value("maxValue").toDouble(), 2.0);
    EXPECT_EQ(params.at(1).toObject().value("paramID").toInt(), 101);
    EXPECT_EQ(params.at(2).toObject().value("name").toString().toStdString(), "Resonance");
    EXPECT_NEAR(params.at(2).toObject().value("minValue").toDouble(), 0.1, 1e-6);
    EXPECT_EQ(params.at(2).toObject().value("maxValue").toDouble(), 10.0);
    EXPECT_EQ(params.at(2).toObject().value("paramID").toInt(), 102);

    // set_internal_fx_param (real units).
    EXPECT_FALSE(isError(call("set_internal_fx_param", {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 250.0}})));
    EXPECT_FALSE(isError(call("set_internal_fx_param", {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 1}, {"value", 1.0}})));
    EXPECT_FALSE(isError(call("set_internal_fx_param", {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 2}, {"value", 1.5}})));

    // Gate 1/10: full rebuild; the LIVE slot keeps the type + restored params.
    engine->drainPendingRoutingRebuild();
    engine->getMainProcessor()->rebuildRoutingGraph();
    engine->drainPendingRoutingRebuild();
    auto* track = engine->getMainProcessor()->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(track->getNumFXSlots(), 1);
    auto* slot = track->getFXChain().at(0).get();
    EXPECT_EQ(slot->getType().toStdString(), "filter");
    auto vals = slot->getInternalParamValues();
    ASSERT_GE(vals.size(), 3u);
    EXPECT_NEAR(vals[0], 250.0f, 0.01f);
    EXPECT_NEAR(vals[1], 1.0f, 0.01f);
    EXPECT_NEAR(vals[2], 1.5f, 0.01f);

    // ReadModel parity (real units).
    auto rp = engine->getReadModel().getInternalFxParams(0, 0);
    ASSERT_EQ(rp.size(), 3u);
    EXPECT_NEAR(rp[0].value, 250.0f, 0.01f);
    EXPECT_NEAR(rp[1].value, 1.0f, 0.01f);
    EXPECT_NEAR(rp[2].value, 1.5f, 0.01f);
}

// P1.3 (plan 2026-08-30): get_internal_fx_param reads back CURRENT internal
// FX param values in REAL units after set_internal_fx_param — the
// metadata-only gap from the 8/30 handoff B5/B8. The read path is
// ReadModelImpl::getInternalFxParams (same ValueTree param_N storage the
// engine loads from; no render, no DSP access). list_fx_params now reports the
// value too, symmetric with its plugin branch.
TEST_F(McpCoverageTest, GetInternalFxParamReadsBackRealUnits) {
    // EQ slot: set Frequency + Gain, leave Q untouched.
    auto addEq = call("add_fx", {{"trackId", 0}, {"fxType", "eq"}});
    ASSERT_FALSE(isError(addEq)) << text(addEq).toStdString();
    EXPECT_EQ(text(addEq).mid(text(addEq).indexOf('=') + 1).toInt(), 0);

    // list_fx_params carries metadata AND the current value.
    auto list = QJsonDocument::fromJson(
        callText("list_fx_params", {{"trackId", 0}, {"slotIndex", 0}}).toString().toUtf8()).object();
    auto params = list.value("params").toArray();
    ASSERT_EQ(params.size(), 3);
    EXPECT_EQ(params.at(0).toObject().value("name").toString().toStdString(), "Frequency");
    EXPECT_TRUE(params.at(0).toObject().contains("value"));
    EXPECT_NEAR(params.at(0).toObject().value("value").toDouble(), 1000.0, 1e-6); // default before set

    EXPECT_FALSE(isError(call("set_internal_fx_param", {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 3600.0}})));
    EXPECT_FALSE(isError(call("set_internal_fx_param", {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 2}, {"value", 2.5}})));

    auto getObj = QJsonDocument::fromJson(
        callText("get_internal_fx_param", {{"trackId", 0}, {"slotIndex", 0}}).toString().toUtf8()).object();
    auto got = getObj.value("params").toArray();
    ASSERT_EQ(got.size(), 3);
    EXPECT_EQ(got.at(0).toObject().value("index").toInt(), 0);
    EXPECT_EQ(got.at(0).toObject().value("name").toString().toStdString(), "Frequency");
    EXPECT_NEAR(got.at(0).toObject().value("value").toDouble(), 3600.0, 1e-3);
    EXPECT_NEAR(got.at(1).toObject().value("value").toDouble(), 0.7, 1e-6);   // Q untouched -> default
    EXPECT_NEAR(got.at(2).toObject().value("value").toDouble(), 2.5, 1e-3);   // Gain
    EXPECT_NEAR(got.at(2).toObject().value("minValue").toDouble(), -24.0, 1e-6);

    // Compressor slot: threshold/ratio in real dB/ratio units.
    auto addComp = call("add_fx", {{"trackId", 0}, {"fxType", "compressor"}});
    ASSERT_FALSE(isError(addComp)) << text(addComp).toStdString();
    EXPECT_FALSE(isError(call("set_internal_fx_param", {{"trackId", 0}, {"slotIndex", 1}, {"paramIndex", 0}, {"value", -18.0}})));
    EXPECT_FALSE(isError(call("set_internal_fx_param", {{"trackId", 0}, {"slotIndex", 1}, {"paramIndex", 1}, {"value", 4.0}})));
    auto compObj = QJsonDocument::fromJson(
        callText("get_internal_fx_param", {{"trackId", 0}, {"slotIndex", 1}}).toString().toUtf8()).object();
    auto comp = compObj.value("params").toArray();
    ASSERT_EQ(comp.size(), 4);
    EXPECT_NEAR(comp.at(0).toObject().value("value").toDouble(), -18.0, 1e-3);
    EXPECT_NEAR(comp.at(1).toObject().value("value").toDouble(), 4.0, 1e-3);

    // Sampler slot: env params read back WITHOUT a sample loaded — the tree is
    // the source of truth (handoff B8: no render predictor needed for values).
    auto addSam = call("add_fx", {{"trackId", 0}, {"fxType", "sampler"}});
    ASSERT_FALSE(isError(addSam)) << text(addSam).toStdString();
    EXPECT_FALSE(isError(call("set_internal_fx_param", {{"trackId", 0}, {"slotIndex", 2}, {"paramIndex", 0}, {"value", 0.5}})));
    auto samObj = QJsonDocument::fromJson(
        callText("get_internal_fx_param", {{"trackId", 0}, {"slotIndex", 2}}).toString().toUtf8()).object();
    auto sam = samObj.value("params").toArray();
    ASSERT_EQ(sam.size(), 10);
    EXPECT_EQ(sam.at(0).toObject().value("name").toString().toStdString(), "Attack");
    EXPECT_NEAR(sam.at(0).toObject().value("value").toDouble(), 0.5, 1e-6);
    EXPECT_NEAR(sam.at(4).toObject().value("value").toDouble(), 0.0, 1e-6);   // Transpose untouched -> 0
}

// P1.3 validation (Gate 9): errors are tool-named (handoff B-note: bare
// validator messages were un-attributable across a burst).
TEST_F(McpCoverageTest, GetInternalFxParamValidation) {
    // Slot out of range -> tool-named error.
    auto r1 = call("get_internal_fx_param", {{"trackId", 0}, {"slotIndex", 5}});
    EXPECT_TRUE(isError(r1)) << text(r1).toStdString();
    EXPECT_TRUE(text(r1).contains("get_internal_fx_param")) << text(r1).toStdString();

    // Empty/unknown FX type (add_fx with no fxType) is not an internal FX.
    auto addEmpty = call("add_fx", {{"trackId", 0}});
    ASSERT_FALSE(isError(addEmpty)) << text(addEmpty).toStdString();
    auto r2 = call("get_internal_fx_param", {{"trackId", 0}, {"slotIndex", 0}});
    EXPECT_TRUE(isError(r2)) << text(r2).toStdString();
    EXPECT_TRUE(text(r2).contains("get_internal_fx_param")) << text(r2).toStdString();
}

// W1 (plan 2026-08-30): generate_psytrance — one call writes the complete
// key-disciplined score onto the caller's palette tracks (Guide §4 grammar).
// Covers tool registration, palette mapping, compact output (B6: no note
// payload), determinism, unmapped-role reporting, and tool-named validation.
TEST_F(McpCoverageTest, GeneratePsytranceRoundTrip) {
    QStringList toolNames;
    for (const auto& t : toolList())
        toolNames << t.toObject().value("name").toString();
    EXPECT_TRUE(toolNames.contains("generate_psytrance"));

    // Palette: 8 sampler-less tracks (notes/clips only — no samples needed).
    auto addTrack = [this](const QString& name) {
        auto r = callText("add_track", {{"name", name}});
        auto obj = QJsonDocument::fromJson(r.toString().toUtf8()).object();
        return obj.value("trackId").toInt(-1);
    };
    QJsonObject pt;
    for (const char* role : { "kick", "bass", "hat", "arp", "stab", "pad", "riser", "down" })
    {
        const int t = addTrack(QString("Psy%1").arg(role));
        ASSERT_GE(t, 3) << "palette track " << role;
        pt[role] = t;
    }

    QJsonArray sections;
    sections.append(QJsonObject{ { "name", "intro" },     { "start", 0.0 },   { "end", 32.0 } });
    sections.append(QJsonObject{ { "name", "build" },     { "start", 32.0 },  { "end", 64.0 } });
    sections.append(QJsonObject{ { "name", "mainA" },     { "start", 64.0 },  { "end", 192.0 } });
    sections.append(QJsonObject{ { "name", "mini" },      { "start", 192.0 }, { "end", 224.0 } });
    sections.append(QJsonObject{ { "name", "mainB" },     { "start", 224.0 }, { "end", 352.0 } });
    sections.append(QJsonObject{ { "name", "breakdown" }, { "start", 352.0 }, { "end", 384.0 } });
    sections.append(QJsonObject{ { "name", "finale" },    { "start", 384.0 }, { "end", 512.0 } });

    auto args = QJsonObject{
        { "paletteTrackIds", pt },
        { "sections", sections },
        { "keyRoot", 5 },
        { "scaleMode", 1 },
        { "density", 0.7 },
        { "seed", 42 } };

    auto r = call("generate_psytrance", args);
    ASSERT_FALSE(isError(r)) << text(r).toStdString();
    // B6: compact output — a summary, never the 2,000+ note payload.
    EXPECT_LT(text(r).size(), 4096) << text(r).left(200).toStdString();
    auto res = QJsonDocument::fromJson(text(r).toUtf8()).object();
    EXPECT_EQ(res.value("totalBeats").toDouble(), 512.0);
    EXPECT_GT(res.value("notesTotal").toInt(), 1500); // ~2,600 in the reference track
    EXPECT_EQ(res.value("notesSkipped").toInt(), 0);
    auto clips = res.value("clips").toArray();
    EXPECT_EQ(clips.size(), 8);

    QSet<int> seenTracks;
    for (const auto& cv : clips)
    {
        auto c = cv.toObject();
        const QString role = c.value("role").toString();
        EXPECT_TRUE(pt.contains(role)) << "unknown role " << role.toStdString();
        EXPECT_EQ(c.value("trackId").toInt(), pt.value(role).toInt(-1))
            << "role " << role.toStdString() << " landed on the wrong track";
        EXPECT_GT(c.value("noteCount").toInt(), 0) << role.toStdString();
        EXPECT_GE(c.value("clipId").toInt(), 0);
        seenTracks.insert(c.value("trackId").toInt());
    }
    EXPECT_EQ(seenTracks.size(), 8);
    // Clap is unmapped → correctly reported as skipped.
    auto skippedArr = res.value("skipped").toArray();
    QStringList skippedList;
    for (const auto& s : skippedArr) skippedList << s.toString();
    EXPECT_TRUE(skippedList.isEmpty() || skippedList.contains("clap"))
        << "only clap should be skipped, got: " << skippedList.join(", ").toStdString();

    // Determinism: same seed + params → identical note counts everywhere.
    auto r2 = call("generate_psytrance", args);
    ASSERT_FALSE(isError(r2));
    auto res2 = QJsonDocument::fromJson(text(r2).toUtf8()).object();
    auto clips2 = res2.value("clips").toArray();
    ASSERT_EQ(clips2.size(), clips.size());
    for (int i = 0; i < clips.size(); ++i)
        EXPECT_EQ(clips.at(i).toObject().value("noteCount").toInt(),
                  clips2.at(i).toObject().value("noteCount").toInt());

    // Unmapped optional roles are reported as skipped, not errors; the clap
    // role defaults to the hat track when omitted.
    auto ptNoFx = pt;
    ptNoFx.remove("riser");
    ptNoFx.remove("down");
    auto argsNoFx = args;
    argsNoFx["paletteTrackIds"] = ptNoFx;
    auto r3 = call("generate_psytrance", argsNoFx);
    ASSERT_FALSE(isError(r3)) << text(r3).toStdString();
    auto res3 = QJsonDocument::fromJson(text(r3).toUtf8()).object();
    auto skipped = res3.value("skipped").toArray();
    EXPECT_TRUE(skipped.contains("riser")) << QJsonDocument(skipped).toJson(QJsonDocument::Compact).toStdString();
    EXPECT_TRUE(skipped.contains("down")) << QJsonDocument(skipped).toJson(QJsonDocument::Compact).toStdString();

    // Validation: a bad palette track id is a tool-named error and nothing is
    // written (Gate 9 + no partial writes).
    auto ptBad = pt;
    ptBad["kick"] = 999;
    auto argsBad = args;
    argsBad["paletteTrackIds"] = ptBad;
    auto r4 = call("generate_psytrance", argsBad);
    EXPECT_TRUE(isError(r4)) << text(r4).toStdString();
    EXPECT_TRUE(text(r4).contains("generate_psytrance")) << text(r4).toStdString();
}


// ============================================================================
// BREAK CHOPPER / COMPOSER — docs/plans/2026-08-29-jungle-dnb-feature-gaps.md P2-1
// ============================================================================

namespace {

// A 4-second click track (short 1 kHz sine bursts every 0.25 s). The
// transient slice detector reliably finds >= 15 onsets, giving a real
// multi-slice break to compose against (hermetic — no dependency on the
// E: sample kit).
QString writeClickWav() {
    QString dir = QDir::tempPath();
    QString path = QString("%1/hdaw_break_click_%2.wav")
                       .arg(QDir::fromNativeSeparators(dir))
                       .arg(QCoreApplication::applicationPid());
    QFile::remove(path);

    constexpr int sampleRate = 44100;
    constexpr int numSeconds = 4;
    juce::AudioBuffer<float> buf(1, sampleRate * numSeconds);
    buf.clear();
    constexpr double clickEvery = 0.25;      // s between clicks
    constexpr int burstSamples = 660;        // ~15 ms burst
    constexpr double clickHz = 1000.0;
    const double clickStep = clickHz / sampleRate;
    for (int c = 0; c < buf.getNumSamples(); ++c)
    {
        const double phaseInClick = std::fmod(static_cast<double>(c) / sampleRate, clickEvery);
        if (phaseInClick < static_cast<double>(burstSamples) / sampleRate)
        {
            const int n = static_cast<int>(std::round(phaseInClick * sampleRate));
            buf.setSample(0, c, 0.5f * static_cast<float>(
                std::sin(2.0 * juce::MathConstants<double>::pi * clickStep * n)));
        }
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(juce::File(path.toStdString())),
                            sampleRate, 1, 16, {}, 0));
    if (writer == nullptr) return {};
    writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    writer->flush();
    return path;
}

} // namespace

// End-to-end: detect slices -> slice mode -> generate_chopped_break writes a
// MIDI slice-trigger pattern (Gate 2 data path: tool -> command -> clip
// MIDI_NOTE_LIST). Pitches map to slices (baseNote + sliceIndex); jungleEdit
// drops the first kick (no note at beat 0); the no-slices path names
// detect_sampler_slices.
TEST_F(McpCoverageTest, GenerateChoppedBreakWritesSlicePattern) {
    // Tool is registered.
    QStringList toolNames;
    for (const auto& t : toolList())
        toolNames << t.toObject().value("name").toString();
    EXPECT_TRUE(toolNames.contains("generate_chopped_break"))
        << "generate_chopped_break not registered";

    // Build a track + sampler with a click WAV and DETECT its slices.
    QString wavPath = writeClickWav();
    ASSERT_FALSE(wavPath.isEmpty()) << "failed to write click WAV";
    auto addFx = call("add_fx", {{"trackId", 0}, {"fxType", "sampler"}});
    ASSERT_FALSE(isError(addFx)) << text(addFx).toStdString();
    int slot = text(addFx).mid(text(addFx).indexOf('=') + 1).toInt();

    auto setSample = call("sampler_set_sample", {
        {"trackId", 0}, {"slotIndex", slot}, {"filePath", wavPath}
    });
    ASSERT_FALSE(isError(setSample)) << text(setSample).toStdString();

    auto detect = call("detect_sampler_slices", {
        {"trackId", 0}, {"slotIndex", slot},
        {"sliceMode", "transient"}, {"sliceSensitivity", 0.5}
    });
    ASSERT_FALSE(isError(detect)) << text(detect).toStdString();
    auto detectObj = QJsonDocument::fromJson(text(detect).toUtf8()).object();
    int totalSlices = detectObj.value("totalSlices").toInt();
    ASSERT_GE(totalSlices, 2) << "click WAV should yield multiple slices: " << text(detect).toStdString();

    auto setMode = call("set_sampler_mode", {
        {"trackId", 0}, {"slotIndex", slot}, {"mode", "slice"}
    });
    ASSERT_FALSE(isError(setMode)) << text(setMode).toStdString();

    // Fresh MIDI clip spanning 2 bars (8 beats).
    int clipId = addMidiClip(0, 0.0, 8.0, "Break");
    ASSERT_GT(clipId, 0);

    // Compose a jungleEdit break: bars=2, dropFirst=true, seed=7.
    auto r = call("generate_chopped_break", {
        {"trackId", 0}, {"slotIndex", slot}, {"clipId", clipId},
        {"bars", 2}, {"style", "jungleEdit"}, {"dropFirst", true}, {"seed", 7}
    });
    ASSERT_FALSE(isError(r)) << text(r).toStdString();
    auto res = QJsonDocument::fromJson(text(r).toUtf8()).object();
    int added = res.value("added").toInt();
    int firstPitch = res.value("firstPitch").toInt();
    int lastPitch = res.value("lastPitch").toInt();
    int sliceCount = res.value("sliceCount").toInt();
    int baseNote = res.value("baseNote").toInt();
    EXPECT_GT(added, 0);
    EXPECT_EQ(sliceCount, totalSlices);
    EXPECT_EQ(baseNote, 60);
    EXPECT_GE(firstPitch, 60);
    EXPECT_GE(lastPitch, 60);
    EXPECT_LE(firstPitch, 60 + totalSlices - 1);
    EXPECT_LE(lastPitch, 60 + totalSlices - 1);

    // The written notes are slice triggers: pitch in [baseNote, baseNote +
    // totalSlices - 1]; clip-local starts in [0, 8); the jungleEdit drops
    // the first kick -> no note at startBeat 0.
    auto listR = call("list_notes", {{"clipId", clipId}});
    ASSERT_FALSE(isError(listR)) << text(listR).toStdString();
    auto listed = QJsonDocument::fromJson(text(listR).toUtf8()).object();
    ASSERT_EQ(listed.value("count").toInt(), added) << "clip note count must match the tool result";
    auto notesArr = listed.value("notes").toArray();
    ASSERT_GT(notesArr.size(), 0);
    for (const auto& nv : notesArr)
    {
        auto n = nv.toObject();
        const int pitch = n.value("pitch").toInt();
        const double start = n.value("start").toDouble();
        EXPECT_GE(pitch, baseNote);
        EXPECT_LE(pitch, baseNote + totalSlices - 1) << "pitch " << pitch;
        EXPECT_GE(start, 0.0);
        EXPECT_LT(start, 8.0) << "start " << start;
        EXPECT_NE(start, 0.0) << "first kick must be dropped (jungleEdit open)";
        EXPECT_GE(n.value("velocity").toInt(), 1);
        EXPECT_LE(n.value("velocity").toInt(), 127);
    }

    // Failure path: a sampler slot with NO detected slices errors and names
    // the missing precondition.
    auto addFx2 = call("add_fx", {{"trackId", 0}, {"fxType", "sampler"}});
    ASSERT_FALSE(isError(addFx2)) << text(addFx2).toStdString();
    int slot2 = text(addFx2).mid(text(addFx2).indexOf('=') + 1).toInt();
    auto failR = call("generate_chopped_break", {
        {"trackId", 0}, {"slotIndex", slot2}, {"clipId", clipId}, {"bars", 2}
    });
    EXPECT_TRUE(isError(failR)) << text(failR).toStdString();
    EXPECT_TRUE(text(failR).contains("detect_sampler_slices"))
        << text(failR).toStdString();

    // Validation: a bad style name is rejected at the tool boundary (Gate 9 —
    // the schema enum check fires before the handler).
    auto badStyle = call("generate_chopped_break", {
        {"trackId", 0}, {"slotIndex", slot}, {"clipId", clipId}, {"style", "juke"}
    });
    EXPECT_TRUE(isError(badStyle));
    EXPECT_TRUE(text(badStyle).contains("enum")) << text(badStyle).toStdString();
}

// ============================================================================
// PATTERN PLACEMENT — docs/plans/2026-08-29-jungle-dnb-feature-gaps.md P2-2
// ============================================================================

namespace {

// The 3-note test pattern used across every placement case (the analyze_midi_
// file patterns[] shape: clip/pattern-local beats, velocity 0..127).
QJsonObject threeNotePattern()
{
    return QJsonObject{{"notes", QJsonArray{
        QJsonObject{{"pitch", 60}, {"startBeat", 0.0}, {"durationBeats", 1.0}, {"velocity", 100}},
        QJsonObject{{"pitch", 64}, {"startBeat", 1.0}, {"durationBeats", 1.0}, {"velocity", 100}},
        QJsonObject{{"pitch", 67}, {"startBeat", 2.0}, {"durationBeats", 1.0}, {"velocity", 100}}}}};
}

// Read every note of a clip into start -> {pitch, velocity}.
std::map<double, std::pair<int, int>> notesByStart(const QJsonObject& listed)
{
    std::map<double, std::pair<int, int>> m;
    for (const auto& nv : listed.value("notes").toArray())
    {
        auto no = nv.toObject();
        m[no.value("start").toDouble()] = { no.value("pitch").toInt(),
                                            no.value("velocity").toInt() };
    }
    return m;
}

} // namespace

// Gate 2 end-to-end data path: tool -> command -> clip MIDI_NOTE_LIST ->
// list_notes read-back. Three placements of a 3-note pattern: bar 1 identity
// (60/64/67 @0..3 vel 100), bar 2 octave+1 / velocity scale 0.5 (72/76/79 @8,
// vel 50), bar 3 retrograde (67/64/60 @16..18). clear=true resets the clip
// inside the same call. Error paths: empty placements, octave out of range,
// unknown clipId, out-of-range pattern pitch.
TEST_F(McpCoverageTest, PlacePatternsTilesWithTransforms)
{
    // Tool is registered.
    QStringList toolNames;
    for (const auto& t2 : toolList())
        toolNames << t2.toObject().value("name").toString();
    EXPECT_TRUE(toolNames.contains("place_patterns")) << "place_patterns not registered";

    int clipId = addMidiClip(0, 0.0, 32.0, "Placed");
    ASSERT_GT(clipId, 0) << "add_midi_clip failed";

    auto r = call("place_patterns", {
        {"clipId", clipId},
        {"patterns", QJsonArray{threeNotePattern()}},
        {"placements", QJsonArray{
            QJsonObject{{"start", 0.0}},
            QJsonObject{{"start", 8.0}, {"octave", 1}, {"velocityScale", 0.5}},
            QJsonObject{{"start", 16.0}, {"reverse", true}}}}
    });
    ASSERT_FALSE(isError(r)) << text(r).toStdString();
    auto res = QJsonDocument::fromJson(text(r).toUtf8()).object();
    EXPECT_EQ(res.value("added").toInt(), 9) << text(r).toStdString();
    EXPECT_EQ(res.value("skipped").toInt(), 0);
    EXPECT_EQ(res.value("clipId").toInt(), clipId);
    EXPECT_EQ(res.value("placementsApplied").toInt(), 3);

    // Read-back through list_notes: bar 1 identity, bar 2 octave+vel, bar 3
    // retrograde (67/64/60 occupying starts 16..18).
    auto listR = call("list_notes", {{"clipId", clipId}});
    ASSERT_FALSE(isError(listR)) << text(listR).toStdString();
    auto listed = QJsonDocument::fromJson(text(listR).toUtf8()).object();
    ASSERT_EQ(listed.value("count").toInt(), 9) << text(listR).toStdString();
    auto byStart = notesByStart(listed);
    ASSERT_EQ(byStart.size(), 9u) << "duplicate (start,pitch) occupancy: " << text(listR).toStdString();
    auto expectNote = [&](double start, int pitch, int velocity) {
        auto it = byStart.find(start);
        ASSERT_NE(it, byStart.end()) << "no note at start " << start;
        EXPECT_EQ(it->second.first, pitch) << "pitch at beat " << start;
        EXPECT_EQ(it->second.second, velocity) << "velocity at beat " << start;
    };
    // Bar 1: identity.
    expectNote(0.0, 60, 100);
    expectNote(1.0, 64, 100);
    expectNote(2.0, 67, 100);
    // Bar 2: octave +1, velocity scaled 0.5 (100 -> 50).
    expectNote(8.0, 72, 50);
    expectNote(9.0, 76, 50);
    expectNote(10.0, 79, 50);
    // Bar 3: retrograde — 67/64/60 at starts 16/17/18 (same span as bar 1).
    expectNote(16.0, 67, 100);
    expectNote(17.0, 64, 100);
    expectNote(18.0, 60, 100);

    // clear=true replaces the clip's content in the SAME call: 2 placements of
    // the 3-note pattern -> 6 notes, total count resets from 9 to 6.
    auto clearR = call("place_patterns", {
        {"clipId", clipId},
        {"patterns", QJsonArray{threeNotePattern()}},
        {"placements", QJsonArray{
            QJsonObject{{"start", 0.0}},
            QJsonObject{{"start", 8.0}, {"reverse", true}}}},
        {"clear", true}
    });
    ASSERT_FALSE(isError(clearR)) << text(clearR).toStdString();
    auto clearRes = QJsonDocument::fromJson(text(clearR).toUtf8()).object();
    EXPECT_EQ(clearRes.value("added").toInt(), 6);
    EXPECT_EQ(clearRes.value("skipped").toInt(), 0);
    auto listR2 = call("list_notes", {{"clipId", clipId}});
    ASSERT_FALSE(isError(listR2)) << text(listR2).toStdString();
    auto listed2 = QJsonDocument::fromJson(text(listR2).toUtf8()).object();
    EXPECT_EQ(listed2.value("count").toInt(), 6) << "clear=true must reset the clip: " << text(listR2).toStdString();
    auto byStart2 = notesByStart(listed2);
    // clear placed identity @0 + retrograde @8: pitches 60/64/67 @0/1/2 and
    // 67/64/60 @10/9/8.
    auto expectNote2 = [&](double start, int pitch) {
        auto it = byStart2.find(start);
        ASSERT_NE(it, byStart2.end()) << "no note at start " << start;
        EXPECT_EQ(it->second.first, pitch);
    };
    expectNote2(0.0, 60); expectNote2(1.0, 64); expectNote2(2.0, 67);
    expectNote2(8.0, 67); expectNote2(9.0, 64); expectNote2(10.0, 60);

    // Error paths (Gate 9).
    auto emptyPlacements = call("place_patterns", {
        {"clipId", clipId}, {"patterns", QJsonArray{threeNotePattern()}},
        {"placements", QJsonArray()}});
    EXPECT_TRUE(isError(emptyPlacements)) << text(emptyPlacements).toStdString();
    EXPECT_TRUE(text(emptyPlacements).contains("placements")) << text(emptyPlacements).toStdString();

    auto badOctave = call("place_patterns", {
        {"clipId", clipId}, {"patterns", QJsonArray{threeNotePattern()}},
        {"placements", QJsonArray{QJsonObject{{"start", 0.0}, {"octave", 99}}}}});
    EXPECT_TRUE(isError(badOctave)) << text(badOctave).toStdString();
    EXPECT_TRUE(text(badOctave).contains("maximum")) << text(badOctave).toStdString();

    auto unknownClip = call("place_patterns", {
        {"clipId", 999999}, {"patterns", QJsonArray{threeNotePattern()}},
        {"placements", QJsonArray{QJsonObject{{"start", 0.0}}}}});
    EXPECT_TRUE(isError(unknownClip)) << text(unknownClip).toStdString();
    EXPECT_TRUE(text(unknownClip).contains("clip not found")) << text(unknownClip).toStdString();

    QJsonObject badPattern{{"notes", QJsonArray{
        QJsonObject{{"pitch", 200}, {"startBeat", 0.0}, {"durationBeats", 1.0}, {"velocity", 100}}}}};
    auto badPitch = call("place_patterns", {
        {"clipId", clipId}, {"patterns", QJsonArray{badPattern}},
        {"placements", QJsonArray{QJsonObject{{"start", 0.0}}}}});
    EXPECT_TRUE(isError(badPitch)) << text(badPitch).toStdString();
    EXPECT_TRUE(text(badPitch).contains("pitch")) << text(badPitch).toStdString();

    // Empty patterns list is rejected at the handler boundary.
    auto emptyPatterns = call("place_patterns", {
        {"clipId", clipId}, {"patterns", QJsonArray()},
        {"placements", QJsonArray{QJsonObject{{"start", 0.0}}}}});
    EXPECT_TRUE(isError(emptyPatterns)) << text(emptyPatterns).toStdString();
    EXPECT_TRUE(text(emptyPatterns).contains("patterns")) << text(emptyPatterns).toStdString();
}

// Cyclic pattern use: placement j uses patterns[j % patterns.size()], so with
// 2 patterns and 3 placements the third placement repeats pattern 0.
TEST_F(McpCoverageTest, PlacePatternsCyclicPlacement)
{
    int clipId = addMidiClip(0, 0.0, 32.0, "Cyclic");
    ASSERT_GT(clipId, 0);

    QJsonObject patA{{"notes", QJsonArray{
        QJsonObject{{"pitch", 60}, {"startBeat", 0.0}, {"durationBeats", 1.0}, {"velocity", 100}},
        QJsonObject{{"pitch", 61}, {"startBeat", 1.0}, {"durationBeats", 1.0}, {"velocity", 100}}}}};
    QJsonObject patB{{"notes", QJsonArray{
        QJsonObject{{"pitch", 70}, {"startBeat", 0.0}, {"durationBeats", 1.0}, {"velocity", 100}},
        QJsonObject{{"pitch", 71}, {"startBeat", 1.0}, {"durationBeats", 1.0}, {"velocity", 100}}}}};

    auto r = call("place_patterns", {
        {"clipId", clipId},
        {"patterns", QJsonArray{patA, patB}},
        {"placements", QJsonArray{
            QJsonObject{{"start", 0.0}},
            QJsonObject{{"start", 4.0}},
            QJsonObject{{"start", 8.0}}}}
    });
    ASSERT_FALSE(isError(r)) << text(r).toStdString();
    auto res = QJsonDocument::fromJson(text(r).toUtf8()).object();
    EXPECT_EQ(res.value("added").toInt(), 6);
    EXPECT_EQ(res.value("placementsApplied").toInt(), 3);

    auto listR = call("list_notes", {{"clipId", clipId}});
    ASSERT_FALSE(isError(listR)) << text(listR).toStdString();
    auto listed = QJsonDocument::fromJson(text(listR).toUtf8()).object();
    auto byStart = notesByStart(listed);
    auto expectNote = [&](double start, int pitch) {
        auto it = byStart.find(start);
        ASSERT_NE(it, byStart.end()) << "no note at start " << start;
        EXPECT_EQ(it->second.first, pitch) << "pitch at beat " << start;
    };
    // placement 0 -> pattern A (60/61); placement 1 -> pattern B (70/71);
    // placement 2 -> pattern A again (j % 2 == 0).
    expectNote(0.0, 60); expectNote(1.0, 61);
    expectNote(4.0, 70); expectNote(5.0, 71);
    expectNote(8.0, 60); expectNote(9.0, 61);
}


// ── P2-4 / P2-5 fixtures and MCP coverage tests ─────────────────────────────

// Minimal MIDI writer (single track): tempo meta + note on/off pairs.
void writeMidiVarLen(std::vector<uint8_t>& buf, uint32_t value)
{
    if (value < 0x80) { buf.push_back(static_cast<uint8_t>(value)); return; }
    std::vector<uint8_t> bytes;
    bytes.push_back(static_cast<uint8_t>(value & 0x7F));
    value >>= 7;
    while (value > 0) {
        bytes.push_back(static_cast<uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
        buf.push_back(*it);
}

// notes = {pitch, velocity, startBeat, durationBeats}. Returns temp file path.
QString writeMidiWithTempo(const std::vector<std::tuple<int, int, double, double>>& notes,
                           double bpm)
{
    const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_analyze_" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".mid");
    f.deleteFile();

    constexpr int ticksPerQuarter = 480;
    struct MidiEvent { uint32_t tick; bool isNoteOn; int pitch; int velocity; };
    std::vector<MidiEvent> events;
    for (const auto& [pitch, vel, start, dur] : notes) {
        events.push_back({ static_cast<uint32_t>(start * ticksPerQuarter), true, pitch, vel });
        events.push_back({ static_cast<uint32_t>((start + dur) * ticksPerQuarter), false, pitch, 0 });
    }
    std::sort(events.begin(), events.end(),
        [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    std::vector<uint8_t> data;
    const uint8_t header[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1,
        static_cast<uint8_t>((ticksPerQuarter >> 8) & 0xFF),
        static_cast<uint8_t>(ticksPerQuarter & 0xFF)
    };
    data.insert(data.end(), header, header + sizeof(header));

    std::vector<uint8_t> trackData;
    const uint32_t usPerQuarter = static_cast<uint32_t>(60000000.0 / bpm);
    writeMidiVarLen(trackData, 0);
    trackData.insert(trackData.end(), { 0xFF, 0x51, 0x03 });
    trackData.push_back((usPerQuarter >> 16) & 0xFF);
    trackData.push_back((usPerQuarter >> 8) & 0xFF);
    trackData.push_back(usPerQuarter & 0xFF);

    uint32_t lastTick = 0;
    for (const auto& ev : events) {
        writeMidiVarLen(trackData, ev.tick - lastTick);
        lastTick = ev.tick;
        trackData.push_back(ev.isNoteOn ? 0x90 : 0x80);
        trackData.push_back(static_cast<uint8_t>(ev.pitch));
        trackData.push_back(static_cast<uint8_t>(ev.velocity));
    }
    writeMidiVarLen(trackData, 0);
    trackData.insert(trackData.end(), { 0xFF, 0x2F, 0 });

    data.insert(data.end(), { 'M','T','r','k' });
    const uint32_t trackLen = static_cast<uint32_t>(trackData.size());
    data.push_back((trackLen >> 24) & 0xFF);
    data.push_back((trackLen >> 16) & 0xFF);
    data.push_back((trackLen >> 8) & 0xFF);
    data.push_back(trackLen & 0xFF);
    data.insert(data.end(), trackData.begin(), trackData.end());

    std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
    out->write(data.data(), static_cast<int>(data.size()));
    out.reset();
    return QString::fromStdString(f.getFullPathName().toStdString());
}

// Phase-continuous sine helper (deterministic fixtures).
double synthSineAt(double t, double freq, double amp)
{
    return amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t);
}

// 8 s @ 48 kHz mono WAV (4 s per section — each section is 8 beats at the
// 120 bpm used below, satisfying the >= 8-beat pump-depth contract):
//   A [0,4): 60 Hz @ 0.5 + 440 Hz @ 0.3  -> rms sqrt(0.17) ~ 0.4123
//   B [4,8): 440 Hz @ 0.2 + 8000 Hz @ 0.3 -> rms sqrt(0.065) ~ 0.2549
// Integer cycles per section -> deterministic RMS. Whole-file rms ~ 0.3428.
QString writeMixReportFixtureWav()
{
    const juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_mix_report_" + juce::String(juce::Random::getSystemRandom().nextInt()) + ".wav");
    f.deleteFile();
    constexpr double sr = 48000.0;
    constexpr int len = static_cast<int>(sr * 8.0);
    juce::AudioBuffer<float> buf(1, len);
    for (int64_t i = 0; i < len; ++i) {
        const double t = static_cast<double>(i) / sr;
        float v = (t < 4.0)
            ? static_cast<float>(synthSineAt(t, 60.0, 0.5) + synthSineAt(t, 440.0, 0.3))
            : static_cast<float>(synthSineAt(t, 440.0, 0.2) + synthSineAt(t, 8000.0, 0.3));
        buf.setSample(0, static_cast<int>(i), v);
    }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(f), sr, 1, 24, {}, 0));
    if (writer == nullptr) return {};
    writer->writeFromAudioSampleBuffer(buf, 0, len);
    return QString::fromStdString(f.getFullPathName().toStdString());
}

// P2-4: analyze_midi_file now returns key/scale/bpm (top-level) and stable
// per-pattern ids. Fixture: two identical bars of C minor @ 128 bpm — C is
// the heaviest pitch class so detectScale lands on Aeolian (scaleType 1).
TEST_F(McpCoverageTest, AnalyzeMidiFileReturnsKeyScaleBpmAndPatternIds)
{
    QStringList toolNames;
    for (const auto& t2 : toolList())
        toolNames << t2.toObject().value("name").toString();
    EXPECT_TRUE(toolNames.contains("analyze_midi_file"));

    std::vector<std::tuple<int, int, double, double>> bar = {
        {60, 100, 0.0, 0.5}, {60, 100, 0.5, 0.5}, {63, 100, 1.0, 0.5},
        {67, 100, 1.5, 0.5}, {65, 100, 2.0, 0.5}, {70, 100, 2.5, 0.25},
        {60, 100, 3.0, 0.75}
    };
    std::vector<std::tuple<int, int, double, double>> notes = bar;
    for (const auto& n : bar) {
        notes.push_back({ std::get<0>(n), std::get<1>(n),
                          std::get<2>(n) + 4.0, std::get<3>(n) });
    }

    const QString path = writeMidiWithTempo(notes, 128.0);
    ASSERT_FALSE(path.isEmpty());
    auto r = call("analyze_midi_file", {{"path", path}});
    ASSERT_FALSE(isError(r)) << text(r).toStdString();

    auto obj = QJsonDocument::fromJson(text(r).toUtf8()).object();
    EXPECT_EQ(obj.value("key").toString().toStdString(), "C minor") << text(r).toStdString();
    EXPECT_DOUBLE_EQ(obj.value("bpm").toDouble(), 128.0);
    EXPECT_DOUBLE_EQ(obj.value("sourceBpm").toDouble(), 128.0);
    EXPECT_EQ(obj.value("scaleType").toInt(), 1) << text(r).toStdString();
    const QString scale = obj.value("scale").toString();
    EXPECT_TRUE(scale.contains("minor", Qt::CaseInsensitive));
    EXPECT_EQ(obj.value("fingerprint").toObject().value("rootNote").toInt(), 60);

    auto patterns = obj.value("patterns").toArray();
    ASSERT_GE(patterns.size(), 1) << "expected at least one repeated-bar pattern: " << text(r).toStdString();
    for (int i = 0; i < patterns.size(); ++i) {
        EXPECT_EQ(patterns[i].toObject().value("id").toString().toStdString(),
                  std::string("p") + std::to_string(i));
    }

    // Error path: missing file.
    EXPECT_TRUE(isError(call("analyze_midi_file", {{"path", "C:/nonexistent/x.mid"}})));

    juce::File(path.toStdString()).deleteFile();
}

// P2-4: scale_note degree math — rootC degree0 -> C, degree7 -> next octave,
// negative degrees wrap, known/unknown scale names, ranges.
TEST_F(McpCoverageTest, ScaleNoteMath)
{
    auto pitch = [&](const QJsonObject& args) -> int {
        auto r = call("scale_note", args);
        if (isError(r)) return -1000;
        auto obj = QJsonDocument::fromJson(text(r).toUtf8()).object();
        return obj.value("midiPitch").toInt(-1000);
    };
    // Base math.
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","major"}, {"degree",0}}), 60);   // C
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","major"}, {"degree",7}}), 72);   // next octave C
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","major"}, {"degree",8}}), 74);   // D, next octave
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","major"}, {"degree",-1}}), 59);  // B below C
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","major"}, {"degree",-7}}), 48);  // C, octave below
    EXPECT_EQ(pitch({{"rootMidi",62}, {"scale","minor"}, {"degree",0}}), 62);   // D minor root
    // Name forms: canonical full name and church-mode alias resolve too.
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","Major (Ionian)"}, {"degree",7}}), 72);
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","ionian"}, {"degree",7}}), 72);
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","aeolian"}, {"degree",0}}), 60);
    // octave param shifts by 12 * octave.
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","major"}, {"degree",0}, {"octave",1}}), 72);
    EXPECT_EQ(pitch({{"rootMidi",60}, {"scale","major"}, {"degree",0}, {"octave",-1}}), 48);
    // Errors: unknown scale, out-of-range rootMidi, pitch out of MIDI range.
    EXPECT_TRUE(isError(call("scale_note", {{"rootMidi",60}, {"scale","not-a-scale"}, {"degree",0}})));
    EXPECT_TRUE(isError(call("scale_note", {{"rootMidi",128}, {"scale","major"}, {"degree",0}})));
    EXPECT_TRUE(isError(call("scale_note", {{"rootMidi",-1}, {"scale","major"}, {"degree",0}})));
    EXPECT_TRUE(isError(call("scale_note", {{"rootMidi",100}, {"scale","major"}, {"degree",40}})));
    EXPECT_TRUE(isError(call("scale_note", {{"rootMidi",60}, {"scale","major"}, {"degree",-100}})));
}

// P2-5: mix_report reads the fixture WAV offline and reports whole-file +
// section metrics; errors on missing/invalid input.
TEST_F(McpCoverageTest, MixReportSections)
{
    QStringList toolNames;
    for (const auto& t2 : toolList())
        toolNames << t2.toObject().value("name").toString();
    EXPECT_TRUE(toolNames.contains("mix_report"));

    const QString wavPath = writeMixReportFixtureWav();
    ASSERT_FALSE(wavPath.isEmpty());

    QJsonArray sectionsArg{
        QJsonObject{{"name","A"}, {"start",0.0}, {"end",4.0}},
        QJsonObject{{"name","B"}, {"start",4.0}, {"end",8.0}}};
    auto r = call("mix_report", {{"filePath", wavPath}, {"bpm", 120.0}, {"sections", sectionsArg}});
    ASSERT_FALSE(isError(r)) << text(r).toStdString();
    auto obj = QJsonDocument::fromJson(text(r).toUtf8()).object();

    EXPECT_NEAR(obj.value("rms").toDouble(), std::sqrt(0.1175), 0.01);
    EXPECT_NEAR(obj.value("duration").toDouble(), 8.0, 1e-6);
    EXPECT_EQ(obj.value("sampleRate").toDouble(), 48000.0);
    EXPECT_EQ(obj.value("bandLabels").toArray().size(), 4);
    EXPECT_TRUE(obj.contains("pumpDepth")) << "bpm given, 8-beat sections -> pumpDepth expected";

    auto secs = obj.value("sections").toArray();
    ASSERT_EQ(secs.size(), 2) << text(r).toStdString();
    EXPECT_EQ(secs[0].toObject().value("name").toString(), "A");
    EXPECT_NEAR(secs[0].toObject().value("rms").toDouble(), std::sqrt(0.17), 0.01);
    EXPECT_NEAR(secs[1].toObject().value("rms").toDouble(), std::sqrt(0.065), 0.01);
    auto secA = secs[0].toObject().value("bandEnergy").toArray();
    EXPECT_GT(secA[0].toDouble(), secA[1].toDouble());   // sub > bass in A

    // Default: one "whole" section, pumpDepth omitted when no bpm.
    auto r2 = call("mix_report", {{"filePath", wavPath}});
    ASSERT_FALSE(isError(r2)) << text(r2).toStdString();
    auto obj2 = QJsonDocument::fromJson(text(r2).toUtf8()).object();
    auto secs2 = obj2.value("sections").toArray();
    ASSERT_EQ(secs2.size(), 1);
    EXPECT_EQ(secs2[0].toObject().value("name").toString(), "whole");
    EXPECT_FALSE(obj2.contains("pumpDepth"));

    // Error paths: missing file, bad section (end <= start).
    EXPECT_TRUE(isError(call("mix_report", {{"filePath", "C:/nonexistent/render.wav"}})));
    QJsonArray badSections{QJsonObject{{"name","bad"}, {"start",2.0}, {"end",1.0}}};
    EXPECT_TRUE(isError(call("mix_report", {{"filePath", wavPath}, {"sections", badSections}})));

    juce::File(wavPath.toStdString()).deleteFile();
}

// Task 3 (plan 2026-09-02-fx-chain-presets): save_fx_chain -> list_fx_chains
// -> load_fx_chain round-trip over MCP; the restored chain is asserted on the
// LIVE processor (Gate 2), not the ReadModel.
TEST_F(McpCoverageTest, FxChainPresetRoundTrip) {
    // Unique name per run (millisecond counter): concurrent/sequential runs
    // — and leftovers from interrupted runs — can never make the exact-name
    // lookup below resolve ambiguously.
    const QString chainName =
        QString("MCP Test Chain ") + QString::number(juce::Time::getMillisecondCounter());

    // Prologue: remove leftovers from a previous interrupted run so the
    // name lookup below resolves to exactly one preset.
    {
        auto existing = QJsonDocument::fromJson(
            callText("list_fx_chains", {}).toString().toUtf8()).array();
        for (const auto& v : existing) {
            auto o = v.toObject();
            if (o.value("name").toString() == chainName)
                EXPECT_FALSE(isError(call("delete_fx_chain", {{"id", o.value("id").toString()}})));
        }
    }

    auto r1 = call("add_fx", {{"trackId", 0}, {"fxType", "compressor"}});
    ASSERT_FALSE(isError(r1)) << text(r1).toStdString();

    auto r2 = call("save_fx_chain", {{"trackId", 0}, {"name", chainName}});
    ASSERT_FALSE(isError(r2)) << text(r2).toStdString();
    auto savedId = QJsonDocument::fromJson(text(r2).toUtf8()).object().value("id").toString();
    EXPECT_FALSE(savedId.isEmpty()) << text(r2).toStdString();

    auto listText = callText("list_fx_chains", {});
    auto arr = QJsonDocument::fromJson(listText.toString().toUtf8()).array();
    bool found = false;
    for (const auto& v : arr)
        if (v.toObject().value("name").toString() == chainName) found = true;
    EXPECT_TRUE(found) << listText.toString().toStdString();

    // NOTE: EXPECT (not ASSERT) from here on: the epilogue cleanup must run
    // on ALL paths, and an ASSERT_* early-return would skip it, leaking the
    // preset file into the real user chains dir.
    EXPECT_FALSE(isError(call("remove_fx", {{"trackId", 0}, {"slotIndex", 0}})));

    auto r4 = call("load_fx_chain", {{"trackId", 0}, {"name", chainName}});
    EXPECT_FALSE(isError(r4)) << text(r4).toStdString();

    // LIVE processor assertion (Gate 2/10): the rebuilt Track owns the slot.
    auto* track = engine->getMainProcessor()->getTrack(0);
    EXPECT_NE(track, nullptr);
    if (track != nullptr)
        EXPECT_EQ(track->getFXChain().size(), 1u);

    // Epilogue: leave no preset files behind (runs on all paths; sweeps by
    // name so it also covers a save that returned no id).
    {
        auto existing = QJsonDocument::fromJson(
            callText("list_fx_chains", {}).toString().toUtf8()).array();
        for (const auto& v : existing) {
            auto o = v.toObject();
            if (o.value("name").toString() == chainName)
                EXPECT_FALSE(isError(call("delete_fx_chain", {{"id", o.value("id").toString()}})));
        }
    }
}

} // namespace
