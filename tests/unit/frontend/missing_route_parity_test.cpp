// Twin tests for the 2026-09-24 RPC-parity wave — the seven MCP tools that
// gained a JSON-RPC route in one pass:
//   automation_preset        <-> project.applyAutomationPreset
//   apply_movement_plan      <-> project.applyMovementPlan
//   set_master_fx_param      <-> project.setMasterFxParam
//   set_master_fx_bypassed   <-> project.setMasterFxBypassed
//   place_patterns           <-> composition.placePatterns
//   scale_note               <-> composition.scaleDegreeToPitch
//   session_get_clip_states  <-> session.getClipStates
//
// AGENTS.md twin-test rule: BOTH surfaces must fail with -32602 and the SAME
// message text, and the route payload must equal the tool's text for the
// JSON-payload routes (the two master-FX routes hand the tool TEXT back as a
// JSON string). Follows the harness idioms of add_fx_parity_test.cpp /
// bus_send_rpc_test.cpp (fixture engine + McpServer, rpc(), mcpText(),
// mcpIsError(), expectSameFailure).
//
// Adversarial: every failure case asserts -32602 + byte-identical text, so this
// file is RED on the pre-wave tree (the route did not exist there -> -32601).
//
// Determinism (lesson 9): createDefaultProject() ships ZERO tracks, so every
// track/clip/lane a case needs is created here. Master FX is the one exception:
// initialize() stamps MASTER_FX with slot 0=eq / slot 1=limiter, both bypassed.

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <memory>
#include <string>

namespace {

class MissingRouteParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    // --- MCP surface (bus_send_rpc_test harness shape) ---------------------
    QJsonObject mcpResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject{ { "name", tool }, { "arguments", args } })
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

    // --- RPC surface --------------------------------------------------------
    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }

    // A failing pair: both surfaces must report -32602 and the same text
    // (copied from add_fx_parity_test.cpp so this TU stays self-contained).
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

    // The MCP server validates a tool's JSON schema (required properties, type,
    // numeric min/max) BEFORE the handler runs, so for schema-invalid args the
    // tool answers "invalid params: …" while the route reaches the shared
    // validator and returns the shared text. Code parity (-32602) still holds on
    // both; the message equality is asserted where both reach the shared code
    // (expectSameFailure). These two helpers cover the schema-gated cases.
    void expectRouteFailure(const QString& method, const QJsonObject& args,
                            const QString& expectedText) {
        const auto r = rpc(method, args);
        ASSERT_TRUE(r.isError) << "expected " << method.toStdString() << " to fail";
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
        EXPECT_EQ(r.payload.toObject().value("message").toString(), expectedText);
    }
    void expectBothReject(const QString& tool, const QString& method,
                          const QJsonObject& args) {
        const auto r = rpc(method, args);
        ASSERT_TRUE(r.isError) << "expected " << method.toStdString() << " to fail";
        EXPECT_TRUE(mcpIsError(tool, args)) << "expected " << tool.toStdString() << " to fail";
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
        EXPECT_FALSE(mcpText(tool, args).isEmpty());
    }

    // --- scaffolding --------------------------------------------------------
    int addTrack(const QString& name = "Track") {
        const auto r = rpc("project.addTrack", QJsonObject{ { "name", name } });
        EXPECT_FALSE(r.isError);
        const int idx = r.payload.toInt();
        EXPECT_GE(idx, 0);
        return idx;
    }
    int addMidiClip(int trackIndex, double start, double duration, const QString& name) {
        const auto r = rpc("project.addMidiClip",
                           QJsonObject{ { "trackIndex", trackIndex },
                                        { "start", start },
                                        { "duration", duration },
                                        { "name", name } });
        EXPECT_FALSE(r.isError);
        return r.payload.toInt();
    }
    juce::ValueTree masterFx() {
        return engine->getProjectModel().getTree().getChildWithName(IDs::MASTER_FX);
    }
    static QJsonObject parseText(const QString& text) {
        return QJsonDocument::fromJson(text.toUtf8()).object();
    }
    static QJsonArray parseArray(const QString& text) {
        return QJsonDocument::fromJson(text.toUtf8()).array();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

// ─── automation_preset <-> project.applyAutomationPreset ────────────────────

TEST_F(MissingRouteParityTest, AutomationPresetFailuresMatchOnBothSurfaces) {
    const QString tool = "automation_preset";
    const QString method = "project.applyAutomationPreset";

    // `{}`: the MCP schema requires trackId + lane, so the tool answers
    // "invalid params: …" before the shared lane gate; the ROUTE reaches the
    // shared gate. Code parity holds on both surfaces.
    expectBothReject(tool, method, QJsonObject{});
    expectRouteFailure(method, QJsonObject{},
        "lane not found; create it with add_automation_lane first "
        "(built-in lanes like \"Volume\" work by name)");

    // Schema-satisfying no-lane case (zero-track default project): byte-identical.
    expectSameFailure(tool, method,
                      QJsonObject{ { "trackId", 0 }, { "lane", "Volume" } });

    // Real track + built-in Volume lane: the preset-name gate fires.
    ASSERT_GE(addTrack("Preset"), 0);
    expectSameFailure(tool, method,
                      QJsonObject{ { "trackId", 0 }, { "lane", "Volume" },
                                   { "preset", "bogus" }, { "start", 0 }, { "end", 4 } });

    // Real track + lane + valid preset: the window-ordering gate fires.
    expectSameFailure(tool, method,
                      QJsonObject{ { "trackId", 0 }, { "lane", "Volume" },
                                   { "preset", "pump" }, { "start", 2 }, { "end", 1 } });
}

TEST_F(MissingRouteParityTest, AutomationPresetPayloadMatchesAcrossTracks) {
    const QString tool = "automation_preset";
    const QString method = "project.applyAutomationPreset";

    const int t0 = addTrack("PresetA");
    const int t1 = addTrack("PresetB");
    ASSERT_GE(t0, 0);
    ASSERT_GE(t1, 0);

    // Identical args except trackId; each track owns its own built-in Volume
    // lane. The payload carries no trackId, so equality is meaningful.
    const QJsonObject base{ { "lane", "Volume" }, { "preset", "pump" },
                            { "start", 0 }, { "end", 4 } };

    QJsonObject routeArgs = base;
    routeArgs["trackId"] = t0;
    QJsonObject mcpArgs = base;
    mcpArgs["trackId"] = t1;

    const auto r = rpc(method, routeArgs);
    ASSERT_FALSE(r.isError)
        << r.payload.toObject().value("message").toString().toStdString();

    EXPECT_FALSE(mcpIsError(tool, mcpArgs)) << mcpText(tool, mcpArgs).toStdString();

    const QJsonObject routePayload = r.payload.toObject();
    const QJsonObject mcpPayload = parseText(mcpText(tool, mcpArgs));
    EXPECT_EQ(routePayload, mcpPayload);
    EXPECT_GT(routePayload.value("pointsAdded").toInt(), 0);
}

// ─── apply_movement_plan <-> project.applyMovementPlan ──────────────────────

TEST_F(MissingRouteParityTest, ApplyMovementPlanFailsIdenticallyOnBothSurfaces) {
    const QString tool = "apply_movement_plan";
    const QString method = "project.applyMovementPlan";

    // `{}`: the MCP schema requires `events` — the tool answers first; the
    // route reaches the shared parser. Code parity holds on both.
    expectBothReject(tool, method, QJsonObject{});
    expectRouteFailure(method, QJsonObject{}, "events array required");

    // Schema-satisfying empty array: the shared parser emits the text on both.
    expectSameFailure(tool, method, QJsonObject{ { "events", QJsonArray{} } });
}

TEST_F(MissingRouteParityTest, ApplyMovementPlanPayloadMatchesAcrossTracks) {
    const QString tool = "apply_movement_plan";
    const QString method = "project.applyMovementPlan";

    const int t0 = addTrack("MovementA");
    const int t1 = addTrack("MovementB");
    ASSERT_GE(t0, 0);
    ASSERT_GE(t1, 0);

    // One event each: paramID 1 (default Volume lane), identical window/preset.
    const auto eventFor = [](int track) {
        return QJsonObject{ { "trackId", track }, { "paramID", 1 },
                            { "preset", "riser" }, { "start", 0 }, { "end", 16 } };
    };
    const QJsonObject routeArgs{ { "events", QJsonArray{ eventFor(t0) } } };
    const QJsonObject mcpArgs{ { "events", QJsonArray{ eventFor(t1) } } };

    const auto r = rpc(method, routeArgs);
    ASSERT_FALSE(r.isError)
        << r.payload.toObject().value("message").toString().toStdString();
    const QJsonObject routePayload = r.payload.toObject();
    const QJsonObject mcpPayload = parseText(mcpText(tool, mcpArgs));

    EXPECT_EQ(routePayload, mcpPayload);
    EXPECT_EQ(routePayload.value("okCount").toInt(), 1);
    EXPECT_EQ(routePayload.value("failCount").toInt(), 0);
    ASSERT_EQ(routePayload.value("events").toArray().size(), 1);
    EXPECT_FALSE(routePayload.value("events").toArray()[0].toObject()
                     .value("laneName").toString().isEmpty());
}

// ─── set_master_fx_param <-> project.setMasterFxParam ───────────────────────

TEST_F(MissingRouteParityTest, SetMasterFxParamFailuresMatchOnBothSurfaces) {
    const QString tool = "set_master_fx_param";
    const QString method = "project.setMasterFxParam";

    // Slot gate: default project has slots 0 and 1 only.
    expectSameFailure(tool, method,
                      QJsonObject{ { "slotIndex", 9 }, { "paramIndex", 0 }, { "value", 1 } });
    // Param gate: neither paramIndex nor paramName.
    expectSameFailure(tool, method,
                      QJsonObject{ { "slotIndex", 0 }, { "value", 1 } });
    // Param gate: unknown paramName.
    expectSameFailure(tool, method,
                      QJsonObject{ { "slotIndex", 0 }, { "paramName", "nope" }, { "value", 1 } });
}

TEST_F(MissingRouteParityTest, SetMasterFxParamClampedWriteReachesTree) {
    const QString tool = "set_master_fx_param";
    const QString method = "project.setMasterFxParam";

    // Slot 0 is the default 'eq' (param 0 = Frequency, range 20..20000 Hz).
    const QJsonObject args{ { "slotIndex", 0 }, { "paramIndex", 0 }, { "value", 1e9 } };

    EXPECT_FALSE(mcpIsError(tool, args)) << mcpText(tool, args).toStdString();
    const QString text = mcpText(tool, args);
    EXPECT_TRUE(text.contains("clamped")) << text.toStdString();

    const auto r = rpc(method, args);
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    // The success payload IS the tool text (a JSON string) on this route.
    ASSERT_TRUE(r.payload.isString());
    EXPECT_EQ(r.payload.toString(), text);

    auto fx = masterFx();
    ASSERT_TRUE(fx.isValid());
    ASSERT_GE(fx.getNumChildren(), 1);
    // The write reached the tree AS CLAMPED to the eq Frequency max (20 kHz).
    EXPECT_FLOAT_EQ((float) (double) fx.getChild(0).getProperty("param_0", 0.0), 20000.0f);

    // A rejected slot write must leave the tree untouched.
    const double before = (double) fx.getChild(0).getProperty("param_0", 0.0);
    expectSameFailure(tool, method,
                      QJsonObject{ { "slotIndex", 9 }, { "paramIndex", 0 }, { "value", 1 } });
    EXPECT_DOUBLE_EQ((double) fx.getChild(0).getProperty("param_0", 0.0), before);
}

// ─── set_master_fx_bypassed <-> project.setMasterFxBypassed ─────────────────

TEST_F(MissingRouteParityTest, SetMasterFxBypassedFailsAndWriteReachesTree) {
    const QString tool = "set_master_fx_bypassed";
    const QString method = "project.setMasterFxBypassed";

    expectSameFailure(tool, method, QJsonObject{ { "slotIndex", 9 }, { "bypassed", true } });

    auto fx = masterFx();
    ASSERT_TRUE(fx.isValid());
    ASSERT_GE(fx.getNumChildren(), 1);

    // Route enables slot 0; the payload IS the tool text ("ok").
    const QJsonObject on{ { "slotIndex", 0 }, { "bypassed", true } };
    EXPECT_FALSE(mcpIsError(tool, on)) << mcpText(tool, on).toStdString();
    const auto r = rpc(method, on);
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    EXPECT_EQ(r.payload.toString(), mcpText(tool, on));
    EXPECT_TRUE(fx.getChild(0).getProperty("bypassed", false));

    // MCP flips it back: the write must reach the tree on this surface too.
    const QJsonObject off{ { "slotIndex", 0 }, { "bypassed", false } };
    EXPECT_FALSE(mcpIsError(tool, off)) << mcpText(tool, off).toStdString();
    EXPECT_FALSE(fx.getChild(0).getProperty("bypassed", true));
}

// ─── place_patterns <-> composition.placePatterns ───────────────────────────

namespace {
QJsonObject oneNotePattern(int pitch) {
    return QJsonObject{ { "notes", QJsonArray{ QJsonObject{
        { "pitch", pitch }, { "startBeat", 0 }, { "durationBeats", 1 }, { "velocity", 100 } } } } };
}
const QJsonArray kOnePlacement{ QJsonObject{ { "start", 0 } } };
} // namespace

TEST_F(MissingRouteParityTest, PlacePatternsFailuresMatchOnBothSurfaces) {
    const QString tool = "place_patterns";
    const QString method = "composition.placePatterns";

    // `{}`: the MCP schema requires clipId/patterns/placements — the tool
    // answers first; the route reaches the shared parser. Code parity holds.
    expectBothReject(tool, method, QJsonObject{});
    expectRouteFailure(method, QJsonObject{}, "patterns must be non-empty");

    // Patterns present + placements empty (schema-satisfying): shared text both.
    expectSameFailure(tool, method,
                      QJsonObject{ { "clipId", 999 },
                                   { "patterns", QJsonArray{ oneNotePattern(60) } },
                                   { "placements", QJsonArray{} } });
    // Empty patterns (schema-satisfying): shared text both.
    expectSameFailure(tool, method,
                      QJsonObject{ { "clipId", 999 },
                                   { "patterns", QJsonArray{} },
                                   { "placements", kOnePlacement } });

    // Note pitch 200: the MCP schema (pitch max 127) rejects first; the route's
    // shared parser emits the exact Gate-9 text.
    const QJsonObject badPitch{ { "clipId", 999 },
                                { "patterns", QJsonArray{ oneNotePattern(200) } },
                                { "placements", kOnePlacement } };
    expectBothReject(tool, method, badPitch);
    expectRouteFailure(method, badPitch, "pattern note pitch must be in 0..127");

    // Placement octave 9: MCP schema (octave max 6) rejects first; route shared.
    const QJsonObject badOctave{
        { "clipId", 999 },
        { "patterns", QJsonArray{ oneNotePattern(60) } },
        { "placements", QJsonArray{ QJsonObject{ { "start", 0 }, { "octave", 9 } } } } };
    expectBothReject(tool, method, badOctave);
    expectRouteFailure(method, badOctave, "placement octave must be in -6..6");

    // Valid request against a clipId that does not exist — the ENGINE's text is
    // shared, so equality is asserted (never guessed) on both surfaces.
    const QJsonObject missing{ { "clipId", 999 },
                               { "patterns", QJsonArray{ oneNotePattern(60) } },
                               { "placements", kOnePlacement } };
    expectSameFailure(tool, method, missing);
    EXPECT_EQ(mcpText(tool, missing), QString("clip not found"));
    EXPECT_EQ(rpc(method, missing).payload.toObject().value("message").toString(),
              QString("clip not found"));
}

TEST_F(MissingRouteParityTest, PlacePatternsPayloadMatchesAcrossClips) {
    const QString tool = "place_patterns";
    const QString method = "composition.placePatterns";

    const int track = addTrack("Place");
    ASSERT_GE(track, 0);
    const int clipA = addMidiClip(track, 0.0, 16.0, "A");
    const int clipB = addMidiClip(track, 16.0, 16.0, "B");
    ASSERT_GE(clipA, 0);
    ASSERT_GE(clipB, 0);

    const QJsonObject base{ { "patterns", QJsonArray{ oneNotePattern(60) } },
                            { "placements", kOnePlacement } };
    QJsonObject routeArgs = base;
    routeArgs["clipId"] = clipA;
    QJsonObject mcpArgs = base;
    mcpArgs["clipId"] = clipB;

    const auto r = rpc(method, routeArgs);
    ASSERT_FALSE(r.isError)
        << r.payload.toObject().value("message").toString().toStdString();
    QJsonObject routePayload = r.payload.toObject();
    QJsonObject mcpPayload = parseText(mcpText(tool, mcpArgs));

    EXPECT_EQ(routePayload.value("added").toInt(), 1);
    EXPECT_EQ(routePayload.value("skipped").toInt(), 0);
    EXPECT_EQ(routePayload.value("placementsApplied").toInt(), 1);
    EXPECT_EQ(mcpPayload.value("added").toInt(), 1);
    EXPECT_EQ(mcpPayload.value("skipped").toInt(), 0);
    EXPECT_EQ(mcpPayload.value("placementsApplied").toInt(), 1);

    // Each surface reports its OWN target clip.
    EXPECT_EQ(routePayload.value("clipId").toInt(), clipA);
    EXPECT_EQ(mcpPayload.value("clipId").toInt(), clipB);

    // Everything else is identical.
    routePayload.remove("clipId");
    mcpPayload.remove("clipId");
    EXPECT_EQ(routePayload, mcpPayload);
}

// ─── scale_note <-> composition.scaleDegreeToPitch ──────────────────────────

TEST_F(MissingRouteParityTest, ScaleNoteFailuresMatchOnBothSurfaces) {
    const QString tool = "scale_note";
    const QString method = "composition.scaleDegreeToPitch";

    // `{}`: the MCP schema requires rootMidi/scale/degree — the tool answers
    // first; the route reaches the shared resolver. Code parity holds.
    expectBothReject(tool, method, QJsonObject{});
    expectRouteFailure(method, QJsonObject{}, "rootMidi must be in 0..127");

    // Schema-satisfying: both surfaces reach the shared resolver.
    expectSameFailure(tool, method,
                      QJsonObject{ { "rootMidi", 60 }, { "scale", "not-a-scale" }, { "degree", 0 } });
    expectSameFailure(tool, method,
                      QJsonObject{ { "rootMidi", 120 }, { "scale", "Major (Ionian)" }, { "degree", 24 } });

    // rootMidi 200: MCP schema (maximum 127) rejects first; route shared text.
    const QJsonObject badRoot{ { "rootMidi", 200 }, { "scale", "Major (Ionian)" }, { "degree", 0 } };
    expectBothReject(tool, method, badRoot);
    expectRouteFailure(method, badRoot, "rootMidi must be in 0..127");
}

TEST_F(MissingRouteParityTest, ScaleNotePayloadMatchesOnBothSurfaces) {
    const QString tool = "scale_note";
    const QString method = "composition.scaleDegreeToPitch";

    const QJsonArray combos{
        QJsonObject{ { "rootMidi", 60 }, { "scale", "Major (Ionian)" }, { "degree", 0 } },
        QJsonObject{ { "rootMidi", 60 }, { "scale", "Major (Ionian)" }, { "degree", 7 } },
        QJsonObject{ { "rootMidi", 60 }, { "scale", "Major (Ionian)" }, { "degree", -1 } },
        QJsonObject{ { "rootMidi", 60 }, { "scale", "Major (Ionian)" }, { "degree", 0 }, { "octave", 1 } },
        QJsonObject{ { "rootMidi", 48 }, { "scale", "Minor (Aeolian)" }, { "degree", 2 }, { "octave", -1 } },
    };
    for (const auto& cv : combos) {
        const QJsonObject args = cv.toObject();
        const auto r = rpc(method, args);
        ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
        EXPECT_FALSE(mcpIsError(tool, args)) << mcpText(tool, args).toStdString();

        const QJsonObject routePayload = r.payload.toObject();
        const QJsonObject mcpPayload = parseText(mcpText(tool, args));
        EXPECT_EQ(routePayload, mcpPayload);
        EXPECT_TRUE(routePayload.contains("midiPitch"));
    }

    // The shared resolver's three accepted spellings must agree.
    const auto pitchOf = [&](const QString& scale) {
        const QJsonObject args{ { "rootMidi", 57 }, { "scale", scale }, { "degree", 3 } };
        const auto r = rpc(method, args);
        EXPECT_FALSE(r.isError);
        return r.payload.toObject().value("midiPitch").toInt();
    };
    const int canonical = pitchOf("Minor (Aeolian)");
    EXPECT_EQ(pitchOf("minor"), canonical);
    EXPECT_EQ(pitchOf("aeolian"), canonical);
}

// ─── session_get_clip_states <-> session.getClipStates ──────────────────────

TEST_F(MissingRouteParityTest, SessionGetClipStatesMatchesOnBothSurfaces) {
    const QString tool = "session_get_clip_states";
    const QString method = "session.getClipStates";

    // Empty session: an empty array, NOT the -32601 unknown-method fallthrough.
    const auto empty = rpc(method, QJsonObject{});
    ASSERT_FALSE(empty.isError)
        << "route must exist, not fall through: "
        << empty.payload.toObject().value("message").toString().toStdString();
    ASSERT_TRUE(empty.payload.isArray());
    EXPECT_TRUE(empty.payload.toArray().isEmpty());
    EXPECT_EQ(empty.payload.toArray(), parseArray(mcpText(tool, QJsonObject{})));

    // One session clip: both surfaces must return the same one-element array.
    const int track = addTrack("Session");
    ASSERT_GE(track, 0);
    const auto created = rpc("session.createClip",
                             QJsonObject{ { "trackIndex", track }, { "sceneIndex", 0 } });
    ASSERT_FALSE(created.isError);
    const int clipId = created.payload.toInt();
    ASSERT_GE(clipId, 0);

    const auto states = rpc(method, QJsonObject{});
    ASSERT_FALSE(states.isError);
    const QJsonArray routeArr = states.payload.toArray();
    ASSERT_EQ(routeArr.size(), 1);
    const QJsonObject clip = routeArr[0].toObject();
    EXPECT_EQ(clip.value("clipId").toInt(), clipId);
    EXPECT_EQ(clip.value("sceneIndex").toInt(), 0);
    EXPECT_TRUE(clip.contains("isPlaying"));
    EXPECT_TRUE(clip.contains("isLaunched"));
    EXPECT_EQ(routeArr, parseArray(mcpText(tool, QJsonObject{})));
}

} // namespace
