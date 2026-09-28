// Twin tests for S6 (docs/plans/2026-09-28-agent-mechanization.md §4):
// unit-EXPLICIT time windows. The ONE shared resolver
// (src/common/WindowUnitArgs.h) runs at BOTH dispatch choke points — the MCP
// tools/call path (src/mcp/McpServer.cpp) and the JSON-RPC dispatch
// (src/frontend/FrontendRouter.cpp) — so:
//   * a conflicting pair is refused with byte-identical text on both surfaces;
//   * the seconds spelling resolves to exactly the same window as the
//     equivalent beats spelling;
//   * the response echoes the unit actually used;
//   * a bare start/end call with no unit key behaves exactly as before.
// Harness idioms follow missing_route_parity_test.cpp (fixture engine +
// McpServer, rpc(), mcpText(), expectSameFailure).
//
// The project default tempo is 120 BPM, so 4 seconds == 8 beats.

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "common/WindowUnitArgs.h"
#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"

#include <memory>

namespace {

class WindowUnitParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    QJsonObject mcpResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject{{"name", tool}, {"arguments", args}}).toObject();
    }
    QString mcpText(const QString& tool, const QJsonObject& args) {
        const auto content = mcpResult(tool, args).value("content").toArray();
        return content.isEmpty() ? QString()
                                 : content[0].toObject().value("text").toString();
    }
    bool mcpIsError(const QString& tool, const QJsonObject& args) {
        return mcpResult(tool, args).value("isError").toBool();
    }
    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }
    void expectSameFailure(const QString& tool, const QString& method,
                           const QJsonObject& args, const QString& expectedText = QString()) {
        const auto r = rpc(method, args);
        ASSERT_TRUE(r.isError) << "expected " << method.toStdString() << " to fail";
        EXPECT_TRUE(mcpIsError(tool, args)) << "expected " << tool.toStdString() << " to fail";
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
        const QString rpcMessage = r.payload.toObject().value("message").toString();
        const QString mcpMessage = mcpText(tool, args);
        EXPECT_EQ(rpcMessage, mcpMessage);
        if (!expectedText.isEmpty()) {
            EXPECT_EQ(rpcMessage, expectedText);
            EXPECT_EQ(mcpMessage, expectedText);
        }
    }
    int addTrack(const QString& name = "Track") {
        const auto r = rpc("project.addTrack", QJsonObject{{"name", name}});
        EXPECT_FALSE(r.isError);
        return r.payload.toInt();
    }
    static QJsonObject parseText(const QString& text) {
        return QJsonDocument::fromJson(text.toUtf8()).object();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

// ── (a) the conflict refusal, byte-identical on both surfaces ───────────────

TEST_F(WindowUnitParityTest, ConflictingSpellingsAreRefusedOnBothSurfaces) {
    ASSERT_GE(addTrack("Conflict"), 0);
    const QString tool = "automation_preset";
    const QString method = "project.applyAutomationPreset";

    // `start` (beats, 0) and startSec (100 s == 200 beats) disagree.
    const QJsonObject args{{"trackId", 0}, {"lane", "Volume"}, {"preset", "pump"},
                           {"start", 0}, {"end", 8}, {"startSec", 100}};
    expectSameFailure(tool, method, args,
                      "conflicting window units: startSec and start disagree");

    // The agreeing pair is ACCEPTED (not refused).
    const QJsonObject agree{{"trackId", 0}, {"lane", "Volume"}, {"preset", "pump"},
                            {"start", 8}, {"end", 16}, {"startSec", 4}};
    EXPECT_FALSE(mcpIsError(tool, agree)) << mcpText(tool, agree).toStdString();
    EXPECT_FALSE(rpc(method, agree).isError);
}

TEST_F(WindowUnitParityTest, ConflictingSpellingsRefusedOnQueryNotesToo) {
    EXPECT_GE(addTrack("QN"), 0);
    const QString tool = "query_notes";
    const QString method = "read.queryNotes";
    const QJsonObject args{{"startBeat", 8}, {"endBeat", 16}, {"startSec", 100}};
    expectSameFailure(tool, method, args,
                      "conflicting window units: startBeat and startSec disagree");
}

// ── (b) seconds spelling == equivalent beats spelling ───────────────────────

TEST_F(WindowUnitParityTest, SecondsSpellingEqualsBeatsSpelling) {
    const int tBeats = addTrack("Beats");
    const int tSecs  = addTrack("Secs");
    ASSERT_GE(tBeats, 0);
    ASSERT_GE(tSecs, 0);

    const QJsonObject beats{{"trackId", tBeats}, {"lane", "Volume"}, {"preset", "pump"},
                            {"start", 0}, {"end", 8}, {"clear", true}, {"seed", 12345}};
    // 0 s / 4 s at 120 BPM == 0 / 8 beats.
    const QJsonObject secs{{"trackId", tSecs}, {"lane", "Volume"}, {"preset", "pump"},
                           {"startSec", 0}, {"endSec", 4}, {"clear", true}, {"seed", 12345}};

    const auto rb = rpc("project.applyAutomationPreset", beats);
    const auto rs = rpc("project.applyAutomationPreset", secs);
    ASSERT_FALSE(rb.isError) << rb.payload.toObject().value("message").toString().toStdString();
    ASSERT_FALSE(rs.isError) << rs.payload.toObject().value("message").toString().toStdString();

    const QJsonObject pb = rb.payload.toObject();
    const QJsonObject ps = rs.payload.toObject();
    EXPECT_EQ(pb.value("pointsAdded").toInt(), ps.value("pointsAdded").toInt());
    EXPECT_GT(pb.value("pointsAdded").toInt(), 0);
    EXPECT_EQ(pb.value("presets"), ps.value("presets"));
    // The echo differs BY DESIGN: the units actually used.
    EXPECT_EQ(pb.value("unit").toString(), QString("beats"));
    EXPECT_EQ(ps.value("unit").toString(), QString("seconds"));
}

TEST_F(WindowUnitParityTest, QueryNotesSecondsEqualsBeatsByteForByte) {
    const int t = addTrack("QN2");
    ASSERT_GE(t, 0);
    const auto clip = rpc("project.addMidiClip",
                          QJsonObject{{"trackIndex", t}, {"start", 0.0},
                                      {"duration", 4.0}, {"name", "c"}});
    ASSERT_FALSE(clip.isError);
    const int clipId = clip.payload.toInt();

    const QJsonObject beats{{"clipId", clipId}, {"start", 0}, {"duration", 8},
                            {"pitch", 60}, {"velocity", 100}};
    EXPECT_FALSE(mcpIsError("add_note", beats)) << mcpText("add_note", beats).toStdString();

    const QJsonObject wBeat{{"startBeat", 0}, {"endBeat", 8}};
    const QJsonObject wSec {{"startSec", 0}, {"endSec", 4}};
    const QJsonObject pBeat = parseText(mcpText("query_notes", wBeat));
    const QJsonObject pSec  = parseText(mcpText("query_notes", wSec));
    EXPECT_FALSE(pBeat.isEmpty());
    EXPECT_EQ(pBeat, pSec);

    // …and the RPC twins agree too.
    const auto rBeat = rpc("read.queryNotes", wBeat);
    const auto rSec  = rpc("read.queryNotes", wSec);
    ASSERT_FALSE(rBeat.isError);
    ASSERT_FALSE(rSec.isError);
    EXPECT_EQ(rBeat.payload.toObject(), pBeat);
    EXPECT_EQ(rSec.payload.toObject(), pSec);
}

// ── (c) export_audio: `unit:"beats"` mode; default stays seconds ────────────

TEST_F(WindowUnitParityTest, ExportAudioUnitModeEchoesTheUnitUsed) {
    const QString out = QStringLiteral("D:/tmp/never-written.wav");
    const QString beats = mcpText("export_audio",
        QJsonObject{{"outputPath", out}, {"dryRun", true},
                    {"start", 8}, {"end", 16}, {"unit", "beats"}});
    EXPECT_TRUE(beats.contains("would export")) << beats.toStdString();
    EXPECT_TRUE(beats.contains("unit=beats")) << beats.toStdString();

    // No `unit` key: the bare start/end keep their documented SECONDS meaning.
    const QString secs = mcpText("export_audio",
        QJsonObject{{"outputPath", out}, {"dryRun", true}, {"start", 4}, {"end", 8}});
    EXPECT_TRUE(secs.contains("unit=seconds")) << secs.toStdString();

    // A beat twin also echoes beats.
    const QString twin = mcpText("export_audio",
        QJsonObject{{"outputPath", out}, {"dryRun", true}, {"startBeat", 8}, {"endBeat", 16}});
    EXPECT_TRUE(twin.contains("unit=beats")) << twin.toStdString();
}

// ── (d) the response echoes the unit actually used ──────────────────────────

TEST_F(WindowUnitParityTest, JsonPayloadEchoesTheUnitUsed) {
    ASSERT_GE(addTrack("Echo"), 0);
    const QJsonObject beatArgs{{"trackId", 0}, {"lane", "Volume"}, {"preset", "pump"},
                               {"start", 0}, {"end", 8}};
    EXPECT_EQ(parseText(mcpText("automation_preset", beatArgs)).value("unit").toString(),
              QString("beats"));
    const auto r = rpc("project.applyAutomationPreset", beatArgs);
    ASSERT_FALSE(r.isError);
    EXPECT_EQ(r.payload.toObject().value("unit").toString(), QString("beats"));

    const int t2 = addTrack("Echo2");
    ASSERT_GE(t2, 0);
    const QJsonObject secArgs{{"trackId", t2}, {"lane", "Volume"}, {"preset", "pump"},
                              {"startSec", 0}, {"endSec", 4}};
    EXPECT_EQ(parseText(mcpText("automation_preset", secArgs)).value("unit").toString(),
              QString("seconds"));
}

// ── (e) a bare call with no unit key still behaves exactly as before ────────

TEST_F(WindowUnitParityTest, BareDefaultsUnchangedOnAllSixTools) {
    const int t = addTrack("Bare");
    ASSERT_GE(t, 0);

    // automation_preset
    const QJsonObject ap{{"trackId", t}, {"lane", "Volume"}, {"preset", "pump"},
                         {"start", 0}, {"end", 8}};
    EXPECT_FALSE(mcpIsError("automation_preset", ap));
    EXPECT_GT(parseText(mcpText("automation_preset", ap)).value("pointsAdded").toInt(), 0);

    // apply_movement_plan
    const QJsonObject mp{{"events", QJsonArray{QJsonObject{
        {"trackId", t}, {"preset", "pump"}, {"start", 0}, {"end", 8}}}}};
    const QJsonObject mpOut = parseText(mcpText("apply_movement_plan", mp));
    EXPECT_EQ(mpOut.value("okCount").toInt(), 1);
    EXPECT_EQ(mpOut.value("failCount").toInt(), 0);

    // generate_automation_envelope
    const QJsonObject env{{"trackId", t}, {"lane", "Volume"}, {"shape", "ramp"},
                          {"start", 0}, {"end", 8}};
    EXPECT_FALSE(mcpIsError("generate_automation_envelope", env));
    // The route names the track `trackIndex` (not `trackId`); the WINDOW keys
    // (start/end) are shared, which is what this test asserts.
    QJsonObject envRoute = env; envRoute.remove("trackId"); envRoute["trackIndex"] = t;
    EXPECT_FALSE(rpc("project.generateAutomationEnvelope", envRoute).isError);

    // generate_clip_gain_envelope / generate_clip_cc_lane need a clip.
    const auto clip = rpc("project.addMidiClip",
                          QJsonObject{{"trackIndex", t}, {"start", 0.0},
                                      {"duration", 4.0}, {"name", "c"}});
    ASSERT_FALSE(clip.isError);
    const int clipId = clip.payload.toInt();
    EXPECT_FALSE(mcpIsError("generate_clip_gain_envelope",
        QJsonObject{{"clipId", clipId}, {"shape", "adsr"}, {"start", 0}, {"end", 4}}));
    EXPECT_FALSE(mcpIsError("generate_clip_cc_lane",
        QJsonObject{{"clipId", clipId}, {"controllerNumber", 1}, {"shape", "sine"},
                    {"start", 0}, {"end", 4}}));

    // export_audio (dryRun keeps it cheap); bare window ⇒ seconds, as before.
    const QString exp = mcpText("export_audio",
        QJsonObject{{"outputPath", QStringLiteral("D:/tmp/x.wav")}, {"dryRun", true},
                    {"start", 0}, {"end", 4}});
    EXPECT_TRUE(exp.contains("would export")) << exp.toStdString();
}

// ── ordering: the envelope tools gained the shared inverted-window refusal ──

TEST_F(WindowUnitParityTest, InvertedEnvelopeWindowRefusedOnBothSurfaces) {
    ASSERT_GE(addTrack("Inv"), 0);
    const QJsonObject args{{"trackId", 0}, {"lane", "Volume"}, {"shape", "ramp"},
                           {"start", 8}, {"end", 4}};
    expectSameFailure("generate_automation_envelope", "project.generateAutomationEnvelope",
                      args,
                      "startBeat/endBeat invalid: endBeat must be greater than startBeat");
}

// ── all THREE spellings (bare / *Beat / *Sec) denote the same window ────────

TEST_F(WindowUnitParityTest, AllThreeSpellingsAgreeOnBothSurfaces) {
    const int t = addTrack("Three");
    ASSERT_GE(t, 0);
    const QString tool = "automation_preset";
    const QString method = "project.applyAutomationPreset";

    const QJsonObject bare{{"trackId", t}, {"lane", "Volume"}, {"preset", "pump"},
                           {"start", 0}, {"end", 8}, {"clear", true}, {"seed", 7}};
    QJsonObject beat = bare; beat.remove("start"); beat.remove("end");
    beat["startBeat"] = 0; beat["endBeat"] = 8;
    QJsonObject sec = bare; sec.remove("start"); sec.remove("end");
    sec["startSec"] = 0; sec["endSec"] = 4;   // 4 s == 8 beats at 120 BPM

    const int nBare = parseText(mcpText(tool, bare)).value("pointsAdded").toInt();
    const int nBeat = parseText(mcpText(tool, beat)).value("pointsAdded").toInt();
    const int nSec  = parseText(mcpText(tool, sec)).value("pointsAdded").toInt();
    EXPECT_GT(nBare, 0);
    EXPECT_EQ(nBare, nBeat);
    EXPECT_EQ(nBare, nSec);

    // …and the RPC twins agree with each other.
    const int rBare = rpc(method, bare).payload.toObject().value("pointsAdded").toInt();
    const int rBeat = rpc(method, beat).payload.toObject().value("pointsAdded").toInt();
    const int rSec  = rpc(method, sec).payload.toObject().value("pointsAdded").toInt();
    EXPECT_EQ(rBare, nBare);
    EXPECT_EQ(rBeat, nBeat);
    EXPECT_EQ(rSec,  nSec);
}

TEST_F(WindowUnitParityTest, EveryDisagreeingPairIsRefusedWithSharedBytes) {
    ASSERT_GE(addTrack("Pairs"), 0);
    const QString tool = "automation_preset";
    const QString method = "project.applyAutomationPreset";
    const auto base = [] { return QJsonObject{{"trackId", 0}, {"lane", "Volume"},
                                              {"preset", "pump"}}; };

    // bare start (beats) vs startSec (seconds)
    { QJsonObject o = base(); o["start"] = 0; o["end"] = 8; o["startSec"] = 100;
      expectSameFailure(tool, method, o,
                        "conflicting window units: startSec and start disagree"); }
    // bare start vs explicit startBeat
    { QJsonObject o = base(); o["start"] = 0; o["end"] = 8; o["startBeat"] = 4;
      expectSameFailure(tool, method, o,
                        "conflicting window units: startBeat and start disagree"); }
    // explicit startBeat vs explicit startSec
    { QJsonObject o = base(); o["startBeat"] = 8; o["endBeat"] = 8; o["startSec"] = 100;
      expectSameFailure(tool, method, o,
                        "conflicting window units: startBeat and startSec disagree"); }
}

// ── the schema DECLARES every accepted spelling (tools/list, not a call) ────

TEST_F(WindowUnitParityTest, SchemaDeclaresEveryAcceptedSpelling) {
    const auto list = server->handleRequestOnTestThread(1, "tools/list", QJsonObject{}).toObject();
    QHash<QString, QJsonObject> props;
    for (const auto& v : list.value("tools").toArray()) {
        const auto o = v.toObject();
        props.insert(o.value("name").toString(),
                     o.value("inputSchema").toObject().value("properties").toObject());
    }
    const auto has = [&](const QString& tool, const QString& key) {
        return props.contains(tool) && props.value(tool).contains(key);
    };
    // bare-canonical tool: bare + explicit beat + explicit seconds + unit
    for (const char* k : {"start", "startBeat", "startSec", "end", "endBeat", "endSec", "unit"})
        EXPECT_TRUE(has("automation_preset", k)) << "automation_preset missing " << k;
    // length-named tool
    for (const char* k : {"start", "startBeat", "startSec", "length", "lengthBeat", "lengthSec", "unit"})
        EXPECT_TRUE(has("add_midi_clip", k)) << "add_midi_clip missing " << k;
    // already-explicit window tool declares both explicit spellings
    EXPECT_TRUE(has("query_notes", "startBeat"));
    EXPECT_TRUE(has("query_notes", "startSec"));
    // export_audio carries both spellings on both endpoints
    for (const char* k : {"start", "startBeat", "startSec", "end", "endBeat", "endSec", "unit"})
        EXPECT_TRUE(has("export_audio", k)) << "export_audio missing " << k;
}

// ── GAP 1: routes that name their window differently get their OWN spec ─────

TEST_F(WindowUnitParityTest, AddMidiClipRouteAcceptsAllThreeSpellings) {
    const int t = addTrack("Route");
    ASSERT_GE(t, 0);
    const auto clipWindowBeats = [&](int clipId) {
        const auto o = parseText(mcpText("get_clip", QJsonObject{{"clipId", clipId}}));
        return QPair<double,double>(o.value("start").toDouble(), o.value("duration").toDouble());
    };

    // bare (beats), explicit beats, explicit seconds (2 s == 4 beats at 120 BPM)
    const auto ra = rpc("project.addMidiClip", QJsonObject{{"trackIndex", t}, {"name", "a"},
                                                           {"start", 4.0}, {"duration", 4.0}});
    const auto rb = rpc("project.addMidiClip", QJsonObject{{"trackIndex", t}, {"name", "b"},
                                                           {"startBeat", 4.0}, {"durationBeat", 4.0}});
    const auto rc = rpc("project.addMidiClip", QJsonObject{{"trackIndex", t}, {"name", "c"},
                                                           {"startSec", 2.0}, {"durationSec", 2.0}});
    ASSERT_FALSE(ra.isError) << ra.payload.toObject().value("message").toString().toStdString();
    ASSERT_FALSE(rb.isError) << rb.payload.toObject().value("message").toString().toStdString();
    ASSERT_FALSE(rc.isError) << rc.payload.toObject().value("message").toString().toStdString();

    const auto wa = clipWindowBeats(ra.payload.toInt());
    const auto wb = clipWindowBeats(rb.payload.toInt());
    const auto wc = clipWindowBeats(rc.payload.toInt());
    EXPECT_NEAR(wa.first, 4.0, 1e-9);
    EXPECT_NEAR(wb.first, wa.first, 1e-9);
    EXPECT_NEAR(wc.first, wa.first, 1e-9);
    EXPECT_NEAR(wb.second, wa.second, 1e-9);
    EXPECT_NEAR(wc.second, wa.second, 1e-9);

    // A conflicting pair is refused on the ROUTE and on the MCP tool, same bytes.
    const QJsonObject clash{{"trackIndex", t}, {"name", "x"}, {"start", 4.0},
                            {"startSec", 100.0}, {"duration", 4.0}};
    const auto rc2 = rpc("project.addMidiClip", clash);
    ASSERT_TRUE(rc2.isError);
    EXPECT_EQ(rc2.payload.toObject().value("code").toInt(), -32602);
    const QJsonObject toolClash{{"trackId", t}, {"start", 4.0}, {"startSec", 100.0},
                                {"length", 4.0}};
    EXPECT_TRUE(mcpIsError("add_midi_clip", toolClash));
    EXPECT_EQ(rc2.payload.toObject().value("message").toString(),
              mcpText("add_midi_clip", toolClash));
}

TEST_F(WindowUnitParityTest, MoveClipRouteAcceptsTheSecondsTwin) {
    const int t = addTrack("Move");
    ASSERT_GE(t, 0);
    const auto clip = rpc("project.addMidiClip",
                          QJsonObject{{"trackIndex", t}, {"start", 0.0},
                                      {"duration", 4.0}, {"name", "m"}});
    ASSERT_FALSE(clip.isError);
    const int clipId = clip.payload.toInt();

    // 4 beats == 2 s at 120 BPM; moveClip's own key is `newStart` (beats).
    const auto r = rpc("project.moveClip",
                       QJsonObject{{"clipId", clipId}, {"newTrackIndex", t}, {"newStartSec", 2.0}});
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const auto o = parseText(mcpText("get_clip", QJsonObject{{"clipId", clipId}}));
    EXPECT_NEAR(o.value("start").toDouble(), 4.0, 1e-9);

    // Both spellings for the same endpoint -> conflict, shared bytes.
    const auto clash = rpc("project.moveClip",
                           QJsonObject{{"clipId", clipId}, {"newTrackIndex", t},
                                       {"newStart", 4.0}, {"newStartSec", 100.0}});
    ASSERT_TRUE(clash.isError);
    EXPECT_EQ(clash.payload.toObject().value("code").toInt(), -32602);
    EXPECT_TRUE(clash.payload.toObject().value("message").toString()
                    .startsWith("conflicting window units:"));
}

TEST_F(WindowUnitParityTest, NoteSetterRouteAcceptsTheSecondsTwin) {
    const int t = addTrack("Setter");
    ASSERT_GE(t, 0);
    const auto clip = rpc("project.addMidiClip",
                          QJsonObject{{"trackIndex", t}, {"start", 0.0},
                                      {"duration", 4.0}, {"name", "s"}});
    ASSERT_FALSE(clip.isError);
    const int clipId = clip.payload.toInt();
    const auto note = rpc("project.addNote", QJsonObject{{"clipId", clipId}, {"pitch", 60},
                                                         {"velocity", 100}, {"startBeat", 0.0},
                                                         {"durationBeats", 1.0}});
    ASSERT_FALSE(note.isError) << note.payload.toObject().value("message").toString().toStdString();
    const int noteId = note.payload.toInt();

    const auto noteStartBeats = [&]() {
        const auto o = parseText(mcpText("list_notes", QJsonObject{{"clipId", clipId}}));
        for (const auto& v : o.value("notes").toArray())
            if (v.toObject().value("noteId").toInt() == noteId)
                return v.toObject().value("start").toDouble();
        return -1.0;
    };

    // setNoteStart's own key is `startBeat` (clip-local beats); 2 s == 4 beats.
    ASSERT_FALSE(rpc("project.setNoteStart",
                     QJsonObject{{"noteId", noteId}, {"startSec", 2.0}}).isError);
    EXPECT_NEAR(noteStartBeats(), 4.0, 1e-9);
    // The explicit beats spelling is identical.
    ASSERT_FALSE(rpc("project.setNoteStart",
                     QJsonObject{{"noteId", noteId}, {"startBeat", 4.0}}).isError);
    EXPECT_NEAR(noteStartBeats(), 4.0, 1e-9);
    // A conflicting pair is refused with the shared bytes.
    const auto clash = rpc("project.setNoteStart",
                           QJsonObject{{"noteId", noteId}, {"startBeat", 4.0},
                                       {"startSec", 100.0}});
    ASSERT_TRUE(clash.isError);
    EXPECT_EQ(clash.payload.toObject().value("code").toInt(), -32602);
    EXPECT_EQ(clash.payload.toObject().value("message").toString(),
              "conflicting window units: startBeat and startSec disagree");
}

// ── the file-based window tools convert with their OWN `bpm` argument ───────
TEST(WindowUnitResolver, MixSectionsConvertWithTheToolsOwnBpm) {
    QJsonObject o{{"bpm", 140.0},
                  {"sections", QJsonArray{QJsonObject{{"name", "a"},
                                                      {"startBeat", 4.0},
                                                      {"endBeat", 8.0}}}}};
    QString unit, err;
    ASSERT_TRUE(HDAW::normalizeWindowArgsForTool("mix_report", o, /*project bpm=*/120.0,
                                                 unit, err)) << err.toStdString();
    const auto s = o.value("sections").toArray()[0].toObject();
    // 4 beats at the ARGUMENT's 140 BPM == 4*60/140 s (a project-tempo conversion
    // would give 2.0).
    EXPECT_NEAR(s.value("start").toDouble(), 4.0 * 60.0 / 140.0, 1e-9);
    EXPECT_NEAR(s.value("end").toDouble(),   8.0 * 60.0 / 140.0, 1e-9);

    // With no `bpm` argument the conversion falls back to the project tempo.
    QJsonObject p{{"sections", QJsonArray{QJsonObject{{"name", "a"},
                                                      {"startSec", 2.0},
                                                      {"endSec", 4.0}}}}};
    ASSERT_TRUE(HDAW::normalizeWindowArgsForTool("mix_report", p, 120.0, unit, err))
        << err.toStdString();
    const auto ps = p.value("sections").toArray()[0].toObject();
    EXPECT_NEAR(ps.value("start").toDouble(), 2.0, 1e-9);
    EXPECT_NEAR(ps.value("end").toDouble(),   4.0, 1e-9);
}

// ── S6c: the four cut-over payloads (value key + echoed unit) ───────────────

TEST_F(WindowUnitParityTest, ArrangerRegionPayloadsCarryValueAndUnitOnBothSurfaces) {
    // add_arranger_region, bare window (documented default SECONDS).
    const QJsonObject bare{{"name", "Intro"}, {"startTime", 0.0}, {"duration", 8.0}};
    const QJsonObject pBare = parseText(mcpText("add_arranger_region", bare));
    ASSERT_FALSE(pBare.value("regionID").toString().isEmpty())
        << mcpText("add_arranger_region", bare).toStdString();
    EXPECT_EQ(pBare.value("unit").toString(), QString("seconds"));
    const auto rBare = rpc("project.addArrangerRegion", bare);
    ASSERT_FALSE(rBare.isError);
    // Each call mints a fresh regionID, so the twin comparison pins the SHAPE
    // (the unit key + a non-empty regionID), not the server-generated id.
    const QJsonObject rBareObj = rBare.payload.toObject();
    EXPECT_FALSE(rBareObj.value("regionID").toString().isEmpty());
    QJsonObject pBareNoId = pBare, rBareNoId = rBareObj;
    pBareNoId.remove("regionID");
    rBareNoId.remove("regionID");
    EXPECT_EQ(rBareNoId, pBareNoId) << "value+unit byte-identical on both surfaces";
    const QString rid = pBare.value("regionID").toString();

    // The explicit beat spelling echoes BEATS on both surfaces.
    const QJsonObject beats{{"name", "B"}, {"startTimeBeat", 0.0}, {"durationBeat", 8.0}};
    const QJsonObject pBeats = parseText(mcpText("add_arranger_region", beats));
    EXPECT_FALSE(pBeats.value("regionID").toString().isEmpty());
    EXPECT_EQ(pBeats.value("unit").toString(), QString("beats"));
    const auto rBeats = rpc("project.addArrangerRegion", beats);
    ASSERT_FALSE(rBeats.isError);
    EXPECT_EQ(rBeats.payload.toObject().value("unit").toString(), QString("beats"));

    // set_arranger_region_bounds: status object + unit, twin surfaces.
    const QJsonObject sbare{{"regionID", rid}, {"startTime", 4.0}, {"duration", 16.0}};
    const QJsonObject pSbare = parseText(mcpText("set_arranger_region_bounds", sbare));
    EXPECT_TRUE(pSbare.value("ok").toBool());
    EXPECT_EQ(pSbare.value("unit").toString(), QString("seconds"));
    const auto rSbare = rpc("project.setArrangerRegionBounds", sbare);
    ASSERT_FALSE(rSbare.isError);
    EXPECT_EQ(rSbare.payload.toObject(), pSbare);

    const QJsonObject sbeats{{"regionID", rid}, {"startTimeBeat", 4.0}, {"durationBeat", 16.0}};
    const QJsonObject pSbeats = parseText(mcpText("set_arranger_region_bounds", sbeats));
    EXPECT_TRUE(pSbeats.value("ok").toBool());
    EXPECT_EQ(pSbeats.value("unit").toString(), QString("beats"));
    const auto rSbeats = rpc("project.setArrangerRegionBounds", sbeats);
    ASSERT_FALSE(rSbeats.isError);
    EXPECT_EQ(rSbeats.payload.toObject(), pSbeats);
}

TEST_F(WindowUnitParityTest, TransportAndSeekPayloadsCarryTheCallersUnit) {
    // transport with a BEAT loop window echoes beats.
    const QJsonObject tb = parseText(mcpText("transport",
        QJsonObject{{"action", "play"}, {"loopStartBeat", 8.0}, {"loopEndBeat", 16.0}}));
    EXPECT_TRUE(tb.value("ok").toBool());
    EXPECT_EQ(tb.value("unit").toString(), QString("beats"));

    // The bare spelling (documented default SECONDS) echoes seconds…
    const QJsonObject ts = parseText(mcpText("transport",
        QJsonObject{{"action", "play"}, {"loopStart", 4.0}, {"loopEnd", 8.0}}));
    EXPECT_TRUE(ts.value("ok").toBool());
    EXPECT_EQ(ts.value("unit").toString(), QString("seconds"));

    // …as does the explicit *Sec spelling.
    const QJsonObject tsec = parseText(mcpText("transport",
        QJsonObject{{"action", "play"}, {"loopStartSec", 4.0}, {"loopEndSec", 8.0}}));
    EXPECT_TRUE(tsec.value("ok").toBool());
    EXPECT_EQ(tsec.value("unit").toString(), QString("seconds"));

    // No loop window: nothing resolved, so the reply stays the bare {"ok":true}.
    const QJsonObject tn = parseText(mcpText("transport", QJsonObject{{"action", "pause"}}));
    EXPECT_TRUE(tn.value("ok").toBool());
    EXPECT_FALSE(tn.contains("unit"));

    // seek: the beat spelling writes beats (8 beats == 4 s at 120 BPM) and echoes beats.
    const QJsonObject sb = parseText(mcpText("seek", QJsonObject{{"positionBeat", 8.0}}));
    EXPECT_TRUE(sb.value("ok").toBool());
    EXPECT_EQ(sb.value("unit").toString(), QString("beats"));
    EXPECT_NEAR(parseText(mcpText("get_transport", QJsonObject{}))
                    .value("position").toDouble(), 4.0, 1e-9);

    // The bare spelling keeps its SECONDS meaning and echoes seconds…
    const QJsonObject ss = parseText(mcpText("seek", QJsonObject{{"position", 6.0}}));
    EXPECT_TRUE(ss.value("ok").toBool());
    EXPECT_EQ(ss.value("unit").toString(), QString("seconds"));
    EXPECT_NEAR(parseText(mcpText("get_transport", QJsonObject{}))
                    .value("position").toDouble(), 6.0, 1e-9);

    // …and so does the explicit *Sec spelling.
    const QJsonObject ssec = parseText(mcpText("seek", QJsonObject{{"positionSec", 6.0}}));
    EXPECT_TRUE(ssec.value("ok").toBool());
    EXPECT_EQ(ssec.value("unit").toString(), QString("seconds"));
}

} // namespace
