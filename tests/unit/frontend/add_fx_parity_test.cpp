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
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <memory>

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
    const auto okText = textOf(okR);
    EXPECT_TRUE(okText.contains("\"fxType\":\"plugin\"")) << okText.toStdString();
    EXPECT_TRUE(okText.contains("\"trackId\":" + QString::number(before + 1)))
        << okText.toStdString();
    EXPECT_TRUE(okText.contains("\"routed\":1")) << okText.toStdString();
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

} // namespace
