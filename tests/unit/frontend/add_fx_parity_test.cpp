// MCP add_fx <-> RPC project.addFxSlot rejection twins (fix 2026-09-23:
// bare/unresolvable pluginIds used to return silent success and leave an inert
// 'none' slot — Track.cpp:178-182 substitutes the placeholder while the tool
// already answered "slot=N"; the *FX shadow editions were pickable the same
// way and silenced the track).
//
// Follows the expectSameFailure pattern of bus_send_rpc_test.cpp: the SAME
// argument object is handed to both surfaces, so the failure must come back
// with the same -32602 code and BYTE-IDENTICAL message text (both surfaces
// format it in one place — HDAW::fxPluginIdError, src/common/FxPluginIdCheck.h).
// The rejection rides the dispatch-level gate (FrontendRouter intercepts
// project.addFxSlot before dispatchProject, importMidiFile precedent), which
// is why shared failure args work even though the route itself reads
// `trackIndex` rather than `trackId`.
//
// Determinism: every assertion here holds with or without the machine-local
// plugin_cache.xml — the unresolvable id resolves in NO cache, the shadow id
// is rejected by the pure predicate BEFORE the resolvability check, and the
// accept path uses the .clap extension fallback (ProjectModel.cpp:501-507).

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

class AddFxParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        ASSERT_GE(engine->getProjectCommands().addTrack("Track"), 0);
        engine->drainPendingRoutingRebuild();
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

    // A failing pair: both surfaces must report the same code and the same
    // text (copied from bus_send_rpc_test.cpp so this TU stays self-contained).
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

    // A rejected add_fx must leave NOTHING behind — the old bug reported
    // success while rebuildFXChain quietly substituted a 'none' placeholder.
    void expectNoFxSlot() {
        const auto fxChain = engine->getProjectModel().getTrackListTree()
                                 .getChild(0).getChildWithName(IDs::FX_CHAIN);
        ASSERT_TRUE(fxChain.isValid()) << "the track must exist";
        EXPECT_EQ(fxChain.getNumChildren(), 0)
            << "a rejected add_fx must not create a slot";
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

TEST_F(AddFxParityTest, UnresolvablePluginIdFailsIdenticallyOnBothSurfaces) {
    const QJsonObject args{ { "trackId", 0 },
                            { "pluginId", "definitely-not-a-plugin" } };
    expectSameFailure("add_fx", "project.addFxSlot", args);
    EXPECT_TRUE(mcpText("add_fx", args).contains("definitely-not-a-plugin"))
        << "the error must name the id: "
        << mcpText("add_fx", args).toStdString();
    expectNoFxSlot();
}

TEST_F(AddFxParityTest, ShadowFxEditionFailsIdenticallyOnBothSurfaces) {
    // Bare cache name — the exact form list_plugins used to publish.
    const QJsonObject bare{ { "trackId", 0 }, { "pluginId", "VavraFX" } };
    expectSameFailure("add_fx", "project.addFxSlot", bare);
    EXPECT_TRUE(mcpText("add_fx", bare).contains("VavraFX"))
        << mcpText("add_fx", bare).toStdString();

    // Format-qualified id: resolvable against a cache that CONTAINS the
    // plugin, so this pins that the shadow predicate fires before the
    // resolvability check — deterministic either way.
    const QJsonObject qualified{ { "trackId", 0 },
                                 { "pluginId", "CLAP-VavraFX-a405fdaa-0" } };
    expectSameFailure("add_fx", "project.addFxSlot", qualified);
    EXPECT_TRUE(mcpText("add_fx", qualified).contains("CLAP-VavraFX-a405fdaa-0"))
        << mcpText("add_fx", qualified).toStdString();
    expectNoFxSlot();
}

TEST_F(AddFxParityTest, InternalFxTypeStillSucceedsOnBothSurfaces) {
    // The gate only fires for a NON-EMPTY pluginId: internal fxType slots and
    // the arg-less sentinel stay valid (mcp_coverage_test pins
    // `add_fx {trackId}` with no fxType/pluginId as non-error).
    const QJsonObject mcpArgs{ { "trackId", 0 }, { "fxType", "eq" } };
    const auto r = mcpResult("add_fx", mcpArgs);
    EXPECT_FALSE(r.value("isError").toBool()) << mcpText("add_fx", mcpArgs).toStdString();
    EXPECT_TRUE(mcpText("add_fx", mcpArgs).startsWith("slot="));

    const QJsonObject rpcArgs{ { "trackIndex", 0 }, { "fxType", "eq" } };
    EXPECT_FALSE(rpc("project.addFxSlot", rpcArgs).isError);
}

TEST_F(AddFxParityTest, ResolvablePluginIdStillSucceedsOnBothSurfaces) {
    // A .clap path resolves WITHOUT any scan cache (extension fallback) — the
    // deterministic accept path. Instantiation of the missing file degrades to
    // a 'none' placeholder exactly like the setFxSlotPlugin
    // "/path/test.vst3" flow that commands_test.cpp:704 already exercises.
    const QString id = "C:/definitely/missing/hdaw_parity_probe.clap";

    // MCP: one single call — the handler answers "slot=N" before any rebuild.
    const QJsonObject mcpArgs{ { "trackId", 0 }, { "pluginId", id } };
    const auto mcpR = mcpResult("add_fx", mcpArgs);
    const auto content = mcpR.value("content").toArray();
    ASSERT_FALSE(content.isEmpty());
    const QString text = content[0].toObject().value("text").toString();
    EXPECT_FALSE(mcpR.value("isError").toBool()) << text.toStdString();
    EXPECT_TRUE(text.startsWith("slot=")) << text.toStdString();

    // RPC: the route requires a type/fxType key (frontend spelling), the gate
    // passes the id through, and the route reports Null on success.
    const QJsonObject rpcArgs{ { "trackIndex", 0 }, { "fxType", "plugin" },
                               { "pluginId", id } };
    const auto rpcR = rpc("project.addFxSlot", rpcArgs);
    EXPECT_FALSE(rpcR.isError)
        << rpcR.payload.toObject().value("message").toString().toStdString();
}

// ─── add_track_with_fx: the composite tool takes the SAME pluginId gate ────
// (item-1 hole flagged by FxClapEditionFix: the handler forwarded pluginId to
// addFxSlot UNGATED, so a shadow-edition/unresolvable id silently created a
// track whose slot degraded to a 'none' placeholder in rebuildFXChain.)
//
// There is NO project.addTrackWithFx RPC route (rpc_parity_map.inc: "unresolved
// — no name-derived route"; the frontend adds tracks via project.addTrack and
// then the already-gated project.addFxSlot), so the byte-identical-text proof
// compares the composite's MCP message against the RPC surface running the
// SAME shared validator for the SAME id. Both texts are produced by
// HDAW::fxPluginIdError by construction — these tests prove both wirings.

TEST_F(AddFxParityTest, AddTrackWithFxShadowIdFailsWithSharedGateText) {
    const QJsonObject args{ { "name", "Shadowed" }, { "pluginId", "VavraFX" } };
    EXPECT_TRUE(mcpIsError("add_track_with_fx", args))
        << mcpText("add_track_with_fx", args).toStdString();
    const QString mcpMessage = mcpText("add_track_with_fx", args);
    EXPECT_TRUE(mcpMessage.contains("VavraFX")) << mcpMessage.toStdString();

    const auto rpcR = rpc("project.addFxSlot",
                          QJsonObject{ { "trackId", 0 }, { "pluginId", "VavraFX" } });
    ASSERT_TRUE(rpcR.isError);
    EXPECT_EQ(rpcR.payload.toObject().value("message").toString(), mcpMessage)
        << "byte-identical text from the shared HDAW::fxPluginIdError";

    // The rejected composite must not have created a track, and neither
    // surface may have created a slot.
    EXPECT_EQ(engine->getProjectModel().getTrackListTree().getNumChildren(), 1);
    expectNoFxSlot();
}

TEST_F(AddFxParityTest, AddTrackWithFxUnresolvableIdFailsWithSharedGateText) {
    const QJsonObject args{ { "name", "Bare" },
                            { "pluginId", "definitely-not-a-plugin" } };
    EXPECT_TRUE(mcpIsError("add_track_with_fx", args))
        << mcpText("add_track_with_fx", args).toStdString();
    const QString mcpMessage = mcpText("add_track_with_fx", args);
    EXPECT_TRUE(mcpMessage.contains("definitely-not-a-plugin")) << mcpMessage.toStdString();

    const auto rpcR = rpc("project.addFxSlot",
                          QJsonObject{ { "trackId", 0 },
                                       { "pluginId", "definitely-not-a-plugin" } });
    ASSERT_TRUE(rpcR.isError);
    EXPECT_EQ(rpcR.payload.toObject().value("message").toString(), mcpMessage);
    EXPECT_EQ(engine->getProjectModel().getTrackListTree().getNumChildren(), 1);
    expectNoFxSlot();
}

TEST_F(AddFxParityTest, AddTrackWithFxAcceptsEmptyAndResolvablePluginId) {
    auto tl = engine->getProjectModel().getTrackListTree();
    const int before = tl.getNumChildren();
    auto textOf = [](const QJsonObject& r) {
        const auto c = r.value("content").toArray();
        return c.isEmpty() ? QString()
                           : c[0].toObject().value("text").toString();
    };

    // Empty pluginId — a track WITHOUT a plugin must succeed (the add_fx
    // accept rules: the validator's first branch is empty -> accept).
    const auto bareR = mcpResult("add_track_with_fx", QJsonObject{ { "name", "NoPlugin" } });
    ASSERT_FALSE(bareR.value("isError").toBool()) << textOf(bareR).toStdString();
    EXPECT_EQ(tl.getNumChildren(), before + 1);

    // Resolvable id: the .clap extension fallback resolves WITHOUT a scan
    // cache (ProjectModel.cpp resolvePluginFormat), so this holds anywhere.
    const auto okR = mcpResult("add_track_with_fx",
                               QJsonObject{ { "name", "Probe" },
                                            { "pluginId", "C:/definitely/missing/hdaw_parity_probe.clap" } });
    ASSERT_FALSE(okR.value("isError").toBool()) << textOf(okR).toStdString();
    ASSERT_EQ(tl.getNumChildren(), before + 2);

    // The resolvable call inferred fxType="plugin" and stored the slot with
    // its pluginId — not a silently dropped or ungated insertion.
    const auto fxChain = tl.getChild(before + 1).getChildWithName(IDs::FX_CHAIN);
    ASSERT_TRUE(fxChain.isValid());
    ASSERT_EQ(fxChain.getNumChildren(), 1);
    EXPECT_EQ(fxChain.getChild(0).getProperty(IDs::pluginID).toString().toStdString(),
              "C:/definitely/missing/hdaw_parity_probe.clap");
    // The payload is the shared compact builder's text (juce::JSON::toString(...,
    // true) — compact, but with a space after ':' and ','), so parse it and assert
    // the VALUES: a literal-vs-text comparison would pin JUCE's spacing instead of
    // the contract.
    const auto okObj = QJsonDocument::fromJson(textOf(okR).toUtf8()).object();
    EXPECT_EQ(okObj.value("fxType").toString().toStdString(), "plugin") << textOf(okR).toStdString();
    EXPECT_EQ(okObj.value("trackId").toInt(), before + 1) << textOf(okR).toStdString();
    EXPECT_EQ(okObj.value("routed").toInt(), 1) << textOf(okR).toStdString();
}

// ─── Route keys mirror the MCP tool property names (track family) ──────────
// removeTrack / moveTrack / duplicateTrack took `trackIndex` where their MCP
// twins take `trackId` (renamed 2026-09-23). Same-object assertion as
// bus_send_rpc_test's RouteKeysMirrorToolPropertyNames — kept here because
// that TU belongs to an active sibling. Handing the SAME object to both
// surfaces is what proves it: a trackId -> trackIndex rename back would fail
// the RPC half with -32602 while the MCP half still succeeded.
TEST_F(AddFxParityTest, TrackRouteKeysMirrorToolPropertyNames) {
    auto textOf = [](const QJsonObject& r) {
        const auto c = r.value("content").toArray();
        return c.isEmpty() ? QString()
                           : c[0].toObject().value("text").toString();
    };

    // duplicate_track / project.duplicateTrack — {trackId}.
    const QJsonObject dupArgs{ { "trackId", 0 } };
    const auto dupRpc = rpc("project.duplicateTrack", dupArgs);
    EXPECT_FALSE(dupRpc.isError)
        << dupRpc.payload.toObject().value("message").toString().toStdString();
    const auto dupMcp = mcpResult("duplicate_track", dupArgs);
    EXPECT_FALSE(dupMcp.value("isError").toBool()) << textOf(dupMcp).toStdString();

    // move_track / project.moveTrack — {trackId, newIndex}.
    const QJsonObject moveArgs{ { "trackId", 0 }, { "newIndex", 1 } };
    const auto moveRpc = rpc("project.moveTrack", moveArgs);
    EXPECT_FALSE(moveRpc.isError)
        << moveRpc.payload.toObject().value("message").toString().toStdString();
    const auto moveMcp = mcpResult("move_track", moveArgs);
    EXPECT_FALSE(moveMcp.value("isError").toBool()) << textOf(moveMcp).toStdString();

    // remove_track / project.removeTrack — {trackId}.
    const QJsonObject removeArgs{ { "trackId", 0 } };
    const auto removeRpc = rpc("project.removeTrack", removeArgs);
    EXPECT_FALSE(removeRpc.isError)
        << removeRpc.payload.toObject().value("message").toString().toStdString();
    const auto removeMcp = mcpResult("remove_track", removeArgs);
    EXPECT_FALSE(removeMcp.value("isError").toBool()) << textOf(removeMcp).toStdString();
}

// ─── set_track <-> the 13 project.setTrack* routes ─────────────────────────
// set_track is the ONE MCP tool that shapes the whole track property set while
// the RPC surface splits it into 13 routes (setTrackName / setTrackColor / ...).
// Each case below hands the SAME QJsonObject to the tool AND to the route that
// owns that property, then checks the LIVE tree: a rename back on either side
// (or a route writing a neighbouring property) fails here. It also pins the
// payload-less convention — MCP answers text "ok", the route answers Null
// (a text tool cannot return JSON null), so there is no third shape.
//
// `mute` / `solo` are the ONE documented spelling divergence (asserted at the
// end): the tool's historical keys are `mute`/`solo` (pinned by
// mcp_functionality_test.cpp, and kept deliberately), while the routes spell
// them `muted`/`soloed` — the vocabulary read.getTrack / TrackSnapshot report,
// so the RPC surface stays internally consistent. Both surfaces still write the
// SAME tree property; only the key spelling differs.
TEST_F(AddFxParityTest, SetTrackPropertiesMirrorRouteArgumentNames) {
    struct Case { const char* method; QJsonObject args; };
    const Case cases[] = {
        { "project.setTrackName",         QJsonObject{ { "trackId", 0 }, { "name", "Renamed" } } },
        { "project.setTrackVolume",       QJsonObject{ { "trackId", 0 }, { "volume", 0.5 } } },
        { "project.setTrackPan",          QJsonObject{ { "trackId", 0 }, { "pan", -0.25 } } },
        { "project.setTrackColor",        QJsonObject{ { "trackId", 0 }, { "color", 0x112233 } } },
        { "project.setTrackHidden",       QJsonObject{ { "trackId", 0 }, { "hidden", true } } },
        { "project.setTrackArmed",        QJsonObject{ { "trackId", 0 }, { "armed", true } } },
        { "project.setTrackInputMonitor", QJsonObject{ { "trackId", 0 }, { "inputMonitor", true } } },
        { "project.setTrackHeight",       QJsonObject{ { "trackId", 0 }, { "height", 180 } } },
        { "project.setTrackMidiChannel",  QJsonObject{ { "trackId", 0 }, { "midiChannel", 7 } } },
        { "project.setTrackType",         QJsonObject{ { "trackId", 0 }, { "trackType", 1 } } },
        { "project.setTrackCollapsed",    QJsonObject{ { "trackId", 0 }, { "collapsed", true } } },
    };

    for (const auto& c : cases) {
        // MCP: set_track's schema accepts every one of these property names
        // (its validator refuses unknown keys, so a rename is a hard failure).
        const auto mcpR = mcpResult("set_track", c.args);
        EXPECT_FALSE(mcpR.value("isError").toBool())
            << "set_track refused " << c.method << "'s object: "
            << mcpText("set_track", c.args).toStdString();
        // RPC: the property's own route accepts the SAME object.
        const auto rpcR = rpc(c.method, c.args);
        EXPECT_FALSE(rpcR.isError)
            << c.method << " refused set_track's object: "
            << rpcR.payload.toObject().value("message").toString().toStdString();
    }

    // The LIVE tree — both surfaces applied the same values twice, so the state
    // is deterministic, and every key landed on the property its route owns.
    const auto tl = engine->getProjectModel().getTrackListTree();
    ASSERT_EQ(tl.getNumChildren(), 1);
    const auto t = tl.getChild(0);
    EXPECT_EQ(t.getProperty(IDs::name).toString().toStdString(), "Renamed");
    EXPECT_DOUBLE_EQ(static_cast<double>(t.getProperty(IDs::volume)), 0.5);
    EXPECT_DOUBLE_EQ(static_cast<double>(t.getProperty(IDs::pan)), -0.25);
    EXPECT_EQ(static_cast<int>(t.getProperty(IDs::color)), 0x112233);
    EXPECT_TRUE(static_cast<bool>(t.getProperty(IDs::isHidden)));
    EXPECT_TRUE(static_cast<bool>(t.getProperty(IDs::isArm)));
    EXPECT_TRUE(static_cast<bool>(t.getProperty(IDs::inputMonitor)));
    EXPECT_DOUBLE_EQ(static_cast<double>(t.getProperty(IDs::trackHeight)), 180.0);
    EXPECT_EQ(static_cast<int>(t.getProperty(IDs::midiChannel)), 7);
    EXPECT_EQ(static_cast<int>(t.getProperty(IDs::trackType)), 1);
    EXPECT_TRUE(static_cast<bool>(t.getProperty(IDs::isCollapsed)));

    // Payload-less mutations: text "ok" on MCP, Null on the route. Pinned so
    // neither surface grows a second shape for the same operation.
    const QJsonObject armedArgs{ { "trackId", 0 }, { "armed", true } };
    EXPECT_EQ(mcpText("set_track", armedArgs).trimmed().toStdString(), "ok");
    EXPECT_TRUE(rpc("project.setTrackArmed", armedArgs).payload.isNull());

    // The two divergent spellings — each surface on its documented key, both
    // landing the SAME tree property (see the note above the test).
    const QJsonObject mcpMute{ { "trackId", 0 }, { "mute", true } };
    EXPECT_FALSE(mcpResult("set_track", mcpMute).value("isError").toBool())
        << mcpText("set_track", mcpMute).toStdString();
    const QJsonObject rpcMute{ { "trackId", 0 }, { "muted", true } };
    EXPECT_FALSE(rpc("project.setTrackMuted", rpcMute).isError);
    EXPECT_TRUE(static_cast<bool>(t.getProperty(IDs::isMuted)));

    const QJsonObject mcpSolo{ { "trackId", 0 }, { "solo", true } };
    EXPECT_FALSE(mcpResult("set_track", mcpSolo).value("isError").toBool())
        << mcpText("set_track", mcpSolo).toStdString();
    const QJsonObject rpcSolo{ { "trackId", 0 }, { "soloed", true } };
    EXPECT_FALSE(rpc("project.setTrackSoloed", rpcSolo).isError);
    EXPECT_TRUE(static_cast<bool>(t.getProperty(IDs::isSoloed)));
}

// ─── Folder moves: the two capabilities that were RPC-only ─────────────────
// move_track_into_folder / move_track_out_of_folder had no MCP twin at all, so
// an agent could not group tracks. The SAME argument object drives both
// surfaces, and the assertion is on the LIVE tree — folder membership is a PAIR
// of positional refs (the folder's childIds CSV and the child's parentId), so a
// surface that only set one of them would still pass a "the call returned" test.
TEST_F(AddFxParityTest, FolderMoveTwinsShareArgumentObjectAndTreeEffect) {
    auto& cmds = engine->getProjectCommands();
    const int folder = cmds.addTrack("Folder", -1, -1, 2);   // trackType 2 = folder
    const int child = cmds.addTrack("Child");
    ASSERT_GE(folder, 1);
    ASSERT_GE(child, 2);
    engine->drainPendingRoutingRebuild();

    const QJsonObject intoArgs{ { "trackId", child }, { "folderId", folder } };
    EXPECT_FALSE(mcpIsError("move_track_into_folder", intoArgs))
        << mcpText("move_track_into_folder", intoArgs).toStdString();
    EXPECT_FALSE(rpc("project.moveTrackIntoFolder", intoArgs).isError);
    engine->drainPendingRoutingRebuild();

    auto tl = engine->getProjectModel().getTrackListTree();
    EXPECT_EQ(tl.getChild(folder).getProperty(IDs::childIds).toString().toStdString(),
              std::to_string(child));
    EXPECT_EQ(static_cast<int>(tl.getChild(child).getProperty(IDs::parentId, -1)), folder);
    // Payload-less mutation: text "ok" on MCP, Null on the route.
    EXPECT_EQ(mcpText("move_track_into_folder", intoArgs).trimmed().toStdString(), "ok");
    EXPECT_TRUE(rpc("project.moveTrackIntoFolder", intoArgs).payload.isNull());

    const QJsonObject outArgs{ { "trackId", child } };
    EXPECT_FALSE(mcpIsError("move_track_out_of_folder", outArgs))
        << mcpText("move_track_out_of_folder", outArgs).toStdString();
    EXPECT_FALSE(rpc("project.moveTrackOutOfFolder", outArgs).isError);
    engine->drainPendingRoutingRebuild();

    // The folder-less sentinel is -1 on both refs (the vocabulary
    // AudioEngineCommands_Helpers.h documents).
    EXPECT_EQ(tl.getChild(folder).getProperty(IDs::childIds).toString().toStdString(), "");
    EXPECT_EQ(static_cast<int>(tl.getChild(child).getProperty(IDs::parentId, -1)), -1);
    EXPECT_EQ(mcpText("move_track_out_of_folder", outArgs).trimmed().toStdString(), "ok");
    EXPECT_TRUE(rpc("project.moveTrackOutOfFolder", outArgs).payload.isNull());
}

// ─── add_track_with_fx / project.addTrackWithFx ────────────────────────────
// The composite had NO route (rpc_parity_map.inc: "unresolved"), so the RPC
// surface could not reach its pluginId gate. Both surfaces now run ONE body
// (src/common/AddTrackWithFx.h), so the SAME object must produce the same
// payload — the id differs because each call creates its own track.
TEST_F(AddFxParityTest, AddTrackWithFxRouteMirrorsToolPayload) {
    auto textOf = [](const QJsonObject& r) {
        const auto c = r.value("content").toArray();
        return c.isEmpty() ? QString() : c[0].toObject().value("text").toString();
    };
    const QJsonObject args{ { "name", "Twin" }, { "fxType", "eq" } };

    const auto mcpR = mcpResult("add_track_with_fx", args);
    ASSERT_FALSE(mcpR.value("isError").toBool()) << textOf(mcpR).toStdString();
    const QJsonValue viaMcp = QJsonDocument::fromJson(textOf(mcpR).toUtf8()).object();
    ASSERT_TRUE(viaMcp.isObject()) << "the tool answers compact JSON";

    const auto rpcR = rpc("project.addTrackWithFx", args);
    ASSERT_FALSE(rpcR.isError)
        << rpcR.payload.toObject().value("message").toString().toStdString();
    const QJsonValue viaRpc = rpcR.payload;
    ASSERT_TRUE(viaRpc.isObject()) << "the route answers the same object";

    // Same keys; same values except the freshly allocated index.
    EXPECT_EQ(viaRpc.toObject().size(), viaMcp.toObject().size());
    EXPECT_TRUE(viaRpc.toObject().contains("trackId"));
    EXPECT_TRUE(viaRpc.toObject().contains("routed"));
    EXPECT_TRUE(viaRpc.toObject().contains("fxType"));
    EXPECT_EQ(viaRpc.toObject().value("fxType"), viaMcp.toObject().value("fxType"));
    EXPECT_EQ(viaRpc.toObject().value("routed"), viaMcp.toObject().value("routed"));
    EXPECT_EQ(viaMcp.toObject().value("routed").toInt(), 1);
    const int mcpId = viaMcp.toObject().value("trackId").toInt(-1);
    const int rpcId = viaRpc.toObject().value("trackId").toInt(-1);
    EXPECT_GE(mcpId, 0);
    EXPECT_GE(rpcId, 0);
    EXPECT_NE(mcpId, rpcId) << "each call creates its own track";

    // Both really landed a track with the requested slot, on the LIVE tree.
    auto tl = engine->getProjectModel().getTrackListTree();
    for (const int id : { mcpId, rpcId }) {
        ASSERT_LT(id, tl.getNumChildren()) << "trackId " << id << " is not in TRACK_LIST";
        EXPECT_EQ(tl.getChild(id).getProperty(IDs::name).toString().toStdString(), "Twin");
        const auto fxChain = tl.getChild(id).getChildWithName(IDs::FX_CHAIN);
        ASSERT_TRUE(fxChain.isValid());
        ASSERT_EQ(fxChain.getNumChildren(), 1);
        EXPECT_EQ(fxChain.getChild(0).getProperty(IDs::fxType).toString().toStdString(), "eq");
    }
}

// The composite's gate, now shared by construction: an ungated pluginId must
// fail with the identical text on the route too, and leave NO track behind.
TEST_F(AddFxParityTest, AddTrackWithFxRouteRejectsUngatedPluginIdIdentically) {
    const QJsonObject shadow{ { "name", "Shadowed" }, { "pluginId", "VavraFX" } };
    expectSameFailure("add_track_with_fx", "project.addTrackWithFx", shadow);

    const QJsonObject unresolvable{ { "name", "Bare" },
                                    { "pluginId", "definitely-not-a-plugin" } };
    expectSameFailure("add_track_with_fx", "project.addTrackWithFx", unresolvable);
    EXPECT_TRUE(mcpText("add_track_with_fx", unresolvable).contains("definitely-not-a-plugin"));

    // Two refused composites, zero tracks (the gate runs BEFORE the track).
    EXPECT_EQ(engine->getProjectModel().getTrackListTree().getNumChildren(), 1);
    expectNoFxSlot();
}

// ─── remove_track / project.removeTrack: the SAME guard ────────────────────
// The route had NO guard: it destroyed a track's clips with no preview and no
// refusal, while the tool refused. Both now run src/common/TrackRemoveGuard.h,
// so the dryRun preview is one text (tool text / bare JSON string) and the
// refusal is one text (tool error / -32602 message), with no mutation on either
// surface until force:true.
TEST_F(AddFxParityTest, RemoveTrackGuardIsIdenticalOnBothSurfaces) {
    auto& cmds = engine->getProjectCommands();
    ASSERT_GT(cmds.addMidiClip(0, 0.0, 4.0, "Guard"), 0);
    engine->drainPendingRoutingRebuild();
    const int before = engine->getProjectModel().getTrackListTree().getNumChildren();
    ASSERT_EQ(before, 1);

    // dryRun: identical preview text, and NEITHER surface mutates.
    const QJsonObject dryArgs{ { "trackId", 0 }, { "dryRun", true } };
    const auto rpcDry = rpc("project.removeTrack", dryArgs);
    ASSERT_FALSE(rpcDry.isError)
        << rpcDry.payload.toObject().value("message").toString().toStdString();
    EXPECT_FALSE(mcpIsError("remove_track", dryArgs));
    const QString mcpDry = mcpText("remove_track", dryArgs);
    EXPECT_EQ(rpcDry.payload.toString(), mcpDry)
        << "the dryRun preview is one text on both surfaces";
    EXPECT_EQ(mcpDry.toStdString(),
              "would remove track 0 (Track), 1 clips. Pass force:true to confirm deletion of clips.");
    EXPECT_EQ(engine->getProjectModel().getTrackListTree().getNumChildren(), before)
        << "a dryRun must not mutate";

    // Refusal: clips at stake and no force — identical text, still no mutation.
    const QJsonObject noForce{ { "trackId", 0 } };
    const auto rpcRefuse = rpc("project.removeTrack", noForce);
    ASSERT_TRUE(rpcRefuse.isError) << "the route removed a clip-carrying track unforced";
    EXPECT_EQ(rpcRefuse.payload.toObject().value("code").toInt(), -32602);
    EXPECT_EQ(rpcRefuse.payload.toObject().value("message").toString(),
              mcpText("remove_track", noForce));
    EXPECT_TRUE(mcpIsError("remove_track", noForce));
    EXPECT_EQ(mcpText("remove_track", noForce).toStdString(),
              "track 0 (Track) has 1 clips. Pass force:true to confirm deletion.");
    EXPECT_EQ(engine->getProjectModel().getTrackListTree().getNumChildren(), before);

    // force:true removes for real on both surfaces, with the same payload. The
    // scene is rebuilt identically before the second surface runs. (The text is
    // read from the ONE call — re-calling the tool here would remove a second
    // track, since the first removal already succeeded.)
    auto textOf = [](const QJsonObject& r) {
        const auto c = r.value("content").toArray();
        return c.isEmpty() ? QString() : c[0].toObject().value("text").toString();
    };
    const QJsonObject forced{ { "trackId", 0 }, { "force", true } };
    const auto mcpForce = mcpResult("remove_track", forced);
    ASSERT_FALSE(mcpForce.value("isError").toBool()) << textOf(mcpForce).toStdString();
    const QJsonValue viaMcp =
        QJsonDocument::fromJson(textOf(mcpForce).toUtf8()).object();
    EXPECT_TRUE(viaMcp.toObject().value("ok").toBool());
    EXPECT_EQ(engine->getProjectModel().getTrackListTree().getNumChildren(), before - 1);

    ASSERT_EQ(cmds.addTrack("Track"), before - 1);
    ASSERT_GT(cmds.addMidiClip(before - 1, 0.0, 4.0, "Guard"), 0);
    engine->drainPendingRoutingRebuild();
    const auto rpcForce = rpc("project.removeTrack", forced);
    ASSERT_FALSE(rpcForce.isError)
        << rpcForce.payload.toObject().value("message").toString().toStdString();
    EXPECT_EQ(rpcForce.payload, viaMcp) << "the same removal payload on both surfaces";
    EXPECT_EQ(engine->getProjectModel().getTrackListTree().getNumChildren(), before - 1);
}

// ─── The retired `trackIndex` is refused on BOTH surfaces ─────────────────
// Same shape as BusSendRpcTest.LegacySendRoutesRejectTrackIndexOnBothSurfaces:
// the route's -32602 must NAME `trackId` and never the retired spelling, and the
// tool's schema must refuse the unknown key outright. (Not a byte-identical-text
// assertion: an argument-name failure surfaces from DIFFERENT validators per
// surface — the route's requireInt vs the tool's JSON schema.)
TEST_F(AddFxParityTest, SetTrackRoutesRejectRetiredTrackIndexOnBothSurfaces) {
    // `rest` is the ROUTE's property set (what the route requires besides trackId);
    // `mcpRest` is the same request in set_track's own vocabulary where the two
    // differ — the tool spells mute/solo as `mute`/`solo` while the routes spell
    // them `muted`/`soloed`, so handing the route's object to the tool would make
    // its schema reject THAT key first and the retired-key assertion would pass
    // for the wrong reason.
    struct Case { const char* method; const char* tool; QJsonObject rest; QJsonObject mcpRest; };
    const Case cases[] = {
        { "project.setTrackName",         "set_track", QJsonObject{ { "name", "Nope" } }, {} },
        { "project.setTrackVolume",       "set_track", QJsonObject{ { "volume", 0.5 } }, {} },
        { "project.setTrackPan",          "set_track", QJsonObject{ { "pan", 0.5 } }, {} },
        { "project.setTrackColor",        "set_track", QJsonObject{ { "color", 0x112233 } }, {} },
        { "project.setTrackMuted",        "set_track", QJsonObject{ { "muted", true } },
                                                       QJsonObject{ { "mute", true } } },
        { "project.setTrackSoloed",       "set_track", QJsonObject{ { "soloed", true } },
                                                       QJsonObject{ { "solo", true } } },
        { "project.setTrackArmed",        "set_track", QJsonObject{ { "armed", true } }, {} },
        { "project.setTrackInputMonitor", "set_track", QJsonObject{ { "inputMonitor", true } }, {} },
        { "project.setTrackHeight",       "set_track", QJsonObject{ { "height", 120 } }, {} },
        { "project.setTrackMidiChannel",  "set_track", QJsonObject{ { "midiChannel", 3 } }, {} },
        { "project.setTrackType",         "set_track", QJsonObject{ { "trackType", 1 } }, {} },
        { "project.setTrackCollapsed",    "set_track", QJsonObject{ { "collapsed", true } }, {} },
        { "project.setTrackHidden",       "set_track", QJsonObject{ { "hidden", true } }, {} },
        { "project.moveTrackIntoFolder",  "move_track_into_folder",
              QJsonObject{ { "folderId", 1 } }, {} },
        { "project.moveTrackOutOfFolder", "move_track_out_of_folder", QJsonObject{}, {} },
    };

    for (const auto& c : cases) {
        // `mcpRest` empty means "the route's own set is already tool-valid" — but an
        // intentionally empty rest (move_track_out_of_folder) must stay empty, so the
        // fallback is decided by the tool, not by emptiness: set_track always needs a
        // property for the second half of this test to be meaningful, and that half
        // passes `rest` for every non-set_track tool.
        const bool toolIsSetTrack = QString(c.tool) == "set_track";
        const QJsonObject mcpRest = toolIsSetTrack ? c.mcpRest : c.rest;

        // The retired name in place of trackId.
        QJsonObject renamed = c.rest;
        renamed["trackIndex"] = 0;
        const auto r = rpc(c.method, renamed);
        ASSERT_TRUE(r.isError) << c.method << " accepted the retired trackIndex key";
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602) << c.method;
        const QString rpcMsg = r.payload.toObject().value("message").toString();
        EXPECT_TRUE(rpcMsg.contains("trackId")) << c.method << ": " << rpcMsg.toStdString();
        EXPECT_FALSE(rpcMsg.contains("trackIndex"))
            << c.method << " still names the retired trackIndex: " << rpcMsg.toStdString();
        // The tool refuses the unknown key (its validator rejects it outright).
        QJsonObject renamedMcp = mcpRest;
        renamedMcp["trackIndex"] = 0;
        EXPECT_TRUE(mcpIsError(c.tool, renamedMcp))
            << c.tool << " accepted the retired trackIndex key";
        EXPECT_TRUE(mcpText(c.tool, renamedMcp).contains("trackIndex"))
            << c.tool << " should name the offending key: "
            << mcpText(c.tool, renamedMcp).toStdString();

        // trackId absent entirely: both surfaces fail AND both name trackId.
        const auto r2 = rpc(c.method, c.rest);
        ASSERT_TRUE(r2.isError) << c.method;
        const QString rpcMissing = r2.payload.toObject().value("message").toString();
        EXPECT_TRUE(rpcMissing.contains("trackId")) << c.method << ": " << rpcMissing.toStdString();
        EXPECT_TRUE(mcpIsError(c.tool, mcpRest)) << c.tool;
        EXPECT_TRUE(mcpText(c.tool, mcpRest).contains("trackId"))
            << c.tool << ": " << mcpText(c.tool, mcpRest).toStdString();
    }
}

} // namespace
