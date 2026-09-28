// MCP <-> RPC parity for slice S2 of docs/plans/2026-09-28-agent-mechanization.md:
// the beat-window archaeology pair query_notes / query_clips and their JSON-RPC
// twins read.queryNotes / read.queryClips.
//
// Both surfaces call ONE src/common shaper (common/ProjectQuery.h), so the
// strongest assertions available are the direct ones:
//   * the same argument object on both surfaces must return the SAME payload
//     (AGENTS.md: argument names are part of the contract), and
//   * every failure — missing startBeat, an inverted window, an unknown trackID —
//     must be the SAME -32602 with the SAME bytes.
//
// The semantics under test are INTERVAL OVERLAP in project beats, with a note's
// audible span clamped to its clip: a note that starts before the window but
// sustains into it is returned; a tail past the clip end is returned with
// endBeat CLIPPED and truncated=true; a note whose onset sits at/after the clip
// end never sounds and is never returned. The last case is the one that used to
// need four regex passes over the .hdaw XML.

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "common/ReadModel.h"
#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <algorithm>
#include <memory>
#include <string>

namespace {

class QueryNotesRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        auto& cmds = engine->getProjectCommands();
        ASSERT_GE(cmds.addTrack("Kick"), 0);   // TRACK_LIST index 0
        ASSERT_GE(cmds.addTrack("Bass"), 0);   // TRACK_LIST index 1
        ASSERT_GE(cmds.addTrack("Pad"), 0);    // TRACK_LIST index 2 (empty: the
                                               // wrong-track hazard's neighbour)

        // Kick clip [0,8) project beats — note startBeat/durationBeats are
        // CLIP-LOCAL (list_notes CONTRACT), the clip's own times are seconds.
        kickClipId = cmds.addMidiClip(0, 0.0, 8.0, "Kick Loop");
        ASSERT_GT(kickClipId, 0);
        nTailPastClipEnd = cmds.addNote(kickClipId, 60, 100, 0.0, 10.0);   // -> clipped at 8
        nInsideA        = cmds.addNote(kickClipId, 62,  90, 1.0,  1.0);
        nAfterSmallWin  = cmds.addNote(kickClipId, 64,  80, 6.0,  1.0);    // onset past a 7.0 window end
        nInsideB        = cmds.addNote(kickClipId, 65,  70, 3.0,  2.0);
        nBoundaryStart  = cmds.addNote(kickClipId, 67,  60, 2.5,  1.0);
        nLateOnset      = cmds.addNote(kickClipId, 68,  55, 7.5,  0.25);
        nHeadTrimmed    = cmds.addNote(kickClipId, 69,  50, -1.0, 2.0);    // starts before the clip
        nPastClipEnd    = cmds.addNote(kickClipId, 71,  45, 9.0,  1.0);    // onset >= clipEndB: never sounds
        ASSERT_GT(nTailPastClipEnd, 0);
        ASSERT_GT(nPastClipEnd, 0);

        bassClipId = cmds.addMidiClip(1, 8.0, 4.0, "Bass");               // [8,12) beats
        ASSERT_GT(bassClipId, 0);
        nBass = cmds.addNote(bassClipId, 40, 110, 0.0, 1.0);              // [8,9)
        ASSERT_GT(nBass, 0);

        engine->drainPendingRoutingRebuild();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    // --- MCP surface -------------------------------------------------------
    QJsonObject mcpResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject{{"name", tool}, {"arguments", args}})
            .toObject();
    }
    QString mcpText(const QString& tool, const QJsonObject& args) {
        const auto content = mcpResult(tool, args).value("content").toArray();
        return content.isEmpty() ? QString()
                                 : content[0].toObject().value("text").toString();
    }
    bool mcpIsError(const QString& tool, const QJsonObject& args) {
        return mcpResult(tool, args).value("isError").toBool();
    }
    QJsonValue mcpValue(const QString& tool, const QJsonObject& args) {
        const QString text = mcpText(tool, args);
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isArray()) return QJsonValue(doc.array());
        if (doc.isObject()) return QJsonValue(doc.object());
        return QJsonValue(text);
    }

    // --- RPC surface -------------------------------------------------------
    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }
    QJsonValue rpcPayload(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_FALSE(r.isError)
            << "RPC " << method.toStdString() << " errored: "
            << r.payload.toObject().value("message").toString().toStdString();
        return r.payload;
    }
    // One shared argument object reaching two surfaces must fail identically:
    // same -32602 code, same message bytes.
    void expectSameFailure(const QString& tool, const QString& method,
                           const QJsonObject& args) {
        const auto r = rpc(method, args);
        ASSERT_TRUE(r.isError) << "expected " << method.toStdString() << " to fail";
        EXPECT_TRUE(mcpIsError(tool, args)) << "expected " << tool.toStdString() << " to fail";
        const QString rpcMessage = r.payload.toObject().value("message").toString();
        const QString mcpMessage = mcpText(tool, args);
        EXPECT_FALSE(mcpMessage.isEmpty()) << "a failure must carry a reason";
        EXPECT_EQ(rpcMessage, mcpMessage);
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
    }

    // The row describing `noteId` in a payload, or {} when absent.
    static QJsonObject rowForNote(const QJsonObject& payload, int noteId) {
        for (const auto& v : payload.value("rows").toArray())
            if (v.toObject().value("noteId").toInt(-1) == noteId) return v.toObject();
        return {};
    }
    // The list_notes row for `noteId` (raw clip-local values, no clipping).
    QJsonObject listNotesRow(int clipId, int noteId) {
        const QJsonObject o = QJsonDocument::fromJson(
            mcpText("list_notes", QJsonObject{{"clipId", clipId}}).toUtf8()).object();
        for (const auto& v : o.value("notes").toArray())
            if (v.toObject().value("noteId").toInt(-1) == noteId) return v.toObject();
        return {};
    }

    int trackIDAt(int index) {
        return static_cast<int>(engine->getProjectModel().getTrackListTree()
                                    .getChild(index).getProperty(IDs::trackID, 0));
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;

    int kickClipId = -1, bassClipId = -1;
    int nTailPastClipEnd = -1, nInsideA = -1, nAfterSmallWin = -1, nInsideB = -1;
    int nBoundaryStart = -1, nLateOnset = -1, nHeadTrimmed = -1, nPastClipEnd = -1;
    int nBass = -1;
};

// (a) The same window answers identically on both surfaces, and the overlap
// semantics hold: N1 starts before the window and sustains into it; N4 and N5
// overlap; the note whose onset is past the window end (N6, 7.5) and the ones
// cut away by the clip (N4's neighbour N2 [1,2)) are excluded.
TEST_F(QueryNotesRpcTest, NoteWindowPayloadMatchesOnBothSurfaces) {
    const QJsonObject args{{"startBeat", 2.5}, {"endBeat", 7.0}};
    const QJsonValue viaMcp = mcpValue("query_notes", args);
    const QJsonValue viaRpc = rpcPayload("read.queryNotes", args);

    ASSERT_TRUE(viaMcp.isObject()) << mcpText("query_notes", args).toStdString();
    EXPECT_EQ(viaRpc, viaMcp) << "one shared shaper, so the payloads must be identical";
    EXPECT_EQ(viaMcp.toObject().value("unit").toString(), "beats");
    EXPECT_EQ(viaMcp.toObject().value("count").toInt(), 4);

    // The sustained note: starts at 0, sustains into the window, tail clamped
    // at the clip end.
    const QJsonObject sustained = rowForNote(viaMcp.toObject(), nTailPastClipEnd);
    ASSERT_FALSE(sustained.isEmpty());
    EXPECT_NEAR(sustained.value("absBeat").toDouble(), 0.0, 1e-6);
    EXPECT_NEAR(sustained.value("endBeat").toDouble(), 8.0, 1e-6);
    EXPECT_TRUE(sustained.value("truncated").toBool());
    EXPECT_NEAR(sustained.value("localBeat").toDouble(), 0.0, 1e-6);
    EXPECT_EQ(sustained.value("pitch").toInt(), 60);
    EXPECT_EQ(sustained.value("velocity").toInt(), 100);
    EXPECT_EQ(sustained.value("clipId").toInt(), kickClipId);
    EXPECT_EQ(sustained.value("trackIndex").toInt(), 0);
    EXPECT_EQ(sustained.value("trackID").toInt(), trackIDAt(0));

    // A fully-inside note is not truncated, and its local span is its own.
    const QJsonObject inside = rowForNote(viaMcp.toObject(), nInsideB);
    ASSERT_FALSE(inside.isEmpty());
    EXPECT_FALSE(inside.value("truncated").toBool());
    EXPECT_NEAR(inside.value("localBeat").toDouble(), 3.0, 1e-6);
    EXPECT_NEAR(inside.value("durationBeats").toDouble(), 2.0, 1e-6);

    // Excluded: onset after the window end (N6 at 7.5), inside the clip but
    // before the window (N2 at [1,2)), and the head-trimmed note whose [0,1)
    // span lies outside. The onset-inside-the-window notes are all present.
    EXPECT_TRUE(rowForNote(viaMcp.toObject(), nLateOnset).isEmpty());
    EXPECT_TRUE(rowForNote(viaMcp.toObject(), nInsideA).isEmpty());
    EXPECT_TRUE(rowForNote(viaMcp.toObject(), nHeadTrimmed).isEmpty());
    EXPECT_FALSE(rowForNote(viaMcp.toObject(), nAfterSmallWin).isEmpty());
    EXPECT_FALSE(rowForNote(viaMcp.toObject(), nBoundaryStart).isEmpty());
}

// Overlap, not containment: a window that starts AFTER the note's onset and
// before its end still returns it.
TEST_F(QueryNotesRpcTest, NoteStartingBeforeWindowIsReturned) {
    const QJsonObject args{{"startBeat", 5.0}, {"endBeat", 6.0}};
    const QJsonValue viaMcp = mcpValue("query_notes", args);
    const QJsonValue viaRpc = rpcPayload("read.queryNotes", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_EQ(viaMcp.toObject().value("count").toInt(), 1);
    const QJsonObject r = rowForNote(viaMcp.toObject(), nTailPastClipEnd);
    ASSERT_FALSE(r.isEmpty()) << "a note sustaining into the window must be returned";
    EXPECT_NEAR(r.value("absBeat").toDouble(), 0.0, 1e-6);
    EXPECT_TRUE(r.value("truncated").toBool());
}

// A note whose onset is at/after the clip end never sounds: even a window that
// covers exactly that absolute time returns nothing from the clip.
TEST_F(QueryNotesRpcTest, NoteOnsetPastClipEndIsNeverReturned) {
    const QJsonObject args{{"startBeat", 8.5}, {"endBeat", 12.0}};
    const QJsonValue viaAll = mcpValue("query_notes", args);
    EXPECT_TRUE(rowForNote(viaAll.toObject(), nPastClipEnd).isEmpty());
    // The only sounding row is the bass note on track 1 (abs [8,9)).
    ASSERT_EQ(viaAll.toObject().value("count").toInt(), 1);
    EXPECT_EQ(viaAll.toObject().value("rows").toArray()[0].toObject()
                  .value("noteId").toInt(), nBass);
}

// The track ref is the established B2 pair: `trackIndex` (position) or `trackID`
// (stable id), resolved by the SAME shared rule as every other track-addressing
// tool. Both forms filter to the same rows, and both surfaces agree.
TEST_F(QueryNotesRpcTest, TrackFilterMatchesOnBothSurfacesAndBothRefForms) {
    const int bassTrackID = trackIDAt(1);
    const QJsonObject byIndex{{"startBeat", 0.0}, {"endBeat", 12.0}, {"trackIndex", 1}};
    const QJsonObject byId{{"startBeat", 0.0}, {"endBeat", 12.0}, {"trackID", bassTrackID}};

    const QJsonValue mcpByIndex = mcpValue("query_notes", byIndex);
    EXPECT_EQ(rpcPayload("read.queryNotes", byIndex), mcpByIndex);
    EXPECT_EQ(mcpValue("query_notes", byId), mcpByIndex)
        << "the stable id names the same track as its position";
    EXPECT_EQ(rpcPayload("read.queryNotes", byId), mcpByIndex);
    EXPECT_EQ(mcpByIndex.toObject().value("count").toInt(), 1);
    EXPECT_EQ(mcpByIndex.toObject().value("rows").toArray()[0].toObject()
                  .value("noteId").toInt(), nBass);
}

// A NON-INTEGRAL track ref is refused IDENTICALLY on both surfaces, and never
// truncated onto a real track. The RPC surface used to static_cast<int>(1.5) ->
// track N (the wrong-track hazard this closes: 1.5 silently queried the track
// whose id/index is 1) while the MCP validator refused it. One shared
// integrality predicate (common/JsonInteger.h) now answers both with the
// validator's own bytes, and no track is queried at all.
TEST_F(QueryNotesRpcTest, FractionalTrackRefsRefusedIdenticallyWithoutQueryingATrack) {
    const QJsonObject fracId{{"startBeat", 0.0}, {"endBeat", 4.0}, {"trackID", 1.5}};
    expectSameFailure("query_notes", "read.queryNotes", fracId);
    EXPECT_EQ(mcpText("query_notes", fracId),
              QString("invalid params: trackID: expected integer"));
    // Refused, so it cannot have returned rows for ANY track (the truncation
    // used to answer for trackID 1).
    EXPECT_EQ(rpc("read.queryNotes", fracId).payload.toObject().value("code").toInt(), -32602);

    const QJsonObject fracIndex{{"startBeat", 0.0}, {"endBeat", 4.0}, {"trackIndex", 0.5}};
    expectSameFailure("query_notes", "read.queryNotes", fracIndex);
    EXPECT_EQ(mcpText("query_notes", fracIndex),
              QString("invalid params: trackIndex: expected integer"));

    // The INTEGRAL forms still return the same rows on both surfaces.
    const int bassTrackID = trackIDAt(1);
    const QJsonObject byIndex{{"startBeat", 0.0}, {"endBeat", 12.0}, {"trackIndex", 1}};
    const QJsonObject byId{{"startBeat", 0.0}, {"endBeat", 12.0}, {"trackID", bassTrackID}};
    const QJsonValue mcpByIndex = mcpValue("query_notes", byIndex);
    EXPECT_EQ(rpcPayload("read.queryNotes", byIndex), mcpByIndex);
    EXPECT_EQ(mcpValue("query_notes", byId), mcpByIndex)
        << "the stable id names the same track as its position";
    EXPECT_EQ(rpcPayload("read.queryNotes", byId), mcpByIndex);
    EXPECT_EQ(mcpByIndex.toObject().value("count").toInt(), 1);
}

// (c) Cross-consistency with the OTHER note-reading path: for every row the
// query returns, pitch/velocity must equal list_notes' values for that noteId,
// and localBeat must equal the note's own start clamped to the clip start (the
// head-trim rule), so truncation never silently diverges from list_notes.
TEST_F(QueryNotesRpcTest, RowsAgreeWithListNotes) {
    const QJsonObject args{{"startBeat", 0.0}, {"endBeat", 8.0}, {"trackIndex", 0}};
    const QJsonObject payload = mcpValue("query_notes", args).toObject();
    ASSERT_GT(payload.value("count").toInt(), 0);

    for (const auto& v : payload.value("rows").toArray()) {
        const QJsonObject row = v.toObject();
        const int noteId = row.value("noteId").toInt(-1);
        const QJsonObject raw = listNotesRow(kickClipId, noteId);
        ASSERT_FALSE(raw.isEmpty()) << "noteId " << noteId << " missing from list_notes";
        EXPECT_EQ(row.value("pitch").toInt(), raw.value("pitch").toInt());
        EXPECT_EQ(row.value("velocity").toInt(), raw.value("velocity").toInt());
        EXPECT_NEAR(row.value("localBeat").toDouble(),
                    std::max(0.0, raw.value("start").toDouble()), 1e-6);
    }
}

// LOOPING: a MIDI clip does NOT replay its content (MidiClipProcessor.h:152
// returns silence outside [startSec, startSec + durSec); the MIDI branch of
// RoutingManager never calls setLooping, RoutingManager.cpp:723-732), so
// occurrenceIndex stays 0 and a looping clip contributes nothing past its own
// interval.
TEST_F(QueryNotesRpcTest, LoopingMidiClipDoesNotReplayNotes) {
    engine->getProjectCommands().setClipLooping(kickClipId, true);
    const QJsonObject inside{{"startBeat", 0.0}, {"endBeat", 8.0}, {"trackIndex", 0}};
    const QJsonObject after{{"startBeat", 8.0}, {"endBeat", 24.0}, {"trackIndex", 0}};

    const QJsonObject inPayload = mcpValue("query_notes", inside).toObject();
    EXPECT_EQ(rpcPayload("read.queryNotes", inside), QJsonValue(inPayload));
    ASSERT_GT(inPayload.value("count").toInt(), 0);
    for (const auto& v : inPayload.value("rows").toArray())
        EXPECT_EQ(v.toObject().value("occurrenceIndex").toInt(-1), 0);

    const QJsonObject afterPayload = mcpValue("query_notes", after).toObject();
    EXPECT_EQ(afterPayload.value("count").toInt(), 0)
        << "a looping MIDI clip must not repeat its notes past its interval";
    EXPECT_EQ(rpcPayload("read.queryNotes", after), QJsonValue(afterPayload));
}

// (d) query_clips uses the same interval reasoning, and a clip STRADDLING the
// window boundary is returned (overlap, not containment).
TEST_F(QueryNotesRpcTest, ClipStraddlingWindowBoundaryIsReturned) {
    const QJsonObject straddle{{"startBeat", 7.5}, {"endBeat", 12.0}};
    const QJsonValue viaMcp = mcpValue("query_clips", straddle);
    const QJsonValue viaRpc = rpcPayload("read.queryClips", straddle);
    ASSERT_TRUE(viaMcp.isObject()) << mcpText("query_clips", straddle).toStdString();
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaMcp.toObject().value("unit").toString(), "beats");
    ASSERT_EQ(viaMcp.toObject().value("count").toInt(), 2);

    bool sawKick = false;
    for (const auto& v : viaMcp.toObject().value("rows").toArray()) {
        const QJsonObject r = v.toObject();
        EXPECT_TRUE(r.contains("clipId"));
        EXPECT_TRUE(r.contains("muted"));
        EXPECT_TRUE(r.contains("gain"));
        if (r.value("clipId").toInt() == kickClipId) {
            sawKick = true;
            // [0,8): starts BEFORE the window, hence a straddle, not containment.
            EXPECT_NEAR(r.value("startBeat").toDouble(), 0.0, 1e-6);
            EXPECT_NEAR(r.value("endBeat").toDouble(), 8.0, 1e-6);
            EXPECT_NEAR(r.value("durationBeats").toDouble(), 8.0, 1e-6);
            EXPECT_EQ(r.value("type").toString(), "midi");
            EXPECT_EQ(r.value("trackID").toInt(), trackIDAt(0));
        }
    }
    EXPECT_TRUE(sawKick) << "the clip straddling the boundary must be returned";

    // A window entirely past both clips returns nothing.
    const QJsonObject empty{{"startBeat", 20.0}, {"endBeat", 24.0}};
    const QJsonValue emptyMcp = mcpValue("query_clips", empty);
    EXPECT_EQ(emptyMcp.toObject().value("count").toInt(), 0);
    EXPECT_EQ(rpcPayload("read.queryClips", empty), emptyMcp);
}

// (b) Failure parity: identical -32602 text on both surfaces, from ONE place.
TEST_F(QueryNotesRpcTest, FailureTextsMatchOnBothSurfaces) {
    // Missing window argument — the shared reader words it, so the tool does NOT
    // go through the MCP schema validator's different "invalid params" text.
    expectSameFailure("query_notes", "read.queryNotes", QJsonObject{{"endBeat", 4.0}});
    EXPECT_EQ(mcpText("query_notes", QJsonObject{{"endBeat", 4.0}}),
              QString("missing or non-numeric param: startBeat"));
    expectSameFailure("query_notes", "read.queryNotes", QJsonObject{{"startBeat", 0.0}});
    EXPECT_EQ(mcpText("query_notes", QJsonObject{{"startBeat", 0.0}}),
              QString("missing or non-numeric param: endBeat"));

    // Inverted / empty window — the shared builder's text.
    const QJsonObject inverted{{"startBeat", 4.0}, {"endBeat", 0.0}};
    expectSameFailure("query_notes", "read.queryNotes", inverted);
    EXPECT_EQ(mcpText("query_notes", inverted),
              QString("beat window invalid: endBeat must be greater than startBeat"));
    const QJsonObject empty{{"startBeat", 2.0}, {"endBeat", 2.0}};
    expectSameFailure("query_notes", "read.queryNotes", empty);

    // Unknown stable id — the shared B2 resolver names it.
    const QJsonObject unknownRef{{"startBeat", 0.0}, {"endBeat", 4.0}, {"trackID", 4242}};
    expectSameFailure("query_notes", "read.queryNotes", unknownRef);
    EXPECT_EQ(mcpText("query_notes", unknownRef), QString("unknown trackID 4242"));

    // The same three failure classes on the clips twin.
    expectSameFailure("query_clips", "read.queryClips", QJsonObject{{"endBeat", 4.0}});
    expectSameFailure("query_clips", "read.queryClips", inverted);
    const QJsonObject clipsEmpty{{"startBeat", 9.0}, {"endBeat", 9.0}};
    expectSameFailure("query_clips", "read.queryClips", clipsEmpty);
    EXPECT_EQ(mcpText("query_clips", clipsEmpty),
              QString("beat window invalid: endBeat must be greater than startBeat"));
}

// STEP 0 (S2b) — a clip whose tree LACKS IDs::gain must read the semantic
// DEFAULT (1.0) on every surface, not 0.0. The bug was
// `static_cast<double>(getProperty(IDs::gain))` (-> 0.0 for an absent property)
// in the query_clips shaper (common/ProjectQuery.cpp) and in list_clips /
// get_clip (McpTools_Read.cpp). ReadModelImpl.cpp:57 already read
// getProperty(IDs::gain, 1.0), so read.snapshot (the ledger twin of list_clips;
// snapshot_project's payload) was correct — asserted here so the three readers
// can never disagree.
TEST_F(QueryNotesRpcTest, ClipWithoutGainPropertyReadsSemanticDefaultOnEverySurface) {
    juce::ValueTree clip;
    auto trackList = engine->getProjectModel().getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren() && !clip.isValid(); ++t) {
        auto cl = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
        for (int i = 0; i < cl.getNumChildren(); ++i)
            if (static_cast<int>(cl.getChild(i).getProperty(IDs::clipID, 0)) == kickClipId) {
                clip = cl.getChild(i);
                break;
            }
    }
    ASSERT_TRUE(clip.isValid());
    clip.removeProperty(IDs::gain, nullptr);   // legacy / hand-authored tree
    ASSERT_FALSE(clip.hasProperty(IDs::gain));

    // (1) query_clips — the shared shaper.
    const QJsonObject win{{"startBeat", 0.0}, {"endBeat", 8.0}};
    const QJsonObject q = mcpValue("query_clips", win).toObject();
    bool sawKick = false;
    for (const auto& v : q.value("rows").toArray())
        if (v.toObject().value("clipId").toInt() == kickClipId) {
            sawKick = true;
            EXPECT_DOUBLE_EQ(v.toObject().value("gain").toDouble(), 1.0);
        }
    EXPECT_TRUE(sawKick);

    // (2) list_clips.
    bool sawList = false;
    for (const auto& v : QJsonDocument::fromJson(
             mcpText("list_clips", QJsonObject{}).toUtf8()).array())
        if (v.toObject().value("id").toInt() == kickClipId) {
            sawList = true;
            EXPECT_DOUBLE_EQ(v.toObject().value("gain").toDouble(), 1.0);
        }
    EXPECT_TRUE(sawList);

    // (3) read.snapshot (ReadModel path; carries clip gain under "clips").
    bool sawSnapshot = false;
    const auto r = frontend::dispatch(*engine, "read.snapshot", QJsonObject{});
    ASSERT_FALSE(r.isError);
    for (const auto& v : r.payload.toObject().value("clips").toArray())
        if (v.toObject().value("clipId").toInt() == kickClipId) {
            sawSnapshot = true;
            EXPECT_DOUBLE_EQ(v.toObject().value("gain").toDouble(), 1.0);
        }
    EXPECT_TRUE(sawSnapshot);
}

} // namespace
