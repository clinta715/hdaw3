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
#include <QDir>
#include <QFile>
#include <QString>

#include <tuple>

#include "engine/AudioEngine.h"
#include "engine/PsyFmState.h"   // B2b: decodeRoutes for the psy_fm matrix seam
#include "common/AutomationPresetRequest.h"  // B2: parse-level sections regression
#include <juce_audio_formats/juce_audio_formats.h>  // B6: twin-test wav staging
#include "frontend/FrontendRouter.h"
#include "common/TrackIdRefs.h"   // design B3: stable-id folder refs
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <memory>
#include <string>

namespace {

// ─── design B3: resolve-and-compare helpers ───────────────────────────────
// A folder's childTrackIDs / a child's parentTrackID hold stable trackIDs; the
// tests assert WHICH entity each ref resolves to, never a raw number.
std::string nameNamedByID(const juce::ValueTree& trackList, int id)
{
    const int idx = HDAW::trackIndexForID(trackList, id);
    return idx < 0 ? std::string("<none>")
                   : trackList.getChild(idx).getProperty(IDs::name).toString().toStdString();
}

std::vector<std::string> childNamesOf(const juce::ValueTree& trackList, const juce::ValueTree& folder)
{
    std::vector<std::string> names;
    for (int id : HDAW::parseIDList(folder, IDs::childTrackIDs))
        names.push_back(nameNamedByID(trackList, id));
    return names;
}

std::string parentNameOf(const juce::ValueTree& trackList, const juce::ValueTree& track)
{
    return nameNamedByID(trackList, static_cast<int>(track.getProperty(IDs::parentTrackID, -1)));
}

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

    // Tool payloads are compact JSON text on success and a bare message on
    // failure, so dispatch on the parsed root exactly like the MCP client does
    // (bus_send_rpc_test's shape).
    QJsonValue mcpValue(const QString& tool, const QJsonObject& args) {
        const QString text = mcpText(tool, args);
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isArray()) return QJsonValue(doc.array());
        if (doc.isObject()) return QJsonValue(doc.object());
        return QJsonValue(text);
    }
    QJsonValue rpcPayload(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_FALSE(r.isError)
            << "RPC " << method.toStdString() << " errored: "
            << r.payload.toObject().value("message").toString().toStdString();
        return r.payload;
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

    // The B2b disagreement on a SPELLING-SPLIT twin. The tools read `trackId`
    // and the fx/automation/plugin-family routes read `trackIndex`
    // (spelling-preserving keys — each surface keeps its own historical
    // positional name, Router_Project.cpp's "spelling-preserving" note), so
    // the ONE resolver template words the two failures with different keys and
    // a shared argument object cannot even express the same request on both
    // surfaces. Each surface gets its OWN object and its own exact text — the
    // shared rule verbatim, one spelling per surface — and neither mutates.
    void expectDisagreementSpelled(const QString& tool, const QString& method,
                                   const QJsonObject& toolArgs, const QJsonObject& routeArgs,
                                   const char* toolIndexKey, const char* routeIndexKey,
                                   int positional, int stableID) {
        const auto r = rpc(method, routeArgs);
        ASSERT_TRUE(r.isError) << "expected " << method.toStdString() << " to fail";
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
        EXPECT_EQ(r.payload.toObject().value("message").toString().toStdString(),
                  std::string(routeIndexKey) + " " + std::to_string(positional)
                      + " and trackID " + std::to_string(stableID) + " disagree");
        EXPECT_TRUE(mcpIsError(tool, toolArgs)) << "expected " << tool.toStdString() << " to fail";
        EXPECT_EQ(mcpText(tool, toolArgs).toStdString(),
                  std::string(toolIndexKey) + " " + std::to_string(positional)
                      + " and trackID " + std::to_string(stableID) + " disagree");
    }

    // The seeded pair every B2b family test drives: the fixture's track 0
    // ("Track") is the DECOY and track 1 ("Target") is the id's track, so an
    // id a surface silently ignored and defaulted to index 0 would write to
    // the decoy and fail the untouched-decoy assertions below.
    struct TwoTracks { int decoyIdx = 0, targetIdx = 1, decoyID = 0, targetID = 0; };
    TwoTracks seedTargetAndDecoy() {
        auto& cmds = engine->getProjectCommands();
        TwoTracks t;
        t.targetIdx = cmds.addTrack("Target");
        EXPECT_EQ(t.targetIdx, 1);
        engine->drainPendingRoutingRebuild();
        const auto tl = engine->getProjectModel().getTrackListTree();
        t.decoyID = static_cast<int>(tl.getChild(t.decoyIdx).getProperty(IDs::trackID, 0));
        t.targetID = static_cast<int>(tl.getChild(t.targetIdx).getProperty(IDs::trackID, 0));
        EXPECT_GT(t.decoyID, 0);
        EXPECT_GT(t.targetID, 0);
        EXPECT_NE(t.decoyID, t.targetID);
        return t;
    }

    // An FX slot's persisted tree node (the source of truth the ReadModel
    // projects and rebuildFXChain restores).
    juce::ValueTree fxSlotNode(int trackIdx, int slotIdx) {
        return engine->getProjectModel().getTrackListTree()
            .getChild(trackIdx).getChildWithName(IDs::FX_CHAIN).getChild(slotIdx);
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
// of STABLE-id refs (the folder's childTrackIDs and the child's parentTrackID),
// so a surface that only set one of them would still pass a "the call returned"
// test. Each ref is resolved to the track it names, never compared as a number.
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
    EXPECT_EQ(childNamesOf(tl, tl.getChild(folder)), (std::vector<std::string>{ "Child" }));
    EXPECT_EQ(parentNameOf(tl, tl.getChild(child)), "Folder");
    // Payload-less mutation: text "ok" on MCP, Null on the route.
    EXPECT_EQ(mcpText("move_track_into_folder", intoArgs).trimmed().toStdString(), "ok");
    EXPECT_TRUE(rpc("project.moveTrackIntoFolder", intoArgs).payload.isNull());

    const QJsonObject outArgs{ { "trackId", child } };
    EXPECT_FALSE(mcpIsError("move_track_out_of_folder", outArgs))
        << mcpText("move_track_out_of_folder", outArgs).toStdString();
    EXPECT_FALSE(rpc("project.moveTrackOutOfFolder", outArgs).isError);
    engine->drainPendingRoutingRebuild();

    // After the move-out the folder claims nothing and the child is folder-less.
    EXPECT_TRUE(childNamesOf(tl, tl.getChild(folder)).empty());
    EXPECT_EQ(parentNameOf(tl, tl.getChild(child)), "<none>");
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

// ─── B2: the stable id drives set_track and every route it mirrors ─────────
// set_track fans out to 13 project.setTrack* routes; B2 gives all of them (and
// the tool) the stable `trackID` next to the positional `trackId`. Each case
// below sends the property's own object with `trackID` ONLY — no positional key
// at all — to the tool AND to the route, and the assertion is on the LIVE tree,
// so a surface that kept parsing only `trackId` would write to index 0 (or fail)
// and be caught. The fixture seeds ONE track, which is exactly the trap: index 0
// is always valid, so a silently ignored id would land a write anyway.
TEST_F(AddFxParityTest, StableTrackIdDrivesSetTrackAndEveryRoute) {
    auto& cmds = engine->getProjectCommands();
    // A second track, so the id cannot accidentally be 0's index.
    const int other = cmds.addTrack("Other");
    ASSERT_GE(other, 1);
    engine->drainPendingRoutingRebuild();
    const auto tl = engine->getProjectModel().getTrackListTree();
    const int id0 = static_cast<int>(tl.getChild(0).getProperty(IDs::trackID, 0));
    const int id1 = static_cast<int>(tl.getChild(1).getProperty(IDs::trackID, 0));
    ASSERT_GT(id0, 0);
    ASSERT_NE(id0, id1);

    struct Case { const char* method; QJsonObject args; };
    const Case cases[] = {
        { "project.setTrackName",         QJsonObject{ { "name", "ByID" } } },
        { "project.setTrackVolume",       QJsonObject{ { "volume", 0.375 } } },
        { "project.setTrackPan",          QJsonObject{ { "pan", -0.5 } } },
        { "project.setTrackColor",        QJsonObject{ { "color", 0x445566 } } },
        { "project.setTrackHidden",       QJsonObject{ { "hidden", true } } },
        { "project.setTrackArmed",        QJsonObject{ { "armed", true } } },
        { "project.setTrackInputMonitor", QJsonObject{ { "inputMonitor", true } } },
        { "project.setTrackHeight",       QJsonObject{ { "height", 222 } } },
        { "project.setTrackMidiChannel",  QJsonObject{ { "midiChannel", 9 } } },
        { "project.setTrackType",         QJsonObject{ { "trackType", 1 } } },
        { "project.setTrackCollapsed",    QJsonObject{ { "collapsed", true } } },
    };

    for (const auto& c : cases) {
        // The tool: `trackID` + the property, nothing positional.
        QJsonObject viaTool = c.args;
        viaTool["trackID"] = id1;
        const auto mcpR = mcpResult("set_track", viaTool);
        EXPECT_FALSE(mcpR.value("isError").toBool())
            << "set_track refused " << c.method << ": "
            << mcpText("set_track", viaTool).toStdString();
        // The route that owns the property: the SAME object.
        const auto rpcR = rpc(c.method, viaTool);
        EXPECT_FALSE(rpcR.isError)
            << c.method << " refused the id-addressed object: "
            << rpcR.payload.toObject().value("message").toString().toStdString();
    }

    // Both surfaces wrote through the ID: track 1 carries every property, and
    // the neighbour (which a silently ignored id would have targeted) carries
    // none of them.
    engine->drainPendingRoutingRebuild();
    const auto t1 = tl.getChild(1);
    const auto t0 = tl.getChild(0);
    EXPECT_EQ(t1.getProperty(IDs::name).toString().toStdString(), "ByID");
    EXPECT_DOUBLE_EQ(static_cast<double>(t1.getProperty(IDs::volume)), 0.375);
    EXPECT_DOUBLE_EQ(static_cast<double>(t1.getProperty(IDs::pan)), -0.5);
    EXPECT_EQ(static_cast<int>(t1.getProperty(IDs::color)), 0x445566);
    EXPECT_TRUE(static_cast<bool>(t1.getProperty(IDs::isHidden)));
    EXPECT_TRUE(static_cast<bool>(t1.getProperty(IDs::isArm)));
    EXPECT_TRUE(static_cast<bool>(t1.getProperty(IDs::inputMonitor)));
    EXPECT_DOUBLE_EQ(static_cast<double>(t1.getProperty(IDs::trackHeight)), 222.0);
    EXPECT_EQ(static_cast<int>(t1.getProperty(IDs::midiChannel)), 9);
    EXPECT_EQ(static_cast<int>(t1.getProperty(IDs::trackType)), 1);
    EXPECT_TRUE(static_cast<bool>(t1.getProperty(IDs::isCollapsed)));
    EXPECT_FALSE(t0.hasProperty(IDs::isHidden)) << "track 0 must be untouched";
    EXPECT_FALSE(static_cast<bool>(t0.getProperty(IDs::isArm)))
        << "createTrackValueTree defaults isArm=false; the id's track set it true";
    EXPECT_FALSE(t0.hasProperty(IDs::isCollapsed));
    EXPECT_EQ(t0.getProperty(IDs::name).toString().toStdString(), "Track");
    EXPECT_NE(static_cast<int>(t0.getProperty(IDs::midiChannel)), 9)
        << "the neighbouring track keeps its own midi channel";
}

// B2's mute/solo pair on the id path, and the ID half of the "one write path"
// claim: the tool's `mute` and the route's `muted` both accept `trackID`.
TEST_F(AddFxParityTest, StableTrackIdDrivesMuteAndSoloOnBothSurfaces) {
    const auto tl = engine->getProjectModel().getTrackListTree();
    const int trackID = static_cast<int>(tl.getChild(0).getProperty(IDs::trackID, 0));
    ASSERT_GT(trackID, 0);

    const QJsonObject mcpMute{ { "trackID", trackID }, { "mute", true } };
    EXPECT_FALSE(mcpResult("set_track", mcpMute).value("isError").toBool())
        << mcpText("set_track", mcpMute).toStdString();
    const QJsonObject rpcMute{ { "trackID", trackID }, { "muted", false } };
    EXPECT_FALSE(rpc("project.setTrackMuted", rpcMute).isError);
    engine->drainPendingRoutingRebuild();
    EXPECT_FALSE(static_cast<bool>(tl.getChild(0).getProperty(IDs::isMuted)))
        << "the two writes landed on one property of one track";

    const QJsonObject mcpSolo{ { "trackID", trackID }, { "solo", true } };
    EXPECT_FALSE(mcpResult("set_track", mcpSolo).value("isError").toBool());
    const QJsonObject rpcSolo{ { "trackID", trackID }, { "soloed", false } };
    EXPECT_FALSE(rpc("project.setTrackSoloed", rpcSolo).isError);
    engine->drainPendingRoutingRebuild();
    EXPECT_FALSE(static_cast<bool>(tl.getChild(0).getProperty(IDs::isSoloed)));
}

// ─── B2: folder membership by stable id ────────────────────────────────────
// The folder target resolves through the same lookup with the folder spellings,
// so `folderID` addresses the folder and `trackID` the child — the pair that
// makes a folder reachable for a caller that only holds identities.
TEST_F(AddFxParityTest, FolderMoveAcceptsStableIds) {
    auto& cmds = engine->getProjectCommands();
    const int folder = cmds.addTrack("Folder", -1, -1, 2);   // trackType 2 = folder
    const int child = cmds.addTrack("Child");
    ASSERT_GE(folder, 1);
    ASSERT_GE(child, 2);
    engine->drainPendingRoutingRebuild();

    auto tl = engine->getProjectModel().getTrackListTree();
    const int folderID = static_cast<int>(tl.getChild(folder).getProperty(IDs::trackID, 0));
    const int childID = static_cast<int>(tl.getChild(child).getProperty(IDs::trackID, 0));
    ASSERT_GT(folderID, 0);
    ASSERT_NE(folderID, childID);

    const QJsonObject intoById{ { "trackID", childID }, { "folderID", folderID } };
    EXPECT_FALSE(mcpIsError("move_track_into_folder", intoById))
        << mcpText("move_track_into_folder", intoById).toStdString();
    engine->drainPendingRoutingRebuild();
    // The membership pair lands by identity: the folder's childTrackIDs carries
    // the child's trackID, the child's parentTrackID the folder's trackID — and
    // each resolves back to the track it names.
    EXPECT_EQ(childNamesOf(tl, tl.getChild(folder)), (std::vector<std::string>{ "Child" }));
    EXPECT_EQ(parentNameOf(tl, tl.getChild(child)), "Folder");
    EXPECT_EQ(static_cast<int>(tl.getChild(child).getProperty(IDs::parentTrackID, -1)), folderID);

    // The route mutates the same membership from the same object.
    EXPECT_FALSE(rpc("project.moveTrackOutOfFolder",
                     QJsonObject{ { "trackID", childID } }).isError);
    engine->drainPendingRoutingRebuild();
    EXPECT_TRUE(childNamesOf(tl, tl.getChild(folder)).empty());
    EXPECT_EQ(parentNameOf(tl, tl.getChild(child)), "<none>");
    EXPECT_EQ(static_cast<int>(tl.getChild(child).getProperty(IDs::parentTrackID, -1)), -1);

    // A disagreement between the two spellings is refused, and the folder is
    // still empty afterwards (a silent pick would have re-parented the child).
    // `folderId 0` names the fixture's own track, whose numeric value is NOT the
    // folder's id — the case where "just use the number" would pick the wrong
    // track.
    const int notTheFolder = 0;
    ASSERT_NE(notTheFolder, folderID);
    const auto clash = rpc("project.moveTrackIntoFolder",
                           QJsonObject{ { "trackID", childID }, { "folderId", notTheFolder },
                                        { "folderID", folderID } });
    ASSERT_TRUE(clash.isError);
    EXPECT_EQ(clash.payload.toObject().value("code").toInt(), -32602);
    EXPECT_EQ(clash.payload.toObject().value("message").toString().toStdString(),
              "folderId " + std::to_string(notTheFolder) + " and folderID "
                  + std::to_string(folderID) + " disagree");
    EXPECT_TRUE(mcpIsError("move_track_into_folder",
                           QJsonObject{ { "trackID", childID }, { "folderId", notTheFolder },
                                        { "folderID", folderID } }));
    engine->drainPendingRoutingRebuild();
    EXPECT_TRUE(childNamesOf(tl, tl.getChild(folder)).empty());
    EXPECT_EQ(parentNameOf(tl, tl.getChild(child)), "<none>")
        << "a refused folder move must not re-parent the child";
}

// ═══ B2b: the stable id across the fx / automation / plugin families ═══════
// The scope B2 cut (common/StableRefResolve.h's post-B2b note): every tool and
// RPC twin that addresses a track now parses it through the ONE shared rule —
// `trackID` wins, an unknown id errors naming it, and a positional argument
// naming a DIFFERENT entity errors naming both. One test per family, each on
// BOTH surfaces (the MCP tool and its rpc_parity_map twin), on a seeded pair
// where track 1 is the id's track and track 0 the decoy:
//   (a) id-only `{trackID}` — no positional key — drives the RIGHT track's
//       slot / lane / state and the decoy stays untouched (asserted on the
//       resulting state, never "the call returned");
//   (b) `{trackID: 4242}` — `unknown trackID 4242`, byte-identical on both;
//   (c) `{trackId/trackIndex: 0, trackID: <other id>}` — the disagreement,
//       nothing mutated. The tools word it `trackId …` (their own positional
//       spelling); the fx / automation / plugin-family ROUTES spell the
//       positional key `trackIndex` (spelling-preserving keys), so on those
//       twins each surface's exact text is pinned separately — one resolver
//       template, one spelling per surface. Twins that resolve through the
//       shared helpers or default keys (fm_synth, matrix, automation_preset /
//       movement plan) keep the byte-identical expectSameFailure.

// ─── fx slot: set_fx_bypass ↔ project.setFxSlotBypassed ────────────────────
TEST_F(AddFxParityTest, StableTrackIdDrivesFxSlotBypassOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    // One eq slot per track (positional setup), so the id has to pick.
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.decoyIdx }, { "fxType", "eq" } })
                    .startsWith("slot="));
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "eq" } })
                    .startsWith("slot="));

    // (a) id-only tool: the TARGET's slot flips, the decoy's does not.
    const QJsonObject toolById{ { "trackID", t.targetID }, { "slotIndex", 0 }, { "bypassed", true } };
    EXPECT_FALSE(mcpIsError("set_fx_bypass", toolById))
        << mcpText("set_fx_bypass", toolById).toStdString();
    EXPECT_TRUE(engine->getReadModel().getFxSlots(t.targetIdx)[0].bypassed);
    EXPECT_FALSE(engine->getReadModel().getFxSlots(t.decoyIdx)[0].bypassed)
        << "the decoy must stay untouched";

    // …and id-only on the route: the same id's slot flips back.
    EXPECT_FALSE(rpc("project.setFxSlotBypassed",
                     QJsonObject{ { "trackID", t.targetID }, { "slotIndex", 0 },
                                  { "bypassed", false } }).isError);
    EXPECT_FALSE(engine->getReadModel().getFxSlots(t.targetIdx)[0].bypassed);
    EXPECT_FALSE(engine->getReadModel().getFxSlots(t.decoyIdx)[0].bypassed);

    // (b) unknown id: one text on both surfaces (the stable spelling is
    // `trackID` everywhere, so the text is spelling-independent).
    const QJsonObject unknown{ { "trackID", 4242 }, { "slotIndex", 0 }, { "bypassed", true } };
    expectSameFailure("set_fx_bypass", "project.setFxSlotBypassed", unknown);
    EXPECT_EQ(mcpText("set_fx_bypass", unknown).toStdString(), "unknown trackID 4242");

    // (c) disagreement: positional 0 (the decoy) vs the target's id.
    expectDisagreementSpelled("set_fx_bypass", "project.setFxSlotBypassed",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 }, { "bypassed", true } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 }, { "bypassed", true } },
                              "trackId", "trackIndex", 0, t.targetID);
    EXPECT_FALSE(engine->getReadModel().getFxSlots(t.decoyIdx)[0].bypassed)
        << "a refusal must not fall back to the positional track";
    EXPECT_FALSE(engine->getReadModel().getFxSlots(t.targetIdx)[0].bypassed);
}

// ─── automation: add_automation_point ↔ project.addAutomationPoint ─────────
TEST_F(AddFxParityTest, StableTrackIdDrivesAutomationPointsOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    auto& cmds = engine->getProjectCommands();
    ASSERT_TRUE(cmds.addAutomationLane(t.decoyIdx, "Pre", 2000));
    ASSERT_TRUE(cmds.addAutomationLane(t.targetIdx, "Pre", 2000));
    engine->drainPendingRoutingRebuild();
    auto points = [&](int track) {
        return engine->getReadModel().getAutomationPoints(track, "Pre");
    };
    ASSERT_TRUE(points(t.decoyIdx).empty());
    ASSERT_TRUE(points(t.targetIdx).empty());

    // (a) id-only tool: the point lands on the id's OWN lane…
    const QJsonObject toolById{ { "trackID", t.targetID }, { "lane", "Pre" },
                                { "time", 1.0 }, { "value", 0.5 } };
    EXPECT_FALSE(mcpIsError("add_automation_point", toolById))
        << mcpText("add_automation_point", toolById).toStdString();
    EXPECT_EQ(points(t.targetIdx).size(), 1u);
    EXPECT_TRUE(points(t.decoyIdx).empty()) << "the decoy's lane must stay empty";

    // …and id-only on the route: a second point on the same id's lane.
    EXPECT_FALSE(rpc("project.addAutomationPoint",
                     QJsonObject{ { "trackID", t.targetID }, { "lane", "Pre" },
                                  { "time", 2.0 }, { "value", 0.25 } }).isError);
    EXPECT_EQ(points(t.targetIdx).size(), 2u);
    EXPECT_TRUE(points(t.decoyIdx).empty());

    // (b) unknown id: identical text.
    const QJsonObject unknown{ { "trackID", 4242 }, { "lane", "Pre" },
                               { "time", 1.0 }, { "value", 0.5 } };
    expectSameFailure("add_automation_point", "project.addAutomationPoint", unknown);
    EXPECT_EQ(mcpText("add_automation_point", unknown).toStdString(), "unknown trackID 4242");

    // (c) disagreement on each surface's own positional spelling; no mutation.
    expectDisagreementSpelled("add_automation_point", "project.addAutomationPoint",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "lane", "Pre" }, { "time", 3.0 }, { "value", 0.75 } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID },
                                           { "lane", "Pre" }, { "time", 3.0 }, { "value", 0.75 } },
                              "trackId", "trackIndex", 0, t.targetID);
    EXPECT_EQ(points(t.targetIdx).size(), 2u);
    EXPECT_TRUE(points(t.decoyIdx).empty());
}

// ─── automation preset / movement plan: the SHARED entry points ────────────
// automation_preset ↔ project.applyAutomationPreset and apply_movement_plan ↔
// project.applyMovementPlan both resolve the ref INSIDE the one shared helper
// (src/common/AutomationPresetRequest.h / MovementPlanJson.h) with the DEFAULT
// keys, so (b) and (c) are byte-identical on both surfaces — and the success
// path is cheap here (a lane + a named preset), so (a) is pinned too.
// B2 twin (sections form): per-section cycles/midPoint must reach the plan on
// BOTH surfaces. The sections parse used to drop both keys silently (while
// startValue/endValue had the top-level fallback), so a sections-form sine
// {cycles:6} landed the len/4 default (24 cycles over the 96-beat window) and
// the vector-bloom breakdown collapsed to near-silence.
TEST_F(AddFxParityTest, SectionsFormCyclesReachThePlanOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    auto& cmds = engine->getProjectCommands();
    ASSERT_TRUE(cmds.addAutomationLane(t.targetIdx, "Breath", 2001));
    ASSERT_TRUE(cmds.addAutomationLane(t.decoyIdx, "Breath", 2002));
    engine->drainPendingRoutingRebuild();

    // The parse layer directly: per-section cycles AND midPoint parse, and the
    // top-level fallback applies when a section omits them.
    {
        HDAW::AutomationPresetRequest req;
        std::string parseError;
        const QJsonObject parseArgs{
            { "midPoint", 0.25 },
            { "sections", QJsonArray{ QJsonObject{
                { "start", 0.0 }, { "end", 16.0 },
                { "preset", "openClose" }, { "cycles", 6 }, { "midPoint", 0.75 } } } } };
        ASSERT_TRUE(HDAW::parseAutomationPresetRequest(parseArgs, req, parseError)) << parseError;
        ASSERT_EQ(req.windows.size(), 1);
        ASSERT_TRUE(req.windows[0].cycles.has_value());
        EXPECT_DOUBLE_EQ(*req.windows[0].cycles, 6.0);
        ASSERT_TRUE(req.windows[0].midPoint.has_value());
        EXPECT_DOUBLE_EQ(*req.windows[0].midPoint, 0.75);

        HDAW::AutomationPresetRequest fallbackReq;
        const QJsonObject fallbackArgs{
            { "cycles", 3.0 },
            { "sections", QJsonArray{ QJsonObject{
                { "start", 0.0 }, { "end", 16.0 }, { "preset", "sine" } } } } };
        ASSERT_TRUE(HDAW::parseAutomationPresetRequest(fallbackArgs, fallbackReq, parseError))
            << parseError;
        ASSERT_TRUE(fallbackReq.windows[0].cycles.has_value());
        EXPECT_DOUBLE_EQ(*fallbackReq.windows[0].cycles, 3.0)
            << "top-level cycles must fall back into sections windows";
    }

    const QJsonObject mcpArgs{
        { "trackID", t.targetID }, { "lane", "Breath" },
        { "sections", QJsonArray{ QJsonObject{
            { "start", 256.0 }, { "end", 352.0 },
            { "preset", "sine" }, { "cycles", 6 } } } } };
    const QJsonValue toolPayload = mcpValue("automation_preset", mcpArgs);
    ASSERT_TRUE(toolPayload.isObject())
        << mcpText("automation_preset", mcpArgs).toStdString();
    EXPECT_GT(toolPayload.toObject().value("pointsAdded").toInt(), 0);

    const QJsonObject rpcArgs{
        { "trackID", t.decoyID }, { "lane", "Breath" },
        { "sections", QJsonArray{ QJsonObject{
            { "start", 256.0 }, { "end", 352.0 },
            { "preset", "sine" }, { "cycles", 6 } } } } };
    EXPECT_EQ(rpcPayload("project.applyAutomationPreset", rpcArgs), toolPayload);

    // Same lane outcome on both surfaces: exactly 6 rising 0.9-crossings over
    // the full 0..1 span — the dropped-cycles default would show 24.
    const auto crossingsOf = [&](int track) {
        const auto pts = engine->getReadModel().getAutomationPoints(track, "Breath");
        double minValue = 1.0, maxValue = 0.0;
        int crossings = 0;
        bool above = false;
        for (const auto& p : pts) {
            if (static_cast<double>(p.value) < minValue) minValue = p.value;
            if (static_cast<double>(p.value) > maxValue) maxValue = p.value;
            if (! above && p.value >= 0.9f) { ++crossings; above = true; }
            else if (above && p.value < 0.9f) above = false;
        }
        return std::make_tuple(crossings, minValue, maxValue);
    };
    const auto toolSide = crossingsOf(t.targetIdx);
    const auto routeSide = crossingsOf(t.decoyIdx);
    EXPECT_EQ(std::get<0>(toolSide), 6);
    EXPECT_EQ(std::get<0>(routeSide), 6);
    EXPECT_NEAR(std::get<1>(toolSide), 0.0, 1e-3);
    EXPECT_NEAR(std::get<1>(routeSide), 0.0, 1e-3);
    EXPECT_NEAR(std::get<2>(toolSide), 1.0, 1e-3);
    EXPECT_NEAR(std::get<2>(routeSide), 1.0, 1e-3);
}

// B6 twin: the brief's targets gate rides the SHARED shapers (MixReportJson /
// MixVerdict), so the MCP and RPC payloads must match byte-for-byte - and the
// rows must actually land (targetChecks + targetsOk on both surfaces).
TEST_F(AddFxParityTest, MixTargetsGateMatchesOnBothSurfaces) {
    juce::File wavFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_b6_targets_twin.wav");
    wavFile.deleteFile();
    {
        std::unique_ptr<juce::FileOutputStream> out(wavFile.createOutputStream());
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(out.get(), 44100.0, 1, 16, {}, 0));
        ASSERT_NE(writer, nullptr);
        out.release();
        const int kSamples = 44100 * 3;
        juce::AudioBuffer<float> buffer(1, kSamples);
        for (int i = 0; i < kSamples; ++i)
            buffer.setSample(0, i, 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * i / 44100.0f));
        writer->writeFromAudioSampleBuffer(buffer, 0, kSamples);
    }

    // 0.3 sine -> rms ~ 0.212: |0.212 - 0.21| is inside the +/-5% masterRms band.
    const QJsonObject targets{ { "masterRms", 0.21 }, { "ceilingHitPctMax", 5.0 } };

    // mix_report twin: identical payloads, rows land on both surfaces.
    const QJsonObject reportArgs{ { "filePath", wavFile.getFullPathName().toStdString().c_str() },
                                  { "targets", targets } };
    const QJsonValue toolReport = mcpValue("mix_report", reportArgs);
    ASSERT_TRUE(toolReport.isObject()) << mcpText("mix_report", reportArgs).toStdString();
    EXPECT_EQ(rpcPayload("audio.mixReport", reportArgs), toolReport);
    EXPECT_EQ(toolReport.toObject().value("targetChecks").toArray().size(), 2);
    EXPECT_TRUE(toolReport.toObject().value("targetsOk").toBool());

    // mix_verdict twin: the targets gate rides the shared composer.
    const QJsonObject verdictArgs{ { "filePath", wavFile.getFullPathName().toStdString().c_str() },
                                   { "targets", targets } };
    const QJsonValue toolVerdict = mcpValue("mix_verdict", verdictArgs);
    ASSERT_TRUE(toolVerdict.isObject()) << mcpText("mix_verdict", verdictArgs).toStdString();
    EXPECT_EQ(rpcPayload("audio.mixVerdict", verdictArgs), toolVerdict);
    const auto gates = toolVerdict.toObject().value("gates").toObject();
    ASSERT_TRUE(gates.contains("targets"));
    EXPECT_TRUE(gates.value("targets").toObject().value("ok").toBool());

    wavFile.deleteFile();
}

TEST_F(AddFxParityTest, StableTrackIdDrivesAutomationPresetAndPlanOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    auto& cmds = engine->getProjectCommands();
    ASSERT_TRUE(cmds.addAutomationLane(t.decoyIdx, "Pre", 2000));
    ASSERT_TRUE(cmds.addAutomationLane(t.targetIdx, "Pre", 2000));
    engine->drainPendingRoutingRebuild();
    auto points = [&](int track) {
        return engine->getReadModel().getAutomationPoints(track, "Pre");
    };

    // (a) automation_preset by id: the window's points land on the id's lane,
    // and the two surfaces return the SAME payload (one shared entry point).
    const QJsonObject presetById{ { "trackID", t.targetID }, { "lane", "Pre" },
                                  { "preset", "pump" }, { "start", 0.0 }, { "end", 8.0 } };
    const QJsonValue toolPreset = mcpValue("automation_preset", presetById);
    ASSERT_TRUE(toolPreset.isObject())
        << mcpText("automation_preset", presetById).toStdString();
    EXPECT_GT(toolPreset.toObject().value("pointsAdded").toInt(), 0);
    EXPECT_EQ(rpcPayload("project.applyAutomationPreset", presetById), toolPreset);
    EXPECT_FALSE(points(t.targetIdx).empty());
    EXPECT_TRUE(points(t.decoyIdx).empty()) << "the decoy's lane must stay empty";

    // apply_movement_plan by id: the per-event ref resolves to the id's track.
    const QJsonObject planById{ { "events", QJsonArray{ QJsonObject{
        { "trackID", t.targetID }, { "preset", "macro" }, { "start", 8.0 }, { "end", 16.0 },
        { "laneName", "Pre" }, { "paramID", 2000 } } } } };
    const QJsonValue toolPlan = mcpValue("apply_movement_plan", planById);
    ASSERT_TRUE(toolPlan.isObject())
        << mcpText("apply_movement_plan", planById).toStdString();
    EXPECT_EQ(toolPlan.toObject().value("okCount").toInt(), 1)
        << mcpText("apply_movement_plan", planById).toStdString();
    EXPECT_EQ(toolPlan.toObject().value("failCount").toInt(), 0);
    EXPECT_EQ(rpcPayload("project.applyMovementPlan", planById), toolPlan);
    EXPECT_TRUE(points(t.decoyIdx).empty()) << "the decoy's lane must stay empty";

    // (b) unknown id — one text on both surfaces, the whole plan refused
    // before anything is applied.
    const QJsonObject presetUnknown{ { "trackID", 4242 }, { "lane", "Pre" },
                                     { "preset", "pump" }, { "start", 0.0 }, { "end", 8.0 } };
    expectSameFailure("automation_preset", "project.applyAutomationPreset", presetUnknown);
    EXPECT_EQ(mcpText("automation_preset", presetUnknown).toStdString(), "unknown trackID 4242");
    const QJsonObject planUnknown{ { "events", QJsonArray{ QJsonObject{
        { "trackID", 4242 }, { "preset", "pump" } } } } };
    expectSameFailure("apply_movement_plan", "project.applyMovementPlan", planUnknown);
    EXPECT_EQ(mcpText("apply_movement_plan", planUnknown).toStdString(), "unknown trackID 4242");

    // (c) disagreement — both surfaces word with the default `trackId`
    // spelling, so the text is byte-identical here.
    const QJsonObject presetClash{ { "trackId", 0 }, { "trackID", t.targetID }, { "lane", "Pre" },
                                   { "preset", "pump" }, { "start", 0.0 }, { "end", 8.0 } };
    expectSameFailure("automation_preset", "project.applyAutomationPreset", presetClash);
    EXPECT_EQ(mcpText("automation_preset", presetClash).toStdString(),
              "trackId 0 and trackID " + std::to_string(t.targetID) + " disagree");
    const QJsonObject planClash{ { "events", QJsonArray{ QJsonObject{
        { "trackId", 0 }, { "trackID", t.targetID }, { "preset", "pump" } } } } };
    expectSameFailure("apply_movement_plan", "project.applyMovementPlan", planClash);
    EXPECT_EQ(mcpText("apply_movement_plan", planClash).toStdString(),
              "trackId 0 and trackID " + std::to_string(t.targetID) + " disagree");

    // nothing mutated by the four refusals
    EXPECT_GT(points(t.targetIdx).size(), 0u);
    EXPECT_TRUE(points(t.decoyIdx).empty());
}

// ─── plugin params: set_fx_param / list_fx_params ↔ pluginParam.* ──────────
// (b)+(c) on pluginParam.setParam / getParams (Router_Plugin) plus the
// id-only read. The .clap extension fallback creates a pluginId-bearing slot
// whose processor degrades to 'none', so the READ side works without any
// plugin; the WRITE side (set_fx_param / pluginParam.setParam -> the
// offline-replay override ledger) needs a LIVE plugin instance — on a
// deviceless slot setPluginParam reports "slot is not a plugin slot" and
// writes nothing (AudioEngineCommands_Fx's !slot->isPlugin() gate), so with no
// CLAP children in this suite the write twins are pinned on (b)/(c) instead,
// which is exactly the B2b contract under test (the shared track ref).
TEST_F(AddFxParityTest, StableTrackIdDrivesPluginParamSurfacesOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    const QString pluginTarget = "C:/definitely/missing/hdaw_b2b_target.clap";
    const QString pluginDecoy = "C:/definitely/missing/hdaw_b2b_decoy.clap";
    // The decoy gets ONE placeholder plugin slot; the target gets an internal
    // eq slot FIRST plus its own placeholder plugin slot — the two chains
    // differ, so every read below can only come from the track the id names.
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.decoyIdx },
                                               { "pluginId", pluginDecoy } })
                    .startsWith("slot="));
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "eq" } })
                    .startsWith("slot="));
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.targetIdx },
                                               { "pluginId", pluginTarget } })
                    .startsWith("slot="));

    // (a) id-only read: the tool projects the id's OWN slot 0 — the target's
    // eq defs — while the decoy's slot 0 is the plugin placeholder (no live
    // params), so a read that landed on the wrong track cannot pass.
    const QJsonValue targetParams =
        mcpValue("list_fx_params", QJsonObject{ { "trackID", t.targetID }, { "slotIndex", 0 } });
    ASSERT_TRUE(targetParams.isObject())
        << mcpText("list_fx_params", QJsonObject{ { "trackID", t.targetID }, { "slotIndex", 0 } })
               .toStdString();
    EXPECT_FALSE(targetParams.toObject().value("params").toArray().isEmpty())
        << "the target's slot 0 is the eq — its params must be listed";
    const QJsonValue decoyParams =
        mcpValue("list_fx_params", QJsonObject{ { "trackID", t.decoyID }, { "slotIndex", 0 } });
    ASSERT_TRUE(decoyParams.isObject());
    EXPECT_TRUE(decoyParams.toObject().value("params").toArray().isEmpty())
        << "the decoy's slot 0 is a plugin placeholder with no live params";

    // id-only read on the route twin: accepted for both ids (a deviceless
    // plugin slot has no live params — the payload is the empty array).
    EXPECT_TRUE(rpcPayload("pluginParam.getParams",
                           QJsonObject{ { "trackID", t.targetID }, { "pluginID", pluginTarget } })
                    .isArray());
    EXPECT_TRUE(rpcPayload("pluginParam.getParams",
                           QJsonObject{ { "trackID", t.decoyID }, { "pluginID", pluginDecoy } })
                    .isArray());

    // The ledger is the write seam — and it stays EMPTY here by design (a
    // deviceless placeholder slot cannot carry an override), which is also the
    // "nothing mutated" baseline for (c) below.
    EXPECT_TRUE(engine->getProjectCommands().getPluginParamOverrides(t.targetIdx, 1).empty())
        << "nothing may land in the ledger without a live plugin";
    EXPECT_TRUE(engine->getProjectCommands().getPluginParamOverrides(t.decoyIdx, 0).empty())
        << "the decoy's slot must stay untouched";

    // (b) unknown id: identical text on both surfaces (each resolves BEFORE
    // its own pluginID / slotIndex argument).
    const QJsonObject unknownRead{ { "trackID", 4242 }, { "slotIndex", 0 } };
    expectSameFailure("list_fx_params", "pluginParam.getParams", unknownRead);
    EXPECT_EQ(mcpText("list_fx_params", unknownRead).toStdString(), "unknown trackID 4242");
    const QJsonObject unknownWrite{ { "trackID", 4242 }, { "slotIndex", 0 },
                                    { "paramIndex", 0 }, { "value", 0.5 } };
    expectSameFailure("set_fx_param", "pluginParam.setParam", unknownWrite);
    EXPECT_EQ(mcpText("set_fx_param", unknownWrite).toStdString(), "unknown trackID 4242");

    // (c) disagreement on each surface's own positional spelling; the ledgers
    // are untouched (a refusal never falls back to track 0).
    expectDisagreementSpelled("set_fx_param", "pluginParam.setParam",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 1 }, { "paramIndex", 0 }, { "value", 0.5 } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID },
                                           { "pluginID", pluginTarget }, { "paramIndex", 0 },
                                           { "normalizedValue", 0.5 } },
                              "trackId", "trackIndex", 0, t.targetID);
    expectDisagreementSpelled("list_fx_params", "pluginParam.getParams",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID },
                                           { "pluginID", pluginTarget } },
                              "trackId", "trackIndex", 0, t.targetID);
    EXPECT_TRUE(engine->getProjectCommands().getPluginParamOverrides(t.targetIdx, 1).empty())
        << "a refused write must not touch the ledger";
    EXPECT_TRUE(engine->getProjectCommands().getPluginParamOverrides(t.decoyIdx, 0).empty());
}

// ─── sampler: set_sampler_mode ↔ sampler.setMode ───────────────────────────
TEST_F(AddFxParityTest, StableTrackIdDrivesSamplerModeOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.decoyIdx }, { "fxType", "sampler" } })
                    .startsWith("slot="));
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "sampler" } })
                    .startsWith("slot="));
    auto mode = [&](int track) {
        return engine->getReadModel().getSamplerState(track, 0).mode;
    };
    ASSERT_EQ(mode(t.decoyIdx), "classic");
    ASSERT_EQ(mode(t.targetIdx), "classic");

    // (a) id-only tool: "slice" on the TARGET only…
    const QJsonObject toolById{ { "trackID", t.targetID }, { "slotIndex", 0 }, { "mode", "slice" } };
    EXPECT_FALSE(mcpIsError("set_sampler_mode", toolById))
        << mcpText("set_sampler_mode", toolById).toStdString();
    EXPECT_EQ(mode(t.targetIdx), "slice");
    EXPECT_EQ(mode(t.decoyIdx), "classic") << "the decoy must stay untouched";

    // …and id-only on the route: "one-shot" on the same id.
    EXPECT_FALSE(rpc("sampler.setMode",
                     QJsonObject{ { "trackID", t.targetID }, { "slotIndex", 0 },
                                  { "mode", "one-shot" } }).isError);
    EXPECT_EQ(mode(t.targetIdx), "one-shot");
    EXPECT_EQ(mode(t.decoyIdx), "classic");

    // (b) unknown id: identical text.
    const QJsonObject unknown{ { "trackID", 4242 }, { "slotIndex", 0 }, { "mode", "classic" } };
    expectSameFailure("set_sampler_mode", "sampler.setMode", unknown);
    EXPECT_EQ(mcpText("set_sampler_mode", unknown).toStdString(), "unknown trackID 4242");

    // (c) disagreement on each surface's own positional spelling; no mutation.
    expectDisagreementSpelled("set_sampler_mode", "sampler.setMode",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 }, { "mode", "classic" } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 }, { "mode", "classic" } },
                              "trackId", "trackIndex", 0, t.targetID);
    EXPECT_EQ(mode(t.targetIdx), "one-shot");
    EXPECT_EQ(mode(t.decoyIdx), "classic");
}

// ─── psy_fm: psy_fm_clear_mod_matrix ↔ psy_fm.clearModMatrix (+ the read) ──
// The clear is a TREE write (slot `psyFmMatrix`), so the persisted route count
// is the deterministic assertion channel; the mod-matrix debug read rides the
// same spellings and pins the same failure texts.
TEST_F(AddFxParityTest, StableTrackIdDrivesPsyFmModMatrixOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.decoyIdx }, { "fxType", "psy_fm" } })
                    .startsWith("slot="));
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "psy_fm" } })
                    .startsWith("slot="));
    // Seed ONE route on each track (positional setup).
    for (const int idx : { t.decoyIdx, t.targetIdx })
        ASSERT_FALSE(mcpIsError("psy_fm_set_mod_route",
                                QJsonObject{ { "trackId", idx }, { "slotIndex", 0 },
                                             { "source", "ratioSweepLFO" }, { "dest", "op1Ratio" },
                                             { "depth", 0.5 } }));
    auto routeCount = [&](int track) {
        return HDAW::PsyFmState::decodeRoutes(
            fxSlotNode(track, 0).getProperty("psyFmMatrix", "").toString().toStdString()).size();
    };
    ASSERT_EQ(routeCount(t.decoyIdx), 1u);
    ASSERT_EQ(routeCount(t.targetIdx), 1u);

    // (a) id-only tool clear: the TARGET's routes go, the decoy's stay.
    EXPECT_FALSE(mcpIsError("psy_fm_clear_mod_matrix",
                            QJsonObject{ { "trackID", t.targetID }, { "slotIndex", 0 } }))
        << mcpText("psy_fm_clear_mod_matrix",
                   QJsonObject{ { "trackID", t.targetID }, { "slotIndex", 0 } }).toStdString();
    EXPECT_EQ(routeCount(t.targetIdx), 0u);
    EXPECT_EQ(routeCount(t.decoyIdx), 1u) << "the decoy must keep its route";

    // Re-seed the target and clear it again through the ROUTE by id.
    ASSERT_FALSE(mcpIsError("psy_fm_set_mod_route",
                            QJsonObject{ { "trackId", t.targetIdx }, { "slotIndex", 0 },
                                         { "source", "ratioSweepLFO" }, { "dest", "op1Ratio" },
                                         { "depth", 0.5 } }));
    EXPECT_FALSE(rpc("psy_fm.clearModMatrix",
                     QJsonObject{ { "trackID", t.targetID }, { "slotIndex", 0 } }).isError);
    EXPECT_EQ(routeCount(t.targetIdx), 0u);
    EXPECT_EQ(routeCount(t.decoyIdx), 1u);

    // The read twin by id: one payload, both surfaces (the shared
    // src/common/PsyFmModMatrixView.cpp shaping).
    const QJsonObject debugById{ { "trackID", t.decoyID }, { "slotIndex", 0 } };
    const QJsonValue toolDebug = mcpValue("psy_fm_mod_matrix_debug", debugById);
    ASSERT_TRUE(toolDebug.isObject())
        << mcpText("psy_fm_mod_matrix_debug", debugById).toStdString();
    EXPECT_EQ(rpcPayload("psy_fm.modMatrixDebug", debugById), toolDebug);

    // (b) unknown id: identical text on the write AND the read twin.
    const QJsonObject unknown{ { "trackID", 4242 }, { "slotIndex", 0 } };
    expectSameFailure("psy_fm_clear_mod_matrix", "psy_fm.clearModMatrix", unknown);
    EXPECT_EQ(mcpText("psy_fm_clear_mod_matrix", unknown).toStdString(), "unknown trackID 4242");
    expectSameFailure("psy_fm_mod_matrix_debug", "psy_fm.modMatrixDebug", unknown);

    // (c) disagreement on each surface's own positional spelling; no mutation.
    expectDisagreementSpelled("psy_fm_clear_mod_matrix", "psy_fm.clearModMatrix",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 } },
                              "trackId", "trackIndex", 0, t.targetID);
    expectDisagreementSpelled("psy_fm_mod_matrix_debug", "psy_fm.modMatrixDebug",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 } },
                              "trackId", "trackIndex", 0, t.targetID);
    EXPECT_EQ(routeCount(t.targetIdx), 0u);
    EXPECT_EQ(routeCount(t.decoyIdx), 1u);
}

// ─── fm_synth: fm_synth_get_state ↔ read.getFmSynthState ───────────────────
// Both surfaces resolve with the DEFAULT keys, so (b)/(c) keep the
// byte-identical shared-object twin, and the state read reports the id's OWN
// slot (the tree's param_0 = the algorithm the rebuild restores).
TEST_F(AddFxParityTest, StableTrackIdDrivesFmSynthStateOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.decoyIdx }, { "fxType", "fm_synth" } })
                    .startsWith("slot="));
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "fm_synth" } })
                    .startsWith("slot="));
    auto& cmds = engine->getProjectCommands();
    cmds.setFxSlotParam(t.decoyIdx, 0, 0, 3.0f);
    cmds.setFxSlotParam(t.targetIdx, 0, 0, 5.0f);
    const int algoDecoy = static_cast<int>(fxSlotNode(t.decoyIdx, 0).getProperty("param_0", 0));
    const int algoTarget = static_cast<int>(fxSlotNode(t.targetIdx, 0).getProperty("param_0", 0));
    ASSERT_NE(algoDecoy, algoTarget) << "premise: the two slots must differ";

    // (a) id-only read on the tool and the route: each reports its OWN slot's
    // algorithm, and the two payloads agree value for value.
    const QJsonObject targetById{ { "trackID", t.targetID }, { "slotIndex", 0 } };
    const QJsonValue toolState = mcpValue("fm_synth_get_state", targetById);
    ASSERT_TRUE(toolState.isObject())
        << mcpText("fm_synth_get_state", targetById).toStdString();
    EXPECT_EQ(toolState.toObject().value("algorithm").toInt(), algoTarget);
    EXPECT_EQ(rpcPayload("read.getFmSynthState", targetById), toolState);
    EXPECT_EQ(mcpValue("fm_synth_get_state",
                       QJsonObject{ { "trackID", t.decoyID }, { "slotIndex", 0 } })
                  .toObject().value("algorithm").toInt(),
              algoDecoy)
        << "the decoy's id must report the decoy's own algorithm";

    // (b) unknown id: identical text on both surfaces.
    const QJsonObject unknown{ { "trackID", 4242 }, { "slotIndex", 0 } };
    expectSameFailure("fm_synth_get_state", "read.getFmSynthState", unknown);
    EXPECT_EQ(mcpText("fm_synth_get_state", unknown).toStdString(), "unknown trackID 4242");

    // (c) disagreement: byte-identical `trackId …` text (default keys both).
    const QJsonObject clash{ { "trackId", 0 }, { "trackID", t.targetID }, { "slotIndex", 0 } };
    expectSameFailure("fm_synth_get_state", "read.getFmSynthState", clash);
    EXPECT_EQ(mcpText("fm_synth_get_state", clash).toStdString(),
              "trackId 0 and trackID " + std::to_string(t.targetID) + " disagree");
    EXPECT_EQ(static_cast<int>(fxSlotNode(t.targetIdx, 0).getProperty("param_0", 0)), algoTarget)
        << "a refusal must not mutate the slot";
    EXPECT_EQ(static_cast<int>(fxSlotNode(t.decoyIdx, 0).getProperty("param_0", 0)), algoDecoy);
}

// ─── matrix: apply_matrix_preset ↔ matrix.applyPreset — failure paths ──────
// The success path needs a live plugin engine plus harvested preset ids
// (machine-dependent), so this family pins the two resolver failure texts
// only — which both surfaces reach BEFORE any preset lookup. Both surfaces
// resolve with the default keys, so the texts are byte-identical.
TEST_F(AddFxParityTest, MatrixPresetTrackRefFailuresMatchOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();

    const QJsonObject unknown{ { "trackID", 4242 }, { "engine", "je8086" },
                               { "id", "nope" }, { "slotIndex", 0 } };
    expectSameFailure("apply_matrix_preset", "matrix.applyPreset", unknown);
    EXPECT_EQ(mcpText("apply_matrix_preset", unknown).toStdString(), "unknown trackID 4242");

    const QJsonObject clash{ { "trackId", 0 }, { "trackID", t.targetID }, { "engine", "je8086" },
                             { "id", "nope" }, { "slotIndex", 0 } };
    expectSameFailure("apply_matrix_preset", "matrix.applyPreset", clash);
    EXPECT_EQ(mcpText("apply_matrix_preset", clash).toStdString(),
              "trackId 0 and trackID " + std::to_string(t.targetID) + " disagree");
}

// ─── midi-fx: list_midi_fx_params ↔ read.getMidiFxSlots ────────────────────
// Read-only family: (a) is "the id-only read returns the RIGHT track's slot
// set" — the decoy carries ONE slot and the target TWO of different types, so
// a read that ignored the id cannot describe both.
TEST_F(AddFxParityTest, StableTrackIdDrivesMidiFxSlotsOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    ASSERT_FALSE(mcpIsError("add_midi_fx",
                            QJsonObject{ { "trackId", t.decoyIdx }, { "fxType", "arpeggiator" } }));
    ASSERT_FALSE(mcpIsError("add_midi_fx",
                            QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "transpose" } }));
    ASSERT_FALSE(mcpIsError("add_midi_fx",
                            QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "humanize" } }));

    // (a) id-only reads: the tool projects the id's own slot, the route lists
    // the id's own slot set — and the id equals the position it resolves to.
    const QJsonObject targetSlot1{ { "trackID", t.targetID }, { "slotIndex", 1 } };
    const QJsonValue slot1 = mcpValue("list_midi_fx_params", targetSlot1);
    ASSERT_TRUE(slot1.isObject())
        << mcpText("list_midi_fx_params", targetSlot1).toStdString();
    EXPECT_EQ(slot1.toObject().value("fxType").toString().toStdString(), "humanize");
    EXPECT_TRUE(mcpIsError("list_midi_fx_params",
                           QJsonObject{ { "trackID", t.decoyID }, { "slotIndex", 1 } }))
        << "the decoy has one slot; slot 1 exists only on the track the target id names";
    const QJsonValue targetList =
        rpcPayload("read.getMidiFxSlots", QJsonObject{ { "trackID", t.targetID } });
    ASSERT_TRUE(targetList.isArray());
    EXPECT_EQ(targetList.toArray().size(), 2);
    EXPECT_EQ(rpcPayload("read.getMidiFxSlots", QJsonObject{ { "trackID", t.decoyID } })
                  .toArray().size(),
              1);
    EXPECT_EQ(rpcPayload("read.getMidiFxSlots", QJsonObject{ { "trackID", t.decoyID } }),
              rpcPayload("read.getMidiFxSlots", QJsonObject{ { "trackIndex", t.decoyIdx } }))
        << "the id names the same slots as the position it resolves to";

    // (b) unknown id: identical text.
    const QJsonObject unknown{ { "trackID", 4242 }, { "slotIndex", 0 } };
    expectSameFailure("list_midi_fx_params", "read.getMidiFxSlots", unknown);
    EXPECT_EQ(mcpText("list_midi_fx_params", unknown).toStdString(), "unknown trackID 4242");

    // (c) disagreement on each surface's own positional spelling; nothing
    // mutated (still 2 + 1 slots).
    expectDisagreementSpelled("list_midi_fx_params", "read.getMidiFxSlots",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "slotIndex", 0 } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID } },
                              "trackId", "trackIndex", 0, t.targetID);
    EXPECT_EQ(engine->getReadModel().getMidiFxSlots(t.targetIdx).size(), 2u);
    EXPECT_EQ(engine->getReadModel().getMidiFxSlots(t.decoyIdx).size(), 1u);
}

// ─── fx chain presets: save_fx_chain / load_fx_chain ↔ project.*FxChainPreset
// The two chains differ (decoy ONE reverb slot, target eq+delay), so the saved
// preset's slotCount can only describe the track the id named, and loading it
// back onto the same id restores THAT chain while the decoy is untouched.
TEST_F(AddFxParityTest, StableTrackIdDrivesFxChainPresetsOnBothSurfaces) {
    const TwoTracks t = seedTargetAndDecoy();
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.decoyIdx }, { "fxType", "reverb" } })
                    .startsWith("slot="));
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "eq" } })
                    .startsWith("slot="));
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "delay" } })
                    .startsWith("slot="));
    ASSERT_EQ(engine->getReadModel().getFxSlots(t.decoyIdx).size(), 1u);
    ASSERT_EQ(engine->getReadModel().getFxSlots(t.targetIdx).size(), 2u);
    auto fxTypes = [&](int track) {
        std::vector<std::string> types;
        for (const auto& s : engine->getReadModel().getFxSlots(track))
            types.push_back(s.fxType);
        return types;
    };
    auto listedRow = [&](const QString& id) {
        for (const auto& v : mcpValue("list_fx_chains", QJsonObject{}).toArray()) {
            const auto row = v.toObject();
            if (row.value("id").toString() == id) return row;
        }
        return QJsonObject{};
    };

    // (a) id-only save on the tool: the preset captures the id's OWN chain.
    const QJsonObject saveTool{ { "trackID", t.targetID }, { "name", "B2b Twin Chain" } };
    const QJsonValue savedTool = mcpValue("save_fx_chain", saveTool);
    ASSERT_TRUE(savedTool.isObject())
        << mcpText("save_fx_chain", saveTool).toStdString();
    const QString idTool = savedTool.toObject().value("id").toString();
    EXPECT_FALSE(idTool.isEmpty());
    EXPECT_EQ(listedRow(idTool).value("slotCount").toInt(), 2)
        << "the preset must describe the TARGET's two slots, not the decoy's one";
    EXPECT_EQ(engine->getReadModel().getFxSlots(t.decoyIdx).size(), 1u)
        << "the decoy must stay untouched";

    // …and id-only save on the route, same id.
    const QJsonObject saveRoute{ { "trackID", t.targetID }, { "name", "B2b Twin Chain R" } };
    const QJsonValue savedRoute = rpcPayload("project.saveFxChainPreset", saveRoute);
    ASSERT_TRUE(savedRoute.isObject());
    const QString idRoute = savedRoute.toObject().value("id").toString();
    EXPECT_FALSE(idRoute.isEmpty());
    EXPECT_EQ(listedRow(idRoute).value("slotCount").toInt(), 2);

    // Grow the target's chain, then load each preset back onto the SAME id —
    // tool once, route once — and watch the id's chain return to [eq, delay].
    ASSERT_TRUE(mcpText("add_fx", QJsonObject{ { "trackId", t.targetIdx }, { "fxType", "chorus" } })
                    .startsWith("slot="));
    ASSERT_EQ(engine->getReadModel().getFxSlots(t.targetIdx).size(), 3u);
    EXPECT_FALSE(mcpIsError("load_fx_chain",
                            QJsonObject{ { "trackID", t.targetID }, { "id", idRoute } }))
        << mcpText("load_fx_chain", QJsonObject{ { "trackID", t.targetID }, { "id", idRoute } })
               .toStdString();
    EXPECT_EQ(fxTypes(t.targetIdx), (std::vector<std::string>{ "eq", "delay" }));
    EXPECT_EQ(fxTypes(t.decoyIdx), (std::vector<std::string>{ "reverb" }))
        << "the decoy must stay untouched";
    EXPECT_FALSE(rpc("project.loadFxChainPreset",
                     QJsonObject{ { "trackID", t.targetID }, { "id", idTool } }).isError);
    EXPECT_EQ(fxTypes(t.targetIdx), (std::vector<std::string>{ "eq", "delay" }));
    EXPECT_EQ(fxTypes(t.decoyIdx), (std::vector<std::string>{ "reverb" }));

    // (b) unknown id: identical text on both surfaces, both operations.
    const QJsonObject unknownSave{ { "trackID", 4242 }, { "name", "Nope" } };
    expectSameFailure("save_fx_chain", "project.saveFxChainPreset", unknownSave);
    EXPECT_EQ(mcpText("save_fx_chain", unknownSave).toStdString(), "unknown trackID 4242");
    const QJsonObject unknownLoad{ { "trackID", 4242 }, { "id", idTool } };
    expectSameFailure("load_fx_chain", "project.loadFxChainPreset", unknownLoad);
    EXPECT_EQ(mcpText("load_fx_chain", unknownLoad).toStdString(), "unknown trackID 4242");

    // (c) disagreement on each surface's own positional spelling; the chains
    // and the library are untouched.
    expectDisagreementSpelled("save_fx_chain", "project.saveFxChainPreset",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "name", "Nope" } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID },
                                           { "name", "Nope" } },
                              "trackId", "trackIndex", 0, t.targetID);
    expectDisagreementSpelled("load_fx_chain", "project.loadFxChainPreset",
                              QJsonObject{ { "trackId", 0 }, { "trackID", t.targetID },
                                           { "id", idTool } },
                              QJsonObject{ { "trackIndex", 0 }, { "trackID", t.targetID },
                                           { "id", idTool } },
                              "trackId", "trackIndex", 0, t.targetID);
    EXPECT_EQ(fxTypes(t.targetIdx), (std::vector<std::string>{ "eq", "delay" }));
    EXPECT_EQ(fxTypes(t.decoyIdx), (std::vector<std::string>{ "reverb" }));
    EXPECT_EQ(listedRow(idTool).value("slotCount").toInt(), 2)
        << "a refused save must not touch the library";

    // Leave the user chain library as found.
    EXPECT_EQ(mcpText("delete_fx_chain", QJsonObject{ { "id", idTool } }).toStdString(), "ok");
    EXPECT_EQ(mcpText("delete_fx_chain", QJsonObject{ { "id", idRoute } }).toStdString(), "ok");
}

// ─── project lifecycle: save_project / load_project absolute-path gate ─────
// Lesson-34/38 class: `save_project {"filePath":"compositions/x.hdaw"}` used
// to return ok and write NOTHING — juce::File anchors a relative path to the
// ENGINE process CWD (a Temp dir on the MCP surface). Both surfaces now refuse
// through the shared gate (HDAW::projectPathError,
// src/common/ProjectPathCheck.h), byte-identically.
TEST_F(AddFxParityTest, SaveLoadProjectRefuseRelativeFilePathOnBothSurfaces) {
    const QString relative = "compositions/x.hdaw";
    // Operation-neutral: the gate serves BOTH save and load.
    const std::string refusal =
        "filePath must be absolute (got \"compositions/x.hdaw\") — relative "
        "paths are not resolved against the engine process CWD";

    // (a) relative save: identical refusal bytes, naming the offending value.
    expectSameFailure("save_project", "project.saveProject",
                      QJsonObject{ { "filePath", relative } });
    EXPECT_EQ(mcpText("save_project", QJsonObject{ { "filePath", relative } }).toStdString(),
              refusal);

    // (b) relative load: the SAME rule, the SAME bytes.
    expectSameFailure("load_project", "project.loadProject",
                      QJsonObject{ { "filePath", relative } });
    EXPECT_EQ(mcpText("load_project", QJsonObject{ { "filePath", relative } }).toStdString(),
              refusal);

    // Neither refusal touched the session project path (whoami vocabulary).
    EXPECT_TRUE(engine->getProjectCommands().getProjectFilePath().empty());

    // (c) absolute + missing parent dir: past the gate, the underlying save
    // failure must still surface — never a silent ok. The MCP tool reports it
    // as an error; the RPC route's contract for THIS method is the command's
    // bare bool (false), so the false is asserted there.
    const QString missingParent =
        QDir::tempPath() + "/hdaw_no_such_parent_dir_2026/a.hdaw";
    EXPECT_TRUE(mcpIsError("save_project", QJsonObject{ { "filePath", missingParent } }));
    EXPECT_EQ(mcpText("save_project", QJsonObject{ { "filePath", missingParent } }).toStdString(),
              "save failed");
    EXPECT_FALSE(rpc("project.saveProject",
                     QJsonObject{ { "filePath", missingParent } }).payload.toBool());
    EXPECT_FALSE(QFile::exists(missingParent));

    // (d) absolute + valid: the file EXISTS on disk after the call (Gate 2:
    // observable behaviour, not the ok payload), on both surfaces.
    const QString valid = QDir::tempPath() + "/hdaw_parity_save_valid.hdaw";
    QFile::remove(valid);
    EXPECT_EQ(mcpText("save_project", QJsonObject{ { "filePath", valid } })
                  .trimmed().toStdString(), "saved");
    EXPECT_TRUE(QFile::exists(valid));
    EXPECT_FALSE(rpc("project.saveProject", QJsonObject{ { "filePath", valid } }).isError);
    EXPECT_TRUE(QFile::exists(valid));
    // The load twin accepts the same absolute path.
    EXPECT_FALSE(rpc("project.loadProject", QJsonObject{ { "filePath", valid } }).isError);
    QFile::remove(valid);
}

} // namespace
