// MCP <-> RPC parity for slice S3 of docs/plans/2026-09-28-agent-mechanization.md:
// the two BATCH mutation verbs and their JSON-RPC twins —
//   set_notes_gain {noteIds:[...], gain}          <-> project.setNotesGain
//   set_clips_edit {edits:[{clipId, ...}, ...]}   <-> project.setClipsEdit
//
// Both surfaces call ONE ProjectCommands entry point (setNotesGain /
// setClipsEdit), so the strongest assertions available are the direct ones:
//   * the same argument object on both surfaces returns the SAME payload
//     (AGENTS.md: argument names are part of the contract);
//   * every failure is the SAME -32602 with the SAME bytes — the shared
//     refusal texts ("noteIds must not be empty" / "edits must not be empty",
//     "unknown noteId N" / "unknown clipId N") and, for a typo'd edit key, the
//     MCP validator's own wording (src/common/BatchEditJson.h reproduces it);
//   * the batch is ONE undo unit: a single undo restores EVERY value (a loop of
//     single-item calls could not pass this with one undo), and a REFUSED batch
//     writes nothing AND opens no undo unit (validate-then-apply).

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <juce_core/juce_core.h>

#include "common/ReadModel.h"
#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
// The integer-argument helper under test (requireInt), for the float-instantiation
// regression guard.
#include "frontend/router/RouterHelpers.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class BatchEditRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        auto& cmds = engine->getProjectCommands();
        ASSERT_GE(cmds.addTrack("Kick"), 0);   // TRACK_LIST index 0
        ASSERT_GE(cmds.addTrack("Bass"), 0);   // TRACK_LIST index 1
        clipA = cmds.addMidiClip(0, 0.0, 4.0, "A");
        clipB = cmds.addMidiClip(1, 4.0, 4.0, "B");
        ASSERT_GT(clipA, 0);
        ASSERT_GT(clipB, 0);
        n1 = cmds.addNote(clipA, 60, 100, 0.0, 1.0);
        n2 = cmds.addNote(clipA, 62, 100, 1.0, 1.0);
        n3 = cmds.addNote(clipB, 40, 100, 0.0, 1.0);
        ASSERT_GT(n1, 0);
        ASSERT_GT(n2, 0);
        ASSERT_GT(n3, 0);
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
    // Compact JSON text on success, a bare message on failure — parse like the
    // MCP client does.
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

    // --- Tree readers (the command layer's own write path) ------------------
    static juce::ValueTree clipTreeIn(const juce::ValueTree& trackList, int clipId) {
        for (int t = 0; t < trackList.getNumChildren(); ++t) {
            auto cl = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
            for (int i = 0; i < cl.getNumChildren(); ++i)
                if (static_cast<int>(cl.getChild(i).getProperty(IDs::clipID, 0)) == clipId)
                    return cl.getChild(i);
        }
        return {};
    }
    juce::ValueTree clipTree(int clipId) {
        return clipTreeIn(engine->getProjectModel().getTrackListTree(), clipId);
    }
    double clipProp(int clipId, const juce::Identifier& id, double fallback = -999.0) {
        auto c = clipTree(clipId);
        return c.isValid() ? static_cast<double>(c.getProperty(id, fallback)) : fallback;
    }
    double noteGain(int clipId, int noteId) {
        auto nl = clipTree(clipId).getChildWithName(IDs::MIDI_NOTE_LIST);
        for (int i = 0; i < nl.getNumChildren(); ++i)
            if (static_cast<int>(nl.getChild(i).getProperty(IDs::noteID, 0)) == noteId)
                // An absent noteGain reads the SEMANTIC default 1.0, the same
                // default list_notes reports (McpTools_Note.cpp:342).
                return static_cast<double>(nl.getChild(i).getProperty(IDs::noteGain, 1.0));
        return -999.0;   // note not found
    }
    int undoDepth() {
        return static_cast<int>(engine->getProjectCommands().getUndoDescriptions().size());
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    int clipA = -1, clipB = -1, n1 = -1, n2 = -1, n3 = -1;
};

// (a) set_notes_gain — identical payload on both surfaces for the same args.
TEST_F(BatchEditRpcTest, SetNotesGainPayloadMatchesOnBothSurfaces) {
    const QJsonObject args{{"noteIds", QJsonArray{n1, n3}}, {"gain", 0.5}};
    const QJsonValue viaMcp = mcpValue("set_notes_gain", args);
    const QJsonValue viaRpc = rpcPayload("project.setNotesGain", args);
    EXPECT_EQ(viaRpc, viaMcp) << "one shared command, so the payloads must be identical";
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_TRUE(viaMcp.toObject().value("ok").toBool());
    EXPECT_EQ(viaMcp.toObject().value("applied").toInt(), 2);
    EXPECT_DOUBLE_EQ(noteGain(clipA, n1), 0.5);
    EXPECT_DOUBLE_EQ(noteGain(clipB, n3), 0.5);
    EXPECT_DOUBLE_EQ(noteGain(clipA, n2), 1.0) << "an unlisted note is untouched";
}

// (a) set_clips_edit — identical payload on both surfaces for the same args.
TEST_F(BatchEditRpcTest, SetClipsEditPayloadMatchesOnBothSurfaces) {
    const QJsonObject args{{"edits", QJsonArray{
        QJsonObject{{"clipId", clipA}, {"gain", 0.3}},
        QJsonObject{{"clipId", clipB}, {"gain", 0.7}}}}};
    const QJsonValue viaMcp = mcpValue("set_clips_edit", args);
    const QJsonValue viaRpc = rpcPayload("project.setClipsEdit", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_EQ(viaMcp.toObject().value("applied").toInt(), 2);
    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::gain), 0.3);
    EXPECT_DOUBLE_EQ(clipProp(clipB, IDs::gain), 0.7);
}

// (b) ONE UNDO UNIT, proven directly: three notes across TWO clips change, then
// a SINGLE undo restores EVERY value — a loop of single-item calls could not.
TEST_F(BatchEditRpcTest, SetNotesGainIsOneUndoUnit) {
    const double before1 = noteGain(clipA, n1);
    const double before2 = noteGain(clipA, n2);
    const double before3 = noteGain(clipB, n3);
    const int depth = undoDepth();

    rpcPayload("project.setNotesGain", QJsonObject{
        {"noteIds", QJsonArray{n1, n2, n3}}, {"gain", 0.25}});
    EXPECT_EQ(undoDepth(), depth + 1) << "the batch must be exactly one transaction";
    EXPECT_DOUBLE_EQ(noteGain(clipA, n1), 0.25);
    EXPECT_DOUBLE_EQ(noteGain(clipA, n2), 0.25);
    EXPECT_DOUBLE_EQ(noteGain(clipB, n3), 0.25);

    engine->getProjectCommands().undo();   // ONE undo
    EXPECT_DOUBLE_EQ(noteGain(clipA, n1), before1);
    EXPECT_DOUBLE_EQ(noteGain(clipA, n2), before2);
    EXPECT_DOUBLE_EQ(noteGain(clipB, n3), before3);
}

// (b) ONE UNDO UNIT for set_clips_edit: two clips, two properties each, one undo.
TEST_F(BatchEditRpcTest, SetClipsEditIsOneUndoUnit) {
    const double aStart = clipProp(clipA, IDs::startTime);
    const double aGain = clipProp(clipA, IDs::gain);
    const double bStart = clipProp(clipB, IDs::startTime);
    const double bGain = clipProp(clipB, IDs::gain);
    const int depth = undoDepth();

    rpcPayload("project.setClipsEdit", QJsonObject{{"edits", QJsonArray{
        QJsonObject{{"clipId", clipA}, {"start", 2.0}, {"gain", 0.4}},
        QJsonObject{{"clipId", clipB}, {"start", 6.0}, {"gain", 0.8}}}}});
    EXPECT_EQ(undoDepth(), depth + 1);
    EXPECT_NE(clipProp(clipA, IDs::startTime), aStart);
    EXPECT_NE(clipProp(clipB, IDs::startTime), bStart);
    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::gain), 0.4);
    EXPECT_DOUBLE_EQ(clipProp(clipB, IDs::gain), 0.8);

    engine->getProjectCommands().undo();   // ONE undo
    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::startTime), aStart);
    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::gain), aGain);
    EXPECT_DOUBLE_EQ(clipProp(clipB, IDs::startTime), bStart);
    EXPECT_DOUBLE_EQ(clipProp(clipB, IDs::gain), bGain);
}

// (e) a PARTIAL edit touches only the named field; every other clip property is
// left alone.
TEST_F(BatchEditRpcTest, SetClipsEditPartialEditLeavesOtherFieldsUntouched) {
    auto& c = engine->getProjectCommands();
    c.setClipLooping(clipA, true);
    c.setClipFadeIn(clipA, 0.5);
    c.setClipFadeOut(clipA, 0.25);
    c.setClipName(clipA, "keep-me");
    const double start = clipProp(clipA, IDs::startTime);
    const double dur = clipProp(clipA, IDs::duration);

    rpcPayload("project.setClipsEdit", QJsonObject{{"edits", QJsonArray{
        QJsonObject{{"clipId", clipA}, {"gain", 0.15}}}}});

    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::gain), 0.15);
    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::startTime), start);
    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::duration), dur);
    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::fadeIn), 0.5);
    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::fadeOut), 0.25);
    EXPECT_TRUE(static_cast<bool>(clipTree(clipA).getProperty(IDs::looping)));
    EXPECT_EQ(clipTree(clipA).getProperty(IDs::name).toString().toStdString(), "keep-me");
}

// (c) Failure parity: identical -32602 text on both surfaces, from ONE place.
TEST_F(BatchEditRpcTest, FailureTextsMatchOnBothSurfaces) {
    // Unknown note id — the shared command text names it.
    const QJsonObject badNote{{"noteIds", QJsonArray{n1, 999999}}, {"gain", 0.5}};
    expectSameFailure("set_notes_gain", "project.setNotesGain", badNote);
    EXPECT_EQ(mcpText("set_notes_gain", badNote), QString("unknown noteId 999999"));

    // Unknown clip id.
    const QJsonObject badClip{{"edits", QJsonArray{
        QJsonObject{{"clipId", 999999}, {"gain", 0.5}}}}};
    expectSameFailure("set_clips_edit", "project.setClipsEdit", badClip);
    EXPECT_EQ(mcpText("set_clips_edit", badClip), QString("unknown clipId 999999"));

    // Empty noteIds — an ERROR, never a silent no-op.
    const QJsonObject emptyNotes{{"noteIds", QJsonArray{}}, {"gain", 0.5}};
    expectSameFailure("set_notes_gain", "project.setNotesGain", emptyNotes);
    EXPECT_EQ(mcpText("set_notes_gain", emptyNotes), QString("noteIds must not be empty"));

    // Empty edits.
    const QJsonObject emptyEdits{{"edits", QJsonArray{}}};
    expectSameFailure("set_clips_edit", "project.setClipsEdit", emptyEdits);
    EXPECT_EQ(mcpText("set_clips_edit", emptyEdits), QString("edits must not be empty"));

    // An edit with an unknown key is REJECTED (additionalProperties:false), and
    // the route's shared parser answers with the MCP validator's exact bytes.
    const QJsonObject bogusKey{{"edits", QJsonArray{
        QJsonObject{{"clipId", clipA}, {"bogus", 1}}}}};
    expectSameFailure("set_clips_edit", "project.setClipsEdit", bogusKey);
    EXPECT_EQ(mcpText("set_clips_edit", bogusKey),
              QString("invalid params: edits[0].bogus: unknown property"));
}

// (d) A refused batch leaves every value unchanged AND adds no undo unit
// (validate-then-apply: no transaction is even opened).
TEST_F(BatchEditRpcTest, RefusedBatchWritesNothingAndAddsNoUndoUnit) {
    const double gain1 = noteGain(clipA, n1);
    const double gain2 = noteGain(clipA, n2);
    const int depth = undoDepth();

    // n1 is valid, 999999 is not: NOTHING may be written.
    const QJsonObject bad{{"noteIds", QJsonArray{n1, 999999}}, {"gain", 0.01}};
    EXPECT_TRUE(rpc("project.setNotesGain", bad).isError);
    EXPECT_TRUE(mcpIsError("set_notes_gain", bad));
    EXPECT_DOUBLE_EQ(noteGain(clipA, n1), gain1) << "a refused batch must not half-apply";
    EXPECT_DOUBLE_EQ(noteGain(clipA, n2), gain2);
    EXPECT_EQ(undoDepth(), depth) << "a refused batch must not open an undo unit";

    const double clipGain = clipProp(clipA, IDs::gain);
    const QJsonObject badClip{{"edits", QJsonArray{
        QJsonObject{{"clipId", clipA}, {"gain", 0.01}},
        QJsonObject{{"clipId", 999999}, {"gain", 0.02}}}}};
    EXPECT_TRUE(rpc("project.setClipsEdit", badClip).isError);
    EXPECT_TRUE(mcpIsError("set_clips_edit", badClip));
    EXPECT_DOUBLE_EQ(clipProp(clipA, IDs::gain), clipGain) << "a refused batch must not half-apply";
    EXPECT_EQ(undoDepth(), depth);
}

// (f) A NON-INTEGRAL id in the batch is refused IDENTICALLY on both surfaces: the
// RPC route used to TRUNCATE 1.5 to note 1 (mutating it) while the MCP validator
// refused it. The refusal must write nothing and open no undo unit, and — the
// argument names being the contract — the bytes must be the validator's own.
TEST_F(BatchEditRpcTest, NonIntegralNoteIdsRefusedIdenticallyWithoutMutation) {
    // Aim the truncation at a REAL note: 1.5 would have become n1's id.
    const QJsonObject args{{"noteIds", QJsonArray{static_cast<double>(n1) + 0.5}},
                           {"gain", 0.5}};
    const double before1 = noteGain(clipA, n1);
    const double before2 = noteGain(clipA, n2);
    const int depth = undoDepth();

    expectSameFailure("set_notes_gain", "project.setNotesGain", args);
    EXPECT_EQ(mcpText("set_notes_gain", args),
              QString("invalid params: noteIds[0]: expected integer"));
    EXPECT_DOUBLE_EQ(noteGain(clipA, n1), before1)
        << "a refused id must not be truncated onto n1";
    EXPECT_DOUBLE_EQ(noteGain(clipA, n2), before2);
    EXPECT_EQ(undoDepth(), depth) << "a refused batch must not open an undo unit";

    // The non-array case answers with the validator's wording too, so the two
    // surfaces cannot diverge on the argument's declared SHAPE either.
    const QJsonObject notArray{{"noteIds", "x"}, {"gain", 0.5}};
    expectSameFailure("set_notes_gain", "project.setNotesGain", notArray);
    EXPECT_EQ(mcpText("set_notes_gain", notArray),
              QString("invalid params: noteIds: expected array"));
}

// (f) The SCALAR twin: one non-integral noteId, refused with the same bytes on
// both surfaces and with no mutation/an undo unit.
TEST_F(BatchEditRpcTest, NonIntegralScalarNoteIdRefusedIdentically) {
    const QJsonObject args{{"noteId", static_cast<double>(n1) + 0.5}, {"gain", 0.5}};
    const double before = noteGain(clipA, n1);
    const int depth = undoDepth();

    expectSameFailure("set_note_gain", "project.setNoteGain", args);
    EXPECT_EQ(mcpText("set_note_gain", args),
              QString("invalid params: noteId: expected integer"));
    EXPECT_DOUBLE_EQ(noteGain(clipA, n1), before);
    EXPECT_EQ(undoDepth(), depth);

    // One shared helper, so the sibling scalar route refuses identically.
    const QJsonObject panArgs{{"noteId", static_cast<double>(n1) + 0.5}, {"pan", 0.25}};
    expectSameFailure("set_note_pan", "project.setNotePan", panArgs);
    EXPECT_EQ(mcpText("set_note_pan", panArgs),
              QString("invalid params: noteId: expected integer"));
    EXPECT_EQ(undoDepth(), depth);
}

// (g) An INTEGRAL id array still works on both surfaces: the strict parser is a
// refusal of non-integers, not of ids.
TEST_F(BatchEditRpcTest, IntegralNoteIdsStillAcceptedOnBothSurfaces) {
    const QJsonObject args{{"noteIds", QJsonArray{n2}}, {"gain", 0.75}};
    const QJsonValue viaMcp = mcpValue("set_notes_gain", args);
    const QJsonValue viaRpc = rpcPayload("project.setNotesGain", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_TRUE(viaMcp.toObject().value("ok").toBool());
    EXPECT_EQ(viaMcp.toObject().value("applied").toInt(), 1);
    EXPECT_DOUBLE_EQ(noteGain(clipA, n2), 0.75);
}

// (g2) Bug fix (2026-10-05): set_notes_gain's gain is CLAMPED to the per-note
// range 0.0..2.0 with a VISIBLE report. The MCP surface's schema validator refuses
// an out-of-range gain before the handler (so no clamp runs there); the RPC route
// has no validator, so the shared clamp/report helper closes the silent
// pass-through — and both surfaces that CAN reach the handler put the identical
// clamp report in a `clamp` field (asserted on the RPC surface directly).
TEST_F(BatchEditRpcTest, SetNotesGainClampsOutOfRangeGainWithReport) {
    const int depth = undoDepth();

    // RPC: 4 is clamped to 2 and the clamp is reported, not swallowed.
    const auto r = rpc("project.setNotesGain",
                       QJsonObject{{"noteIds", QJsonArray{n1}}, {"gain", 4.0}});
    ASSERT_FALSE(r.isError)
        << r.payload.toObject().value("message").toString().toStdString();
    ASSERT_TRUE(r.payload.isObject());
    const QJsonObject o = r.payload.toObject();
    EXPECT_TRUE(o.value("ok").toBool());
    EXPECT_EQ(o.value("applied").toInt(), 1);
    EXPECT_EQ(o.value("clamp").toString(), QString("(gain clamped: 4 -> 2)"))
        << "an out-of-range gain must be reported, not silently accepted";
    EXPECT_DOUBLE_EQ(noteGain(clipA, n1), 2.0) << "the write must use the clamped value";
    EXPECT_EQ(undoDepth(), depth + 1) << "the clamped batch is still exactly one unit";

    // An IN-RANGE value is byte-identical to before: no clamp report.
    const auto ok = rpc("project.setNotesGain",
                        QJsonObject{{"noteIds", QJsonArray{n2}}, {"gain", 0.5}});
    ASSERT_FALSE(ok.isError);
    EXPECT_EQ(ok.payload,
              QJsonValue(QJsonObject{{"applied", 1}, {"ok", true}}))
        << "a non-clamped write must not gain a clamp field";
}

// (h) Regression guard for the guard: requireInt's integrality rejection is
// gated on std::is_integral<T>, so a floating instantiation still takes a
// fractional value — and a fractional-valued route still accepts its value.
TEST_F(BatchEditRpcTest, FractionalValuesStillPassThroughTheIntegerHelper) {
    frontend::DispatchResult err;
    float f = 0.0f;
    EXPECT_TRUE(frontend::router_helpers::requireInt<float>(
        QJsonObject{{"volume", 0.5}}, "volume", f, &err))
        << "a floating instantiation must not reject a fractional value";
    EXPECT_FLOAT_EQ(f, 0.5f);
    EXPECT_FALSE(err.isError);

    // End-to-end: a fractional fader value is still accepted by a real route.
    rpcPayload("project.setClipGain", QJsonObject{{"clipId", clipA}, {"gain", 0.4}});
    EXPECT_NEAR(clipProp(clipA, IDs::gain), 0.4, 1e-6);
}

// (f) The handler path the MCP validator does NOT pre-empt (`noteIds` is not in
// the tool's `required` list): an absent/ill-typed noteIds answers identically on
// both surfaces, and both name noteIds BEFORE gain so a doubly-wrong request
// cannot blame a different argument on each surface.
TEST_F(BatchEditRpcTest, MissingNoteIdsAnswersIdenticallyOnBothSurfaces) {
    const QJsonObject noIds{{"gain", 0.5}};
    expectSameFailure("set_notes_gain", "project.setNotesGain", noIds);
    EXPECT_EQ(mcpText("set_notes_gain", noIds),
              QString("invalid params: noteIds: expected array"));

    const QJsonObject bothWrong{{"noteIds", "x"}};
    expectSameFailure("set_notes_gain", "project.setNotesGain", bothWrong);
    EXPECT_EQ(mcpText("set_notes_gain", bothWrong),
              QString("invalid params: noteIds: expected array"));
}

// ---------------------------------------------------------------------------
// S7 EDIT BATCH (begin_batch / end_batch). While a batch is open, EVERY undo
// boundary a command draws — its own begin/endTransaction pair AND every
// internal transaction it opens — is suppressed (the command layer's ONE choke
// point, AudioEngineCommands::transactionBoundary), so the whole batch is ONE
// named undo unit. The batch is engine-global, one-at-a-time, and gated to the
// stdio transport (it owns the process-wide undo transaction).
// ---------------------------------------------------------------------------

// (a) NO-BATCH EQUIVALENCE, the guard on the flag design: with no batch open
// today's undo granularity is byte-for-byte unchanged — each command is its own
// unit, and the deliberately-UNPAIRED createBus -> createSend pair still
// coalesces into ONE (createSend joins createBus's unit).
TEST_F(BatchEditRpcTest, NoBatchBehaviourIsUnchanged) {
    auto& c = engine->getProjectCommands();
    EXPECT_FALSE(c.batchActive());

    // (i) duplicateClips is ONE undo unit.
    int depth = undoDepth();
    c.duplicateClips({clipA}, {8.0}, {0});
    EXPECT_EQ(undoDepth(), depth + 1) << "duplicateClips must stay ONE undo unit";
    c.undo();
    EXPECT_EQ(undoDepth(), depth);

    // (ii) createBus then createSend is ONE undo unit (the unpaired family —
    // createSend deliberately joins createBus's unit).
    depth = undoDepth();
    auto bus = c.createBus("group", "Group Bus", "", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    auto send = c.createSend(0, bus.busID, 1.0f, false);
    ASSERT_TRUE(send.ok) << send.error;
    EXPECT_EQ(undoDepth(), depth + 1)
        << "createBus + createSend must stay ONE undo step (no batch involved)";
    c.undo();
    EXPECT_EQ(undoDepth(), depth);

    // (iii) a standalone command is its own unit.
    depth = undoDepth();
    c.removeClips({clipB});
    EXPECT_EQ(undoDepth(), depth + 1) << "a standalone command is still its own unit";
    c.undo();
}

// (b) COLLAPSE: with a batch open, three commands that would EACH draw their own
// undo boundary — createBus (a raw boundary), duplicateClips (a paired
// boundary), removeClips (a paired boundary) — collapse into ONE undo unit, so a
// single undo restores every edit. Before the change this test FAILS: without the
// suppression each boundary splits the group (3 undos would be needed).
TEST_F(BatchEditRpcTest, BatchCollapsesInternallyTransactionalCommandsIntoOneUndo) {
    server->setTransportName("stdio");
    auto& c = engine->getProjectCommands();
    const int depth = undoDepth();

    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "session"}}));
    ASSERT_TRUE(c.batchActive());

    auto bus = c.createBus("group", "Group Bus", "", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    const std::vector<int> dups = c.duplicateClips({clipA}, {8.0}, {0});
    ASSERT_EQ(dups.size(), 1u);
    const int dup = dups.front();
    ASSERT_GT(dup, 0);
    c.removeClips({clipB});
    EXPECT_FALSE(clipTree(clipB).isValid()) << "clipB should be gone before end_batch";

    ASSERT_FALSE(mcpIsError("end_batch", QJsonObject{}));
    EXPECT_FALSE(c.batchActive());
    EXPECT_EQ(undoDepth(), depth + 1)
        << "the whole batch must be exactly ONE undo unit";

    c.undo();   // ONE undo restores ALL THREE edits
    EXPECT_TRUE(clipTree(clipB).isValid()) << "one undo must restore the removed clip";
    EXPECT_FALSE(clipTree(dup).isValid()) << "one undo must remove the duplicate";

    bool busFound = false;
    auto bl = engine->getProjectModel().getBusListTree();
    for (int i = 0; i < bl.getNumChildren(); ++i)
        if (static_cast<int>(bl.getChild(i).getProperty(IDs::busID, -1)) == bus.busID)
            busFound = true;
    EXPECT_FALSE(busFound) << "one undo must remove the created bus";
}

// (c) EXIT: after end_batch the suppression is gone — the next command forms its
// own unit again (no residual batch state).
TEST_F(BatchEditRpcTest, CommandAfterEndBatchFormsItsOwnUnit) {
    server->setTransportName("stdio");
    auto& c = engine->getProjectCommands();
    const int depth = undoDepth();

    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "b"}}));
    c.removeClips({clipB});
    ASSERT_FALSE(mcpIsError("end_batch", QJsonObject{}));
    EXPECT_EQ(undoDepth(), depth + 1);

    const int after = undoDepth();
    c.duplicateClips({clipA}, {12.0}, {0});
    EXPECT_EQ(undoDepth(), after + 1) << "no residual suppression after end_batch";
}

// (d) REFUSALS carry the shared bytes and change nothing: the transport gate
// (a batch owns the process-wide undo transaction, so only a dedicated stdio
// engine may open one), the one-at-a-time rule, and end_batch with none open.
TEST_F(BatchEditRpcTest, BatchRefusalsShareTheExactTextAndTouchNothing) {
    auto& c = engine->getProjectCommands();
    const int depth = undoDepth();

    // (1) transport gate — the default test server is not stdio.
    server->setTransportName("http");
    const QJsonObject open{{"name", "x"}};
    EXPECT_TRUE(mcpIsError("begin_batch", open));
    EXPECT_EQ(mcpText("begin_batch", open),
              QString("begin_batch requires the stdio transport (current: http) - a batch "
                      "owns the process-wide undo transaction"));
    EXPECT_FALSE(c.batchActive());
    EXPECT_EQ(undoDepth(), depth) << "a refused begin_batch opens no undo unit";

    // (2) one at a time: on stdio a second begin_batch is refused naming the open one.
    server->setTransportName("stdio");
    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "first"}}));
    EXPECT_TRUE(c.batchActive());
    EXPECT_EQ(c.batchName(), std::string("first"));
    EXPECT_TRUE(mcpIsError("begin_batch", QJsonObject{{"name", "second"}}));
    EXPECT_EQ(mcpText("begin_batch", QJsonObject{{"name", "second"}}),
              QString("a batch is already open (name \"first\") - call end_batch first"));
    EXPECT_EQ(c.batchName(), std::string("first")) << "the first batch keeps ownership";
    ASSERT_FALSE(mcpIsError("end_batch", QJsonObject{}));

    // (3) end_batch with no open batch.
    EXPECT_FALSE(c.batchActive());
    EXPECT_TRUE(mcpIsError("end_batch", QJsonObject{}));
    EXPECT_EQ(mcpText("end_batch", QJsonObject{}), QString("no open batch"));
    EXPECT_EQ(undoDepth(), depth) << "a refused end_batch adds no undo unit";
}

// (f) A command FAILURE inside a batch does not close it: the batch keeps owning
// the process-wide transaction until the caller explicitly calls end_batch.
TEST_F(BatchEditRpcTest, FailedCommandInsideBatchLeavesTheBatchOpen) {
    server->setTransportName("stdio");
    auto& c = engine->getProjectCommands();
    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "faults"}}));

    const QJsonObject bad{{"edits", QJsonArray{
        QJsonObject{{"clipId", 999999}, {"gain", 0.5}}}}};
    EXPECT_TRUE(mcpIsError("set_clips_edit", bad));
    EXPECT_TRUE(c.batchActive()) << "a failed command must not close the batch";
    EXPECT_EQ(c.batchName(), std::string("faults"));

    ASSERT_FALSE(mcpIsError("end_batch", QJsonObject{}));
    EXPECT_FALSE(c.batchActive());
    EXPECT_TRUE(c.batchName().empty());
}

// ---------------------------------------------------------------------------
// S4 LEDGER FIX: project.beginBatch / project.endBatch are TRUE twins of
// begin_batch / end_batch (the SAME ProjectCommands::beginBatch / endBatch
// entry points and the SAME refusal texts) — the rows no longer alias the raw
// begin/endTransaction pair. The ONE deliberate asymmetry: the MCP tool
// ADDITIONALLY refuses on a non-stdio transport (the batch owns the
// process-wide undo transaction), while the RPC route — the process's own UI
// client — does not.
// ---------------------------------------------------------------------------

// (a) The RPC route alone opens/seals a batch, and the batch still collapses
// internally-transactional commands into ONE undo unit — the pin for the
// ledger's "exact twin" claim.
TEST_F(BatchEditRpcTest, BeginEndBatchRpcRouteCollapsesIntoOneUndoUnit) {
    auto& c = engine->getProjectCommands();
    const int depth = undoDepth();

    const auto begin = rpc("project.beginBatch", QJsonObject{{"name", "rpc-session"}});
    ASSERT_FALSE(begin.isError) << begin.payload.toObject().value("message").toString().toStdString();
    EXPECT_TRUE(c.batchActive());
    EXPECT_EQ(c.batchName(), std::string("rpc-session"));

    c.duplicateClips({clipA}, {8.0}, {0});
    c.removeClips({clipB});

    const auto end = rpc("project.endBatch", QJsonObject{});
    ASSERT_FALSE(end.isError);
    EXPECT_FALSE(c.batchActive());
    EXPECT_TRUE(c.batchName().empty());
    EXPECT_EQ(undoDepth(), depth + 1) << "the RPC batch must be exactly ONE undo unit";

    c.undo();
    EXPECT_TRUE(clipTree(clipB).isValid()) << "one undo restores the removed clip";
}

// (b) The deliberate asymmetry, asserted BOTH ways on a NON-stdio server: the
// MCP tool refuses (process-isolation guard) while the RPC route succeeds —
// because the route is the process's own UI client.
TEST_F(BatchEditRpcTest, RpcBeginBatchHasNoTransportGateWhileTheToolHas) {
    server->setTransportName("http");
    auto& c = engine->getProjectCommands();
    const int depth = undoDepth();

    const QJsonObject open{{"name", "ui"}};
    EXPECT_TRUE(mcpIsError("begin_batch", open));
    EXPECT_EQ(mcpText("begin_batch", open),
              QString("begin_batch requires the stdio transport (current: http) - a batch "
                      "owns the process-wide undo transaction"));
    EXPECT_FALSE(c.batchActive());

    const auto begin = rpc("project.beginBatch", open);
    EXPECT_FALSE(begin.isError)
        << "the RPC route is the process's own client and must not be transport-gated";
    EXPECT_TRUE(c.batchActive());
    EXPECT_EQ(c.batchName(), std::string("ui"));
    EXPECT_EQ(undoDepth(), depth) << "beginBatch itself opens no extra undo unit";

    // end_batch is not transport-gated on either surface, so both close it; the
    // second close answers with the shared "no open batch" bytes.
    ASSERT_FALSE(mcpIsError("end_batch", QJsonObject{}));
    EXPECT_FALSE(c.batchActive());
    EXPECT_EQ(undoDepth(), depth);
}

// (c) The SHARED refusals are byte-identical on both surfaces: a second open
// while one is open (naming the open batch) and a close with none open.
TEST_F(BatchEditRpcTest, BatchTwinRefusalsShareTheExactBytes) {
    server->setTransportName("stdio");
    auto& c = engine->getProjectCommands();
    const int depth = undoDepth();

    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "first"}}));
    EXPECT_TRUE(c.batchActive());

    // Already open: identical -32602 bytes on both surfaces.
    const QJsonObject second{{"name", "second"}};
    expectSameFailure("begin_batch", "project.beginBatch", second);
    EXPECT_EQ(mcpText("begin_batch", second),
              QString("a batch is already open (name \"first\") - call end_batch first"));
    EXPECT_EQ(c.batchName(), std::string("first")) << "the first batch keeps ownership";

    // The bytes match a route-opened batch too (same entry point, same text).
    ASSERT_FALSE(rpc("project.endBatch", QJsonObject{}).isError);
    ASSERT_FALSE(rpc("project.beginBatch", QJsonObject{{"name", "route-first"}}).isError);
    expectSameFailure("begin_batch", "project.beginBatch", second);
    EXPECT_EQ(mcpText("begin_batch", second),
              QString("a batch is already open (name \"route-first\") - call end_batch first"));
    ASSERT_FALSE(rpc("project.endBatch", QJsonObject{}).isError);

    // None open: both refuse with the shared bytes and touch nothing.
    expectSameFailure("end_batch", "project.endBatch", QJsonObject{});
    EXPECT_EQ(mcpText("end_batch", QJsonObject{}), QString("no open batch"));
    EXPECT_FALSE(c.batchActive());
    EXPECT_EQ(undoDepth(), depth) << "a refused end_batch adds no undo unit";
}

// ---------------------------------------------------------------------------
// S7 §7 — end_batch's OPTIONAL verify hook. ORDERING (stated in both tool
// descriptions and pinned here): arguments parse FIRST, the batch SEALS SECOND,
// verification runs LAST. A verification failure NEVER un-seals the batch.
// ---------------------------------------------------------------------------

// (d) + ordering proof: an ill-typed `verify` is refused BEFORE the seal, so with
// NO batch open the answer is the argument error — never "no open batch". The
// bytes are the MCP validator's own on BOTH surfaces (nested object: the
// unknown-property pass first, then the declared-property type pass).
TEST_F(BatchEditRpcTest, EndBatchArgsRefusedBeforeSealingOnBothSurfaces) {
    auto& c = engine->getProjectCommands();
    ASSERT_FALSE(c.batchActive());

    const QJsonObject bad{{"verify", QJsonObject{{"targets", 5}}}};
    expectSameFailure("end_batch", "project.endBatch", bad);
    EXPECT_EQ(mcpText("end_batch", bad),
              QString("invalid params: verify.targets: expected object"));
    EXPECT_FALSE(c.batchActive()) << "a refused argument must not touch the batch state";

    const QJsonObject bogus{{"verify", QJsonObject{{"bogus", 1}}}};
    expectSameFailure("end_batch", "project.endBatch", bogus);
    EXPECT_EQ(mcpText("end_batch", bogus),
              QString("invalid params: verify.bogus: unknown property"));

    // No verify + no open batch => the shared bare refusal, byte-identical.
    expectSameFailure("end_batch", "project.endBatch", QJsonObject{});
    EXPECT_EQ(mcpText("end_batch", QJsonObject{}), QString("no open batch"));
}

// (a) No `verify` => NO verification key, and the payload is byte-identical on
// both surfaces.
TEST_F(BatchEditRpcTest, EndBatchWithoutVerifyCarriesNoVerificationKey) {
    server->setTransportName("stdio");

    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "mcp-plain"}}));
    rpcPayload("project.setNoteGain", QJsonObject{{"noteId", n1}, {"gain", 0.5}});
    const QJsonValue viaMcp = mcpValue("end_batch", QJsonObject{});
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_EQ(viaMcp.toObject(), (QJsonObject{{"ok", true}, {"sealed", true}}));

    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "rpc-plain"}}));
    rpcPayload("project.setNoteGain", QJsonObject{{"noteId", n2}, {"gain", 0.25}});
    const QJsonValue viaRpc = rpcPayload("project.endBatch", QJsonObject{});
    EXPECT_EQ(viaRpc, viaMcp) << "the no-verify payload must be identical on both surfaces";
    EXPECT_FALSE(viaRpc.toObject().contains("verification"));
    EXPECT_FALSE(viaRpc.toObject().contains("verificationError"));
}

// (b) `verify:{targets:{ceilingHitPctMax:0}}` => `verification` present,
// byte-identical on both surfaces, and the produced file exists.
TEST_F(BatchEditRpcTest, EndBatchVerifyProducesAByteIdenticalVerdict) {
    server->setTransportName("stdio");
    const QString wav = QDir::temp().filePath(
        QStringLiteral("hdaw_end_batch_verify_%1.wav")
            .arg(juce::Random::getSystemRandom().nextInt()));
    const QJsonObject args{{"verify", QJsonObject{
        {"targets", QJsonObject{{"ceilingHitPctMax", 0.0}}}, {"outputPath", wav}}}};

    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "mcp-verify"}}));
    rpcPayload("project.setNoteGain", QJsonObject{{"noteId", n1}, {"gain", 0.5}});
    const QJsonValue viaMcp = mcpValue("end_batch", args);
    ASSERT_TRUE(viaMcp.isObject()) << mcpText("end_batch", args).toStdString();
    ASSERT_TRUE(viaMcp.toObject().contains("verification"))
        << "a requested verify must carry the verification payload";
    EXPECT_TRUE(QFileInfo::exists(wav)) << "the render is kept for A/B";
    EXPECT_TRUE(viaMcp.toObject().value("sealed").toBool());

    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "rpc-verify"}}));
    rpcPayload("project.setNoteGain", QJsonObject{{"noteId", n2}, {"gain", 0.25}});
    const QJsonValue viaRpc = rpcPayload("project.endBatch", args);
    EXPECT_EQ(viaRpc, viaMcp) << "render + verdict must be byte-identical on both surfaces";
    const QJsonObject verification = viaRpc.toObject().value("verification").toObject();
    EXPECT_EQ(verification.value("wavPath").toString(), wav);
    EXPECT_TRUE(verification.contains("verdict"));

    QFile::remove(wav);
}

// (c) A FAILING verify (unwritable outputPath) must NOT un-seal the batch: the
// payload still reports sealed:true with the error in `verificationError`, the
// writes remain ONE undo unit, and a second end_batch answers "no open batch".
TEST_F(BatchEditRpcTest, FailedVerifyStillSealsTheBatch) {
    server->setTransportName("stdio");
    auto& c = engine->getProjectCommands();
    const int depth = undoDepth();
    const double before1 = noteGain(clipA, n1);
    const double before2 = noteGain(clipA, n2);

    // A FILE used as a directory component: the render cannot write through it.
    const QString blocker = QDir::temp().filePath(
        QStringLiteral("hdaw_end_batch_blocker_%1")
            .arg(juce::Random::getSystemRandom().nextInt()));
    { QFile f(blocker); ASSERT_TRUE(f.open(QIODevice::WriteOnly)); f.write("x"); }
    const QString bad = blocker + "/out.wav";
    const QJsonObject args{{"verify", QJsonObject{{"outputPath", bad}}}};

    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "mcp-fail"}}));
    rpcPayload("project.setNoteGain", QJsonObject{{"noteId", n1}, {"gain", 0.3}});
    const QJsonValue viaMcp = mcpValue("end_batch", args);
    ASSERT_TRUE(viaMcp.isObject()) << "a verification FAILURE is not a tool error";
    EXPECT_TRUE(viaMcp.toObject().value("ok").toBool());
    EXPECT_TRUE(viaMcp.toObject().value("sealed").toBool());
    EXPECT_TRUE(viaMcp.toObject().contains("verificationError"))
        << "the verification failure must be surfaced";
    EXPECT_FALSE(viaMcp.toObject().contains("verification"));

    // The batch is SEALED: one undo unit, no open batch afterwards.
    EXPECT_FALSE(c.batchActive());
    EXPECT_EQ(undoDepth(), depth + 1) << "the batch's writes are still ONE undo unit";
    c.undo();
    EXPECT_DOUBLE_EQ(noteGain(clipA, n1), before1) << "one undo reverts the whole batch";

    expectSameFailure("end_batch", "project.endBatch", QJsonObject{});
    EXPECT_EQ(mcpText("end_batch", QJsonObject{}), QString("no open batch"));

    // Same failure through the RPC surface: identical payload.
    ASSERT_FALSE(mcpIsError("begin_batch", QJsonObject{{"name", "rpc-fail"}}));
    rpcPayload("project.setNoteGain", QJsonObject{{"noteId", n2}, {"gain", 0.3}});
    const QJsonValue viaRpc = rpcPayload("project.endBatch", args);
    EXPECT_EQ(viaRpc, viaMcp) << "a verification failure must be byte-identical too";
    EXPECT_FALSE(c.batchActive());
    EXPECT_NEAR(noteGain(clipA, n2), 0.3, 1e-6);

    QFile::remove(blocker);
}

} // namespace
