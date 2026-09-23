// MCP <-> RPC parity for the bus / send creators — slice B of
// docs/plans/2026-09-22-bus-send-surface.md.
//
// Before this slice the send tools (get_track_sends, set_track_send_level,
// set_track_send_mode, set_track_send_bypassed) could only SHAPE sends that
// already existed: nothing appended a BUS to busList and nothing appended a SEND
// to a track's SEND_LIST, so the dub/psybient "shared delay + reverb return fed
// by per-phrase sends" idiom was unreachable from either agent surface.
//
// Four creators now exist on both: MCP add_bus / remove_bus / add_send /
// remove_send and RPC project.addBus / project.removeBus / project.addSend /
// project.removeSend. Each pair calls the SAME ProjectCommands entry point
// (createBus / removeBus / createSend / removeSend) and shapes the SAME payload,
// so the strongest assertion available is the direct one: drive both surfaces and
// require identical results — success payload AND failure text.
//
// Two properties are asserted that a per-surface test cannot see:
//   1. the argument OBJECT IS SHARED. The same QJsonObject is handed to both
//      surfaces, so the route's key names must BE the MCP property names
//      (AGENTS.md: "Argument names are part of the contract"). Renaming a key on
//      either side fails here instead of silently sending -32602 in the field.
//   2. a creation payload carries the freshly allocated id, which MUST differ
//      between the two calls (each call creates a new object) — so busID /
//      sendIndex are compared structurally and every other field verbatim.

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "common/ReadModel.h"
#include "engine/AudioEngine.h"
#include "engine/ProjectSerializer.h"   // the save a listing must agree with (G4)
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"   // ProjectModel + the IDs property namespace

#include <memory>
#include <string>

namespace {

class BusSendRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        auto& cmds = engine->getProjectCommands();
        ASSERT_GE(cmds.addTrack("Kick"), 0);
        ASSERT_GE(cmds.addTrack("Bass"), 0);
        engine->drainPendingRoutingRebuild();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    // --- MCP surface -------------------------------------------------------
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
    // Tool payloads are compact JSON text on success and a bare message on
    // failure, so dispatch on the parsed root exactly like the MCP client does.
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
    // A failing pair: the shared args must reach the same command and come back as
    // the same text on both surfaces.
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

    // A creation payload carries the new object's fresh id, so the two surfaces
    // cannot return the same number. Compare the whole payload with that one
    // field blanked, and require the two ids themselves to be valid and distinct.
    static QJsonObject withoutId(const QJsonObject& o, const char* key) {
        QJsonObject copy = o;
        copy[key] = 0;
        return copy;
    }
    void expectSameCreationPayload(const QJsonValue& viaMcp, const QJsonValue& viaRpc,
                                   const char* idKey) {
        ASSERT_TRUE(viaMcp.isObject());
        EXPECT_EQ(withoutId(viaRpc.toObject(), idKey), withoutId(viaMcp.toObject(), idKey));
        EXPECT_TRUE(viaRpc.toObject().value("ok").toBool());
        const int mcpId = viaMcp.toObject().value(idKey).toInt(-1);
        const int rpcId = viaRpc.toObject().value(idKey).toInt(-1);
        EXPECT_GE(mcpId, 0);
        EXPECT_GE(rpcId, 0);
        EXPECT_NE(mcpId, rpcId) << "each call must allocate its own " << idKey;
    }

    // The bus node named by `busID`, straight off the project model — the ReadModel
    // exposes no bus surface, so the tree is the read path (same one
    // RoutingManager::rebuildFromValueTree reads).
    juce::ValueTree busNode(int busID) {
        return busNodeIn(engine->getProjectModel().getBusListTree(), busID);
    }
    static juce::ValueTree busNodeIn(const juce::ValueTree& busList, int busID) {
        for (int i = 0; i < busList.getNumChildren(); ++i) {
            auto child = busList.getChild(i);
            if (static_cast<int>(child.getProperty(IDs::busID, -1)) == busID) return child;
        }
        return {};
    }
    int busCount() { return engine->getProjectModel().getBusListTree().getNumChildren(); }

    // The row `list_buses` returned for one busID ({} when it listed no such bus).
    static QJsonObject listedBus(const QJsonArray& rows, int busID) {
        for (const auto& v : rows)
            if (v.toObject().value("busID").toInt(-1) == busID) return v.toObject();
        return {};
    }
    // One listing row against the BUS node it describes: the listing is only honest
    // if its fields are the node's (the same node a save serializes). Only an fx bus
    // has an FX chain, so only its fxType is a contract — for every other bus type the
    // row just has to carry the key (the shaper owns what an absent chain reads as).
    static void expectBusMatchesNode(const QJsonObject& listed, const juce::ValueTree& node) {
        ASSERT_TRUE(node.isValid());
        EXPECT_EQ(listed.value("name").toString().toStdString(),
                  node.getProperty(IDs::name).toString().toStdString());
        EXPECT_EQ(listed.value("busType").toString().toStdString(),
                  node.getProperty(IDs::busType).toString().toStdString());
        EXPECT_EQ(listed.value("busTarget").toInt(),
                  static_cast<int>(node.getProperty(IDs::busTarget)));
        EXPECT_TRUE(listed.contains("fxType")) << "every bus row carries fxType";
        if (node.getProperty(IDs::busType).toString() == "fx")
            EXPECT_EQ(listed.value("fxType").toString().toStdString(),
                      node.getProperty(IDs::fxType).toString().toStdString());
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

// --- Buses ------------------------------------------------------------------

// G6 (success half): add_bus and project.addBus return the same payload and both
// land a bus with the requested type/name/fxType on the same parent.
TEST_F(BusSendRpcTest, AddBusMatchesMcp) {
    const QJsonObject args{ { "busType", "fx" }, { "name", "Dub Delay" },
                            { "fxType", "delay" }, { "busTarget", 0 } };

    const QJsonValue viaMcp = mcpValue("add_bus", args);
    const QJsonValue viaRpc = rpcPayload("project.addBus", args);
    expectSameCreationPayload(viaMcp, viaRpc, "busID");

    // Both ids exist and describe the SAME request — one code path, no drift.
    for (const int id : { viaMcp.toObject().value("busID").toInt(-1),
                          viaRpc.toObject().value("busID").toInt(-1) }) {
        const auto node = busNode(id);
        ASSERT_TRUE(node.isValid()) << "busID " << id << " is not in BUS_LIST";
        EXPECT_EQ(node.getProperty(IDs::name).toString().toStdString(), "Dub Delay");
        EXPECT_EQ(node.getProperty(IDs::busType).toString().toStdString(), "fx");
        EXPECT_EQ(node.getProperty(IDs::fxType).toString().toStdString(), "delay");
        EXPECT_EQ(static_cast<int>(node.getProperty(IDs::busTarget)), 0);
    }
}

// G6 (failure half): a busTarget naming no bus is rejected by the command layer,
// and both surfaces report the identical reason with NO mutation.
TEST_F(BusSendRpcTest, AddBusRejectsUnknownBusTargetOnBothSurfaces) {
    const QJsonObject args{ { "busType", "fx" }, { "name", "Orphan" },
                            { "fxType", "delay" }, { "busTarget", 999 } };
    const int before = busCount();
    expectSameFailure("add_bus", "project.addBus", args);
    EXPECT_EQ(busCount(), before) << "a rejected createBus must not touch BUS_LIST";
    EXPECT_FALSE(busNode(999).isValid());
}

// The other rejection the command layer owns: an fxType FxBusProcessor cannot
// build. Reported identically by both surfaces.
TEST_F(BusSendRpcTest, AddBusRejectsUnsupportedFxTypeOnBothSurfaces) {
    const QJsonObject args{ { "busType", "fx" }, { "name", "Bogus Bus" },
                            { "fxType", "notafxtype" }, { "busTarget", 0 } };
    const int before = busCount();
    expectSameFailure("add_bus", "project.addBus", args);
    EXPECT_EQ(busCount(), before) << "a rejected createBus must not touch BUS_LIST";
    EXPECT_TRUE(rpc("project.addBus", args).payload.toObject().value("message").toString()
                    .contains("filter"))
        << "the rejection must name the accepted fx types, filter included";
}

// The `filter` return (slice E of docs/plans/2026-09-23-filter-bus.md) is a bus
// the command layer accepts, so both surfaces create it and report the same
// payload — the type list is one shared list, not a per-surface one.
TEST_F(BusSendRpcTest, AddFilterBusMatchesMcp) {
    const QJsonObject args{ { "busType", "fx" }, { "name", "Dub HPF" },
                            { "fxType", "filter" }, { "busTarget", 0 } };
    const QJsonValue viaMcp = mcpValue("add_bus", args);
    const QJsonValue viaRpc = rpcPayload("project.addBus", args);
    expectSameCreationPayload(viaMcp, viaRpc, "busID");
    for (const int id : { viaMcp.toObject().value("busID").toInt(-1),
                          viaRpc.toObject().value("busID").toInt(-1) }) {
        ASSERT_GE(id, 0);
        ASSERT_TRUE(busNode(id).isValid());
        EXPECT_EQ(busNode(id).getProperty(IDs::fxType).toString().toStdString(), "filter");
    }

    // Both surfaces read the same three params back through list_bus_fx_params /
    // read.listBusFxParams.
    const QJsonObject readArgs{ { "busID", viaMcp.toObject().value("busID").toInt(-1) } };
    const QJsonValue mcpRead = mcpValue("list_bus_fx_params", readArgs);
    const QJsonValue rpcRead = rpcPayload("read.listBusFxParams", readArgs);
    EXPECT_EQ(rpcRead, mcpRead);
    const QJsonArray params = mcpRead.toObject().value("params").toArray();
    ASSERT_EQ(params.size(), 3);
    EXPECT_EQ(params[0].toObject().value("name").toString(), QString("Cutoff"));
    EXPECT_EQ(params[1].toObject().value("name").toString(), QString("Mode"));
    EXPECT_EQ(params[2].toObject().value("name").toString(), QString("Resonance"));
    EXPECT_DOUBLE_EQ(params[0].toObject().value("minValue").toDouble(), 20.0);
    EXPECT_DOUBLE_EQ(params[0].toObject().value("maxValue").toDouble(), 20000.0);
    EXPECT_DOUBLE_EQ(params[1].toObject().value("maxValue").toDouble(), 2.0);
    // Resonance's min is a float def (0.1f) surfaced through JSON, so compare
    // with a tolerance rather than bit-exactness (0.1f != 0.1 as a double).
    EXPECT_NEAR(params[2].toObject().value("minValue").toDouble(), 0.1, 1e-6);
}

// remove_bus / project.removeBus: the SUCCESS payload is identical string-for-string
// ("ok"), each surface dropping an equivalent bus.
TEST_F(BusSendRpcTest, RemoveBusMatchesMcp) {
    auto& cmds = engine->getProjectCommands();
    const int a = cmds.createBus("fx", "Dub Delay", "delay", 0).busID;
    const int b = cmds.createBus("fx", "Dub Delay", "delay", 0).busID;
    ASSERT_GE(a, 0);
    ASSERT_GE(b, 0);
    ASSERT_TRUE(busNode(a).isValid());
    ASSERT_TRUE(busNode(b).isValid());

    const QJsonValue viaMcp = mcpValue("remove_bus", QJsonObject{ { "busID", a } });
    const QJsonValue viaRpc = rpcPayload("project.removeBus", QJsonObject{ { "busID", b } });
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaRpc, QJsonValue(QString("ok")));
    EXPECT_FALSE(busNode(a).isValid());
    EXPECT_FALSE(busNode(b).isValid());
}

// The master bus (busID 0) is not removable — the failure the plan calls out.
TEST_F(BusSendRpcTest, RemoveBusRefusesMasterOnBothSurfaces) {
    expectSameFailure("remove_bus", "project.removeBus", QJsonObject{ { "busID", 0 } });
    EXPECT_TRUE(busNode(0).isValid()) << "the master bus must survive the refusal";
}

// A bus that is not there is refused identically too.
TEST_F(BusSendRpcTest, RemoveBusUnknownIdFailsOnBothSurfaces) {
    const int before = busCount();
    expectSameFailure("remove_bus", "project.removeBus", QJsonObject{ { "busID", 999 } });
    EXPECT_EQ(busCount(), before);
}

// --- Sends ------------------------------------------------------------------

// G6 (success half): add_send and project.addSend agree on the payload and both
// land a send with the requested level and pre/post mode.
TEST_F(BusSendRpcTest, AddSendMatchesMcp) {
    // busTarget 1 is the default project's "Reverb" fx bus.
    const QJsonObject args{ { "trackId", 0 }, { "busTarget", 1 },
                            { "level", 0.5 }, { "isPreFader", true } };

    const QJsonValue viaMcp = mcpValue("add_send", args);
    const QJsonValue viaRpc = rpcPayload("project.addSend", args);
    expectSameCreationPayload(viaMcp, viaRpc, "sendIndex");

    // Both sends exist on track 0 with the requested shape (the ReadModel the
    // existing get_track_sends tool reads).
    const auto sends = engine->getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 2u);
    for (const auto& s : sends) {
        EXPECT_NEAR(s.level, 0.5f, 1e-5);
        EXPECT_TRUE(s.isPreFader);
    }
    EXPECT_EQ(sends[0].sendIndex, viaMcp.toObject().value("sendIndex").toInt(-1));
    EXPECT_EQ(sends[1].sendIndex, viaRpc.toObject().value("sendIndex").toInt(-1));
}

// The defaults are part of the contract: level 1.0, post-fader, both surfaces.
TEST_F(BusSendRpcTest, AddSendDefaultsMatchMcp) {
    const QJsonObject args{ { "trackId", 0 }, { "busTarget", 1 } };
    const QJsonValue viaMcp = mcpValue("add_send", args);
    const QJsonValue viaRpc = rpcPayload("project.addSend", args);
    expectSameCreationPayload(viaMcp, viaRpc, "sendIndex");

    const auto sends = engine->getReadModel().getTrackSends(0);
    ASSERT_EQ(sends.size(), 2u);
    for (const auto& s : sends) {
        EXPECT_NEAR(s.level, 1.0f, 1e-5);
        EXPECT_FALSE(s.isPreFader);
    }
}

// A track index out of range, and a busTarget naming no bus: both rejected by the
// command layer with the same text on both surfaces, with no SEND_LIST mutation.
TEST_F(BusSendRpcTest, AddSendRejectsBadTrackIdOnBothSurfaces) {
    expectSameFailure("add_send", "project.addSend",
                      QJsonObject{ { "trackId", 99 }, { "busTarget", 1 } });
    EXPECT_TRUE(engine->getReadModel().getTrackSends(0).empty());
}

TEST_F(BusSendRpcTest, AddSendRejectsUnknownBusTargetOnBothSurfaces) {
    expectSameFailure("add_send", "project.addSend",
                      QJsonObject{ { "trackId", 0 }, { "busTarget", 999 } });
    EXPECT_TRUE(engine->getReadModel().getTrackSends(0).empty());
}

// remove_send / project.removeSend: identical "ok" payload, each surface dropping
// an equivalent send, and the ReadModel reflects both removals.
TEST_F(BusSendRpcTest, RemoveSendMatchesMcp) {
    auto& cmds = engine->getProjectCommands();
    ASSERT_EQ(cmds.createSend(0, 1, 0.5f, false).sendIndex, 0);
    ASSERT_EQ(cmds.createSend(1, 1, 0.5f, false).sendIndex, 0);

    const QJsonValue viaMcp = mcpValue("remove_send",
                                       QJsonObject{ { "trackId", 0 }, { "sendIndex", 0 } });
    const QJsonValue viaRpc = rpcPayload("project.removeSend",
                                         QJsonObject{ { "trackId", 1 }, { "sendIndex", 0 } });
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaRpc, QJsonValue(QString("ok")));
    EXPECT_TRUE(engine->getReadModel().getTrackSends(0).empty());
    EXPECT_TRUE(engine->getReadModel().getTrackSends(1).empty());
}

// A send index that is not there, and a track that is not there: refused
// identically on both surfaces, with the existing send left in place.
TEST_F(BusSendRpcTest, RemoveSendUnknownIndexFailsOnBothSurfaces) {
    ASSERT_EQ(engine->getProjectCommands().createSend(0, 1, 0.5f, false).sendIndex, 0);
    expectSameFailure("remove_send", "project.removeSend",
                      QJsonObject{ { "trackId", 0 }, { "sendIndex", 5 } });
    expectSameFailure("remove_send", "project.removeSend",
                      QJsonObject{ { "trackId", 99 }, { "sendIndex", 0 } });
    EXPECT_EQ(engine->getReadModel().getTrackSends(0).size(), 1u)
        << "a rejected removeSend must leave the send in place";
}

// --- Bus FX params + list_buses — slice C of ---------------------------------
//     docs/plans/2026-09-22-bus-fx-params.md
//
// Slice B made a bus return REACHABLE (add_bus / add_send) and SHAPABLE at the send
// end; these three pairs make the return itself shapable (set_bus_fx_param) and
// visible (list_buses / list_bus_fx_params). Same discipline as the creators above:
// one argument object into both surfaces, the same command / the same shaping
// (common/BusInfo.h) behind both, and the SUCCESS payload and the FAILURE text
// asserted identical — the argument keys are part of the contract, so the shared
// object is what proves the route keys mirror the tool property names.

// G4: list_buses / read.listBuses list every bus (sorted by busID), agree with each
// other, and agree with what a SAVE shows — including a bus created a moment earlier
// by add_bus, listed with the fxType it was created with.
TEST_F(BusSendRpcTest, ListBusesMatchesMcpAndTheSavedProject) {
    const QJsonObject addArgs{ { "busType", "fx" }, { "name", "Dub Delay" },
                               { "fxType", "delay" }, { "busTarget", 0 } };
    const int newBusID = mcpValue("add_bus", addArgs).toObject().value("busID").toInt(-1);
    ASSERT_GE(newBusID, 0);

    const QJsonValue viaMcp = mcpValue("list_buses", QJsonObject{});
    const QJsonValue viaRpc = rpcPayload("read.listBuses", QJsonObject{});
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isArray()) << "list_buses returns an array of buses";
    const QJsonArray listed = viaMcp.toArray();
    ASSERT_EQ(listed.size(), busCount()) << "every bus in BUS_LIST must be listed";

    int previousID = -1;
    for (const auto& v : listed) {
        const QJsonObject bus = v.toObject();
        const int id = bus.value("busID").toInt(-1);
        EXPECT_GT(id, previousID) << "list_buses is sorted by busID";
        previousID = id;
        expectBusMatchesNode(bus, busNode(id));
    }

    // The freshly created bus is there, described by the request that created it.
    const QJsonObject created = listedBus(listed, newBusID);
    ASSERT_FALSE(created.isEmpty()) << "the bus add_bus just created is not listed";
    EXPECT_EQ(created.value("name").toString().toStdString(), "Dub Delay");
    EXPECT_EQ(created.value("busType").toString().toStdString(), "fx");
    EXPECT_EQ(created.value("fxType").toString().toStdString(), "delay");
    EXPECT_EQ(created.value("busTarget").toInt(), 0);

    // What a save shows: serialize the project the way save_project does, load it
    // into a FRESH model, and require the listing to describe that tree. A listing
    // built from anything but the saved state fails here.
    const juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getNonexistentChildFile("hdaw_list_buses_rpc_test", ".hdaw", false);
    ASSERT_TRUE(HDAW::ProjectSerializer::save(engine->getProjectModel(), file));
    ProjectModel reloaded;
    ASSERT_TRUE(HDAW::ProjectSerializer::load(reloaded, file));
    file.deleteFile();

    const auto savedBuses = reloaded.getBusListTree();
    ASSERT_EQ(static_cast<int>(listed.size()), savedBuses.getNumChildren());
    for (const auto& v : listed) {
        const QJsonObject bus = v.toObject();
        const int id = bus.value("busID").toInt(-1);
        const auto saved = busNodeIn(savedBuses, id);
        ASSERT_TRUE(saved.isValid()) << "busID " << id << " is missing from the saved project";
        expectBusMatchesNode(bus, saved);
    }
}

// G5: list_bus_fx_params / read.listBusFxParams return the same payload, and it is the
// vocabulary list_fx_params uses for track FX (index / name / minValue / maxValue /
// defaultValue / value) — the defs the bus DSP honors, in index order.
TEST_F(BusSendRpcTest, ListBusFxParamsMatchesMcp) {
    const QJsonObject args{ { "busID", 1 } };   // the default project's "Reverb" fx bus

    const QJsonValue viaMcp = mcpValue("list_bus_fx_params", args);
    const QJsonValue viaRpc = rpcPayload("read.listBusFxParams", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    const QJsonObject payload = viaMcp.toObject();
    // The bus the defs belong to travels with them, so a caller cannot pair a def list
    // with the wrong bus.
    EXPECT_EQ(payload.value("busID").toInt(-1), 1);
    EXPECT_EQ(payload.value("name").toString().toStdString(), "Reverb");
    EXPECT_EQ(payload.value("busType").toString().toStdString(), "fx");
    EXPECT_EQ(payload.value("fxType").toString().toStdString(), "reverb");

    // The param entry vocabulary IS list_fx_params' (index/name/…Value/value): an agent
    // that has read a track FX slot reads a bus return without translating.
    const QJsonArray params = payload.value("params").toArray();
    ASSERT_EQ(params.size(), 5) << "reverb honors exactly Room Size/Damping/Wet/Dry/Width";
    for (int i = 0; i < params.size(); ++i) {
        const QJsonObject p = params[i].toObject();
        EXPECT_EQ(p.value("index").toInt(), i) << "params are in index order";
        for (const char* key : { "name", "minValue", "maxValue", "defaultValue", "value" })
            EXPECT_TRUE(p.contains(key)) << "param " << i << " is missing " << key;
        EXPECT_FALSE(p.value("name").toString().isEmpty());
        EXPECT_LT(p.value("minValue").toDouble(), p.value("maxValue").toDouble());
        EXPECT_GE(p.value("defaultValue").toDouble(), p.value("minValue").toDouble());
        EXPECT_LE(p.value("defaultValue").toDouble(), p.value("maxValue").toDouble());
        // Nothing has stamped a value onto a fresh bus, so the DSP's power-on value is
        // the def's default — the reader must not report a phantom 0.
        EXPECT_NEAR(p.value("value").toDouble(), p.value("defaultValue").toDouble(), 1e-5);
    }
    EXPECT_EQ(params[0].toObject().value("name").toString().toStdString(), "Room Size");
    EXPECT_NEAR(params[0].toObject().value("defaultValue").toDouble(), 0.5, 1e-5);
    EXPECT_NEAR(params[0].toObject().value("minValue").toDouble(), 0.0, 1e-5);
    EXPECT_NEAR(params[0].toObject().value("maxValue").toDouble(), 1.0, 1e-5);
}

// The two read rejections, identical on both surfaces: an unknown busID and a bus
// that carries no FX (the master bus; a 'group' bus is the other shape).
TEST_F(BusSendRpcTest, ListBusFxParamsFailuresMatchOnBothSurfaces) {
    expectSameFailure("list_bus_fx_params", "read.listBusFxParams",
                      QJsonObject{ { "busID", 999 } });
    // busID 0 is the master bus (busType 'master', fxType 'none').
    expectSameFailure("list_bus_fx_params", "read.listBusFxParams",
                      QJsonObject{ { "busID", 0 } });

    const int groupID = engine->getProjectCommands()
                            .createBus("group", "Drum Bus", "", 0).busID;
    ASSERT_GE(groupID, 0);
    expectSameFailure("list_bus_fx_params", "read.listBusFxParams",
                      QJsonObject{ { "busID", groupID } });
}

// set_bus_fx_param / project.setBusFxParam: identical "ok" payload, and the write is
// durable — it lands on the BUS node as param_N (what a save serializes) and the read
// tool reports it back, so the two surfaces cannot disagree about what was applied.
TEST_F(BusSendRpcTest, SetBusFxParamMatchesMcp) {
    const QJsonObject args{ { "busID", 1 }, { "paramIndex", 0 }, { "value", 0.75 } };

    const QJsonValue viaMcp = mcpValue("set_bus_fx_param", args);
    const QJsonValue viaRpc = rpcPayload("project.setBusFxParam", args);
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaRpc, QJsonValue(QString("ok")));

    // param_0 on the BUS node is the durable storage (the plan's param_N convention,
    // the property the engine's listener routes into the processor).
    EXPECT_NEAR(static_cast<double>(busNode(1).getProperty("param_0")), 0.75, 1e-5);

    const QJsonArray params =
        mcpValue("list_bus_fx_params", QJsonObject{ { "busID", 1 } })
            .toObject().value("params").toArray();
    ASSERT_FALSE(params.isEmpty());
    EXPECT_NEAR(params[0].toObject().value("value").toDouble(), 0.75, 1e-5);
}

// The three rejections the command owns, reported identically on both surfaces with
// no mutation: unknown busID, a paramIndex the bus's fxType does not have, and a bus
// that is not an fx bus.
TEST_F(BusSendRpcTest, SetBusFxParamFailuresMatchOnBothSurfaces) {
    const auto paramBefore = busNode(1).getProperty("param_0");

    expectSameFailure("set_bus_fx_param", "project.setBusFxParam",
                      QJsonObject{ { "busID", 999 }, { "paramIndex", 0 }, { "value", 0.5 } });
    expectSameFailure("set_bus_fx_param", "project.setBusFxParam",
                      QJsonObject{ { "busID", 1 }, { "paramIndex", 99 }, { "value", 0.5 } });
    expectSameFailure("set_bus_fx_param", "project.setBusFxParam",
                      QJsonObject{ { "busID", 0 }, { "paramIndex", 0 }, { "value", 0.5 } });

    EXPECT_EQ(busNode(1).getProperty("param_0"), paramBefore)
        << "a rejected setBusFxParam must not touch the bus";
}

// --- Argument names (the shared-object assertion) ---------------------------

// The route keys ARE the tool property names. Handing the same object to both
// surfaces is what proves it: a `trackId` -> `trackIndex` rename on the route
// would fail the RPC half with -32602 while the MCP half still succeeded, and a
// rename on the tool side would fail schema validation instead.
TEST_F(BusSendRpcTest, RouteKeysMirrorToolPropertyNames) {
    const QJsonObject addBusArgs{ { "busType", "fx" }, { "name", "Delay" },
                                  { "fxType", "delay" }, { "busTarget", 0 } };
    EXPECT_FALSE(rpc("project.addBus", addBusArgs).isError);
    EXPECT_FALSE(mcpIsError("add_bus", addBusArgs));

    const QJsonObject addSendArgs{ { "trackId", 1 }, { "busTarget", 1 },
                                   { "level", 0.25 }, { "isPreFader", false } };
    EXPECT_FALSE(rpc("project.addSend", addSendArgs).isError);
    EXPECT_FALSE(mcpIsError("add_send", addSendArgs));

    EXPECT_FALSE(rpc("project.removeSend",
                     QJsonObject{ { "trackId", 1 }, { "sendIndex", 0 } }).isError);
    EXPECT_FALSE(mcpIsError("remove_send",
                            QJsonObject{ { "trackId", 1 }, { "sendIndex", 0 } }));

    const int busID = engine->getProjectCommands().createBus("fx", "Tail", "delay", 0).busID;
    ASSERT_GE(busID, 0);
    EXPECT_FALSE(rpc("project.removeBus", QJsonObject{ { "busID", busID } }).isError);

    // Slice C: list_buses / list_bus_fx_params (read.*) and set_bus_fx_param
    // (project.*) take the same object the tools take.
    EXPECT_FALSE(rpc("read.listBuses", QJsonObject{}).isError);
    EXPECT_FALSE(mcpIsError("list_buses", QJsonObject{}));

    const QJsonObject listBusFxArgs{ { "busID", 1 } };
    EXPECT_FALSE(rpc("read.listBusFxParams", listBusFxArgs).isError);
    EXPECT_FALSE(mcpIsError("list_bus_fx_params", listBusFxArgs));

    const QJsonObject setBusFxArgs{ { "busID", 1 }, { "paramIndex", 2 }, { "value", 0.25 } };
    EXPECT_FALSE(rpc("project.setBusFxParam", setBusFxArgs).isError);
    EXPECT_FALSE(mcpIsError("set_bus_fx_param", setBusFxArgs));
}

} // namespace
