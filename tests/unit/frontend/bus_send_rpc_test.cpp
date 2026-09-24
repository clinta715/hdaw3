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
#include "common/SendJson.h"
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

// set_bus_target / project.setBusTarget (docs/plans/2026-09-23-set-bus-target.md,
// slice F): the re-parent pair. Unlike the creators it allocates nothing, so its
// payload carries no fresh id and must match VERBATIM — both surfaces report the
// bare {"ok":true} the frozen contract names, and both drive the same BUS node.
TEST_F(BusSendRpcTest, SetBusTargetMatchesMcp) {
    auto& cmds = engine->getProjectCommands();
    const int hpf = cmds.createBus("fx", "Dub HPF", "filter", 0).busID;
    const int delay = cmds.createBus("fx", "Dub Delay", "delay", 0).busID;
    ASSERT_GE(hpf, 0);
    ASSERT_GE(delay, 0);
    ASSERT_TRUE(busNode(hpf).isValid());
    ASSERT_TRUE(busNode(delay).isValid());

    // ONE args object for both surfaces: the route's key names must BE the MCP
    // property names (`busID` / `busTarget`), or this fails here instead of
    // sending -32602 in the field.
    const QJsonObject args{ { "busID", delay }, { "busTarget", hpf } };
    const QJsonValue viaMcp = mcpValue("set_bus_target", args);
    const QJsonValue viaRpc = rpcPayload("project.setBusTarget", args);
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_TRUE(viaMcp.toObject().value("ok").toBool());

    // Both calls wrote the same parent (the second merely repeats it — an
    // idempotent re-parent is accepted, not reported as a conflict), and the
    // listing an agent reads back agrees with the edit.
    EXPECT_EQ(static_cast<int>(busNode(delay).getProperty(IDs::busTarget)), hpf);
    EXPECT_EQ(listedBus(mcpValue("list_buses", QJsonObject{}).toArray(), delay)
                  .value("busTarget").toInt(),
              hpf);
}

// Every refusal the command layer owns, on the shared args object: a cycle closed
// through a descendant, the master, the bus itself, an unknown parent, an unknown
// bus. Same text on both surfaces, and the tree never moves.
TEST_F(BusSendRpcTest, SetBusTargetRefusalsMatchMcp) {
    auto& cmds = engine->getProjectCommands();
    const int hpf = cmds.createBus("fx", "Dub HPF", "filter", 0).busID;
    const int delay = cmds.createBus("fx", "Dub Delay", "delay", hpf).busID;   // delay -> HPF
    ASSERT_GE(hpf, 0);
    ASSERT_GE(delay, 0);

    const juce::String treeBefore = engine->getProjectModel().getTree().toXmlString();

    // The cycle the transitive walk exists for: the proposed parent is the HPF's
    // own child (a loop createBus could never have made).
    expectSameFailure("set_bus_target", "project.setBusTarget",
                      QJsonObject{ { "busID", hpf }, { "busTarget", delay } });
    EXPECT_TRUE(rpc("project.setBusTarget", QJsonObject{ { "busID", hpf }, { "busTarget", delay } })
                    .payload.toObject().value("message").toString().contains("descendant"))
        << "the transitive cycle refusal must say why";

    expectSameFailure("set_bus_target", "project.setBusTarget",      // the master
                      QJsonObject{ { "busID", 0 }, { "busTarget", hpf } });
    expectSameFailure("set_bus_target", "project.setBusTarget",      // itself
                      QJsonObject{ { "busID", hpf }, { "busTarget", hpf } });
    expectSameFailure("set_bus_target", "project.setBusTarget",      // no such parent
                      QJsonObject{ { "busID", hpf }, { "busTarget", 999 } });
    expectSameFailure("set_bus_target", "project.setBusTarget",      // no such bus
                      QJsonObject{ { "busID", 999 }, { "busTarget", 0 } });

    EXPECT_EQ(engine->getProjectModel().getTree().toXmlString(), treeBefore)
        << "a refused re-parent changed the tree";
    EXPECT_EQ(static_cast<int>(busNode(delay).getProperty(IDs::busTarget)), hpf)
        << "a refused re-parent moved the bus";
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

// remove_send / project.removeSend: identical shift payload on both surfaces,
// each surface dropping an equivalent send, and the ReadModel reflects both
// removals. HANDOFF-7 FLAGGED UPDATE: this test previously pinned the bare
// string "ok" — removeSend's payload was extended ADDITIVELY on both surfaces
// with {removed, shifted} (mirrored byte-for-byte), so the pin moved to the
// new object. Each track held exactly one send, so this is a LAST-send
// removal: shifted must be [].
TEST_F(BusSendRpcTest, RemoveSendMatchesMcp) {
    auto& cmds = engine->getProjectCommands();
    ASSERT_EQ(cmds.createSend(0, 1, 0.5f, false).sendIndex, 0);
    ASSERT_EQ(cmds.createSend(1, 1, 0.5f, false).sendIndex, 0);

    const QJsonValue viaMcp = mcpValue("remove_send",
                                       QJsonObject{ { "trackId", 0 }, { "sendIndex", 0 } });
    const QJsonValue viaRpc = rpcPayload("project.removeSend",
                                         QJsonObject{ { "trackId", 1 }, { "sendIndex", 0 } });
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaRpc, QJsonValue(QJsonObject{ { "ok", true }, { "removed", 0 },
                                              { "shifted", QJsonArray{} } }));
    EXPECT_TRUE(engine->getReadModel().getTrackSends(0).empty());
    EXPECT_TRUE(engine->getReadModel().getTrackSends(1).empty());
}

// Handoff 7 (B): removing a NON-last send reports the index shift — the same
// payload object on both surfaces — and the survivor reindexes to 0; the
// LAST-send case on the same surfaces reports an empty shifted array.
TEST_F(BusSendRpcTest, RemoveSendReportsShiftOnBothSurfaces) {
    auto& cmds = engine->getProjectCommands();
    ASSERT_GE(cmds.createSend(0, 1, 0.5f, false).sendIndex, 0);   // track 0: index 0
    ASSERT_GE(cmds.createSend(0, 1, 0.5f, false).sendIndex, 1);   // track 0: index 1
    ASSERT_GE(cmds.createSend(1, 1, 0.5f, false).sendIndex, 0);
    ASSERT_GE(cmds.createSend(1, 1, 0.5f, false).sendIndex, 1);

    // Non-last removal (sendIndex 0 of 2): {from:1,to:0} on BOTH surfaces.
    const QJsonValue nonLast = QJsonObject{
        { "ok", true }, { "removed", 0 },
        { "shifted", QJsonArray{ QJsonObject{ { "from", 1 }, { "to", 0 } } } } };
    const QJsonValue viaMcp = mcpValue("remove_send",
                                       QJsonObject{ { "trackId", 0 }, { "sendIndex", 0 } });
    const QJsonValue viaRpc = rpcPayload("project.removeSend",
                                         QJsonObject{ { "trackId", 1 }, { "sendIndex", 0 } });
    EXPECT_EQ(viaMcp, nonLast);
    EXPECT_EQ(viaRpc, nonLast);
    EXPECT_EQ(viaRpc, viaMcp);
    const auto sends0 = engine->getReadModel().getTrackSends(0);
    const auto sends1 = engine->getReadModel().getTrackSends(1);
    ASSERT_EQ(sends0.size(), 1u);
    EXPECT_EQ(sends0[0].sendIndex, 0) << "the survivor reindexes to 0";
    ASSERT_EQ(sends1.size(), 1u);
    EXPECT_EQ(sends1[0].sendIndex, 0);

    // Last-send removal: empty shifted on both surfaces.
    const QJsonValue last = QJsonObject{ { "ok", true }, { "removed", 0 },
                                         { "shifted", QJsonArray{} } };
    const QJsonValue lastMcp = mcpValue("remove_send",
                                        QJsonObject{ { "trackId", 0 }, { "sendIndex", 0 } });
    const QJsonValue lastRpc = rpcPayload("project.removeSend",
                                          QJsonObject{ { "trackId", 1 }, { "sendIndex", 0 } });
    EXPECT_EQ(lastMcp, last);
    EXPECT_EQ(lastRpc, last);
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

// Handoff item-7: removeSend must also remap the durable sendIndex encoded in
// automation-lane paramIDs (2000 + sendIndex) — and the remap lives in the ONE
// shared splice command, so both surfaces produce the same TREE state, not
// just the same payload: the removed send's lane is gone, survivor lanes
// decrement, 2999 rides the send-range window, and the 3000+ bus pid never
// moves. Track 0 goes through MCP, track 1 through RPC, identical setup.
TEST_F(BusSendRpcTest, RemoveSendRemapsLanePidsOnBothSurfaces) {
    auto& cmds = engine->getProjectCommands();
    for (int t = 0; t < 2; ++t) {
        ASSERT_GE(cmds.createSend(t, 1, 0.5f, false).sendIndex, 0);
        ASSERT_GE(cmds.createSend(t, 1, 0.5f, false).sendIndex, 1);
        ASSERT_TRUE(cmds.addAutomationLane(t, "LaneA", 2000));
        ASSERT_TRUE(cmds.addAutomationLane(t, "LaneB", 2001));
        ASSERT_TRUE(cmds.addAutomationLane(t, "Ghost999", 2999));
        ASSERT_TRUE(cmds.addAutomationLane(t, "BusRoom", 3000 + 1 * 8 + 0));
    }
    engine->drainPendingRoutingRebuild();

    const QJsonValue viaMcp = mcpValue("remove_send",
                                       QJsonObject{ { "trackId", 0 }, { "sendIndex", 0 } });
    const QJsonValue viaRpc = rpcPayload("project.removeSend",
                                         QJsonObject{ { "trackId", 1 }, { "sendIndex", 0 } });
    const QJsonValue expected = QJsonObject{
        { "ok", true }, { "removed", 0 },
        { "shifted", QJsonArray{ QJsonObject{ { "from", 1 }, { "to", 0 } } } } };
    EXPECT_EQ(viaMcp, expected);
    EXPECT_EQ(viaRpc, expected);

    // paramID of the named lane, -999 when absent — the tree contract both
    // surfaces must land identically through the shared command.
    auto lanePid = [this](int track, const std::string& name) {
        const auto list = engine->getProjectModel().getTrackListTree()
                              .getChild(track).getChildWithName(IDs::AUTOMATION_LIST);
        for (int i = 0; i < list.getNumChildren(); ++i) {
            const auto lane = list.getChild(i);
            if (lane.getProperty(IDs::name, "").toString().toStdString() == name)
                return static_cast<int>(lane.getProperty(IDs::paramID, 0));
        }
        return -999;
    };
    for (int t = 0; t < 2; ++t) {
        EXPECT_EQ(lanePid(t, "LaneA"), -999)
            << "the removed send's lane must be gone (track " << t << ")";
        EXPECT_EQ(lanePid(t, "LaneB"), 2000)
            << "survivor lane decrements onto the reindexed send (track " << t << ")";
        EXPECT_EQ(lanePid(t, "Ghost999"), 2998)
            << "2999 shifts with the send-range survivor window (track " << t << ")";
        EXPECT_EQ(lanePid(t, "BusRoom"), 3000 + 1 * 8 + 0)
            << "stable bus pid never remapped (track " << t << ")";
    }
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

// --- One shape per operation: duplicate_track -------------------------------

// duplicate_track / project.duplicateTrack (2026-09-23): the tool answered the
// text "trackId=N routed=1" while the route answered a bare int — two shapes for
// one operation, so no consumer could compare them and no twin test could assert
// agreement. Both now answer the object common/TrackJson.h shapes
// ({trackId, routed}), and this is the direct comparison the fix exists for.
// The new index differs between the two calls (each duplicates), so the payload
// is compared structurally with that one field blanked — the bus/send creation
// pattern above.
TEST_F(BusSendRpcTest, DuplicateTrackReturnsTheSamePayloadOnBothSurfaces) {
    const QJsonObject args{ { "trackId", 0 } };

    const QJsonValue viaMcp = mcpValue("duplicate_track", args);
    const QJsonValue viaRpc = rpcPayload("project.duplicateTrack", args);
    ASSERT_TRUE(viaMcp.isObject());
    ASSERT_TRUE(viaRpc.isObject());

    // Same keys, same values except the freshly allocated index.
    EXPECT_EQ(viaRpc.toObject().size(), viaMcp.toObject().size());
    EXPECT_EQ(viaRpc.toObject().value("routed"), viaMcp.toObject().value("routed"));
    EXPECT_EQ(viaMcp.toObject().value("routed").toInt(), 1);

    const int mcpId = viaMcp.toObject().value("trackId").toInt(-1);
    const int rpcId = viaRpc.toObject().value("trackId").toInt(-1);
    EXPECT_GE(mcpId, 0);
    EXPECT_GE(rpcId, 0);
    EXPECT_NE(mcpId, rpcId) << "each call duplicates its own copy";

    // Both copies are real tracks in TRACK_LIST (the payload describes the tree
    // the command actually mutated, not a fabricated index).
    auto tl = engine->getProjectModel().getTrackListTree();
    for (const int id : { mcpId, rpcId }) {
        ASSERT_LT(id, tl.getNumChildren()) << "trackId " << id << " is not in TRACK_LIST";
        EXPECT_EQ(tl.getChild(id).getProperty(IDs::name).toString().toStdString(), "Kick copy");
    }
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

    // The legacy four (renamed 2026-09-23, trap legacy-send-arg-mismatch): the
    // send reader and the three send shapers take the MCP tool's keys — `trackId`,
    // the name the tools were born with — not the pre-parity `trackIndex`.
    ASSERT_GE(engine->getProjectCommands().createSend(0, 1, 0.5f, false).sendIndex, 0);

    const QJsonObject trackSendsArgs{ { "trackId", 0 } };
    EXPECT_FALSE(rpc("read.getTrackSends", trackSendsArgs).isError);
    EXPECT_FALSE(mcpIsError("get_track_sends", trackSendsArgs));

    const QJsonObject sendLevelArgs{ { "trackId", 0 }, { "sendIndex", 0 }, { "level", 0.25 } };
    EXPECT_FALSE(rpc("project.setTrackSendLevel", sendLevelArgs).isError);
    EXPECT_FALSE(mcpIsError("set_track_send_level", sendLevelArgs));

    const QJsonObject sendModeArgs{ { "trackId", 0 }, { "sendIndex", 0 }, { "isPreFader", true } };
    EXPECT_FALSE(rpc("project.setTrackSendMode", sendModeArgs).isError);
    EXPECT_FALSE(mcpIsError("set_track_send_mode", sendModeArgs));

    const QJsonObject sendBypassArgs{ { "trackId", 0 }, { "sendIndex", 0 }, { "bypassed", true } };
    EXPECT_FALSE(rpc("project.setTrackSendBypassed", sendBypassArgs).isError);
    EXPECT_FALSE(mcpIsError("set_track_send_bypassed", sendBypassArgs));
}

// The negative half for the same four routes: the OLD argument name must be
// rejected on BOTH surfaces (a renamed argument is not a parity twin —
// apply_preset_test.cpp LoadNordBankRpcTwinSharesLoaderFailure), the route's
// -32602 must name `trackId` (never the retired `trackIndex`), and when trackId
// is merely absent BOTH failures must identify `trackId`. Unlike
// expectSameFailure, the messages are not compared verbatim: an arg-name failure
// surfaces from DIFFERENT validators per surface (the route's requireInt vs the
// tool's JSON schema), so the shared contract is the rejected payload and the
// named key, not byte-identical text.
TEST_F(BusSendRpcTest, LegacySendRoutesRejectTrackIndexOnBothSurfaces) {
    ASSERT_GE(engine->getProjectCommands().createSend(0, 1, 0.5f, false).sendIndex, 0);

    struct Case { const char* tool; const char* method; QJsonObject rest; };
    const Case cases[] = {
        { "get_track_sends",         "read.getTrackSends",           QJsonObject{} },
        { "set_track_send_level",    "project.setTrackSendLevel",
              QJsonObject{ { "sendIndex", 0 }, { "level", 0.25 } } },
        { "set_track_send_mode",     "project.setTrackSendMode",
              QJsonObject{ { "sendIndex", 0 }, { "isPreFader", true } } },
        { "set_track_send_bypassed", "project.setTrackSendBypassed",
              QJsonObject{ { "sendIndex", 0 }, { "bypassed", true } } },
    };

    for (const auto& c : cases) {
        // The old name in place of trackId: the route fails its normal missing-arg
        // check naming the key it WANTS, and the tool schema refuses the payload too.
        QJsonObject renamed = c.rest;
        renamed["trackIndex"] = 0;
        const auto r = rpc(c.method, renamed);
        ASSERT_TRUE(r.isError) << c.method << " accepted the old trackIndex key";
        EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602) << c.method;
        const QString rpcMsg = r.payload.toObject().value("message").toString();
        EXPECT_TRUE(rpcMsg.contains("trackId")) << c.method << ": " << rpcMsg.toStdString();
        EXPECT_FALSE(rpcMsg.contains("trackIndex"))
            << c.method << " still names the retired trackIndex: " << rpcMsg.toStdString();
        EXPECT_TRUE(mcpIsError(c.tool, renamed)) << c.tool << " accepted the old trackIndex key";

        // trackId absent entirely: both surfaces fail AND both name trackId.
        const auto r2 = rpc(c.method, c.rest);
        ASSERT_TRUE(r2.isError) << c.method;
        const QString rpcMissing = r2.payload.toObject().value("message").toString();
        EXPECT_TRUE(rpcMissing.contains("trackId")) << c.method << ": " << rpcMissing.toStdString();
        EXPECT_TRUE(mcpIsError(c.tool, c.rest)) << c.tool;
        const QString mcpMissing = mcpText(c.tool, c.rest);
        EXPECT_FALSE(mcpMissing.isEmpty()) << c.tool << " failed without a reason";
        EXPECT_TRUE(mcpMissing.contains("trackId")) << c.tool << ": " << mcpMissing.toStdString();

        // The new name: accepted by BOTH surfaces. The mutators answer Null (RPC)
        // vs "ok" (MCP) by surface design, so acceptance — not payload equality —
        // is the contract here; the read twin's payload equality is asserted below.
        QJsonObject valid = c.rest;
        valid["trackId"] = 0;
        EXPECT_FALSE(rpc(c.method, valid).isError) << c.method;
        EXPECT_FALSE(mcpIsError(c.tool, valid)) << c.tool;
    }

    // read.getTrackSends / get_track_sends answer the accepted payload with the
    // same array (sendIndex / level / isPreFader / bypassed on both surfaces).
    const QJsonObject withTrackId{ { "trackId", 0 } };
    EXPECT_EQ(rpcPayload("read.getTrackSends", withTrackId),
              mcpValue("get_track_sends", withTrackId));
}

// --- Read payload parity: sends + FX slots (common/SendJson.h) ---------------

// get_track_sends / read.getTrackSends were TWO hand-written serializers (the MCP
// inline literal vs the router's toJson(SendSnapshot)) held equal only by the
// assertion at the end of LegacySendRoutesRejectTrackIndexOnBothSurfaces. Both now
// shape through common/SendJson.h::shapeSendsJson, so this drives both surfaces over
// a two-send track — a mixed pre/post-fader pair, the flag a shaper is most likely to
// drop — and requires the SAME payload, key for key and value for value.
TEST_F(BusSendRpcTest, TrackSendsMatchMcpWithMixedPrePostPair) {
    ASSERT_GE(engine->getProjectCommands().createSend(0, 1, 0.371f, false).sendIndex, 0);
    ASSERT_GE(engine->getProjectCommands().createSend(0, 1, 0.75f, true).sendIndex, 0);
    engine->drainPendingRoutingRebuild();

    const QJsonObject args{ { "trackId", 0 } };
    const QJsonValue viaMcp = mcpValue("get_track_sends", args);
    const QJsonValue viaRpc = rpcPayload("read.getTrackSends", args);
    EXPECT_EQ(viaRpc, viaMcp) << "both surfaces must shape ONE payload (common/SendJson.h)";

    ASSERT_TRUE(viaMcp.isArray());
    const QJsonArray rows = viaMcp.toArray();
    ASSERT_EQ(rows.size(), 2);
    for (int i = 0; i < rows.size(); ++i) {
        const QJsonObject row = rows[i].toObject();
        EXPECT_EQ(row.value("sendIndex").toInt(-1), i) << "rows are in SEND_LIST order";
        EXPECT_EQ(row.size(), 5)
            << "the send vocabulary: sendIndex / sendID / level / isPreFader / bypassed";
        for (const char* key : { "sendIndex", "sendID", "level", "isPreFader", "bypassed" })
            EXPECT_TRUE(row.contains(key)) << "send row " << i << " is missing " << key;
        // sendIndex IS positional (it is the argument every send tool takes and it
        // renumbers when a lower send is removed); sendID is the stable identity
        // (design B1) — read the address from one, the identity from the other.
        EXPECT_GT(row.value("sendID").toInt(0), 0) << "a live send carries a stable id";
    }
    EXPECT_NE(rows[0].toObject().value("sendID"), rows[1].toObject().value("sendID"))
        << "two sends must not share an id";
    EXPECT_FALSE(rows[0].toObject().value("isPreFader").toBool()) << "send 0 is post-fader";
    EXPECT_TRUE(rows[1].toObject().value("isPreFader").toBool()) << "send 1 is pre-fader";
    EXPECT_NEAR(rows[0].toObject().value("level").toDouble(), 0.371, 1e-5);
    EXPECT_NEAR(rows[1].toObject().value("level").toDouble(), 0.75, 1e-5);
    EXPECT_FALSE(rows[0].toObject().value("bypassed").toBool());
    EXPECT_FALSE(rows[1].toObject().value("bypassed").toBool());

    // Grounded in the tree both surfaces project: SEND_LIST holds exactly the two
    // sends, in order, with the modes the payload reports.
    const auto sendList = engine->getProjectModel().getTrackListTree()
                              .getChild(0).getChildWithName(IDs::SEND_LIST);
    ASSERT_EQ(sendList.getNumChildren(), 2);
    EXPECT_EQ(sendList.getChild(0).getProperty(IDs::sendMode).toString(), juce::String("post"));
    EXPECT_EQ(sendList.getChild(1).getProperty(IDs::sendMode).toString(), juce::String("pre"));
}

// list_fx / read.getFxSlots were DIVERGENT, not merely duplicated: MCP emitted
// slot/type/pluginId/pluginFormat/paramCount while the router emitted
// slotIndex/fxType/pluginId/pluginName/pluginFormat/bypassed/paramCount — different key
// VOCABULARIES for one entity. Both now emit the ONE canonical vocabulary (slotIndex /
// fxType / pluginId / pluginName / pluginFormat / paramCount / bypassed — the argument
// names every FX tool takes, and what the live consumers read; the evidence is recorded
// in common/SendJson.h), with the plugin-only conditionality preserved for the plugin
// IDENTITY fields (pluginId / pluginName / pluginFormat) — paramCount rides every slot,
// because an internal slot's defs-table size is exactly the "did it load" readback the
// 2026-09-23 paramCount fix exists to provide.
TEST_F(BusSendRpcTest, FxSlotsMatchMcpOnTheCanonicalVocabulary) {
    ASSERT_FALSE(rpc("project.addFxSlot",
                     QJsonObject{ { "trackIndex", 0 }, { "fxType", "eq" } }).isError);
    ASSERT_FALSE(rpc("project.addFxSlot",
                     QJsonObject{ { "trackIndex", 0 }, { "fxType", "reverb" } }).isError);
    // A plugin slot WITHOUT a real plugin: setFxSlotPlugin only writes tree properties
    // (the same fixture frontend_server_test.cpp's FxSlotPluginFormatExposed uses).
    ASSERT_FALSE(rpc("project.setFxSlotPlugin",
                     QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 1 },
                                   { "fxType", "plugin" }, { "pluginID", "test.plugin" },
                                   { "pluginFormat", "VST3" },
                                   { "pluginPath", "/path/test.vst3" } }).isError);
    engine->drainPendingRoutingRebuild();

    const QJsonValue viaMcp = mcpValue("list_fx", QJsonObject{ { "trackId", 0 } });
    const QJsonValue viaRpc = rpcPayload("read.getFxSlots", QJsonObject{ { "trackIndex", 0 } });
    EXPECT_EQ(viaRpc, viaMcp) << "both surfaces must shape ONE payload (common/SendJson.h)";

    ASSERT_TRUE(viaMcp.isArray());
    const QJsonArray rows = viaMcp.toArray();
    ASSERT_EQ(rows.size(), 2);

    // The internal slot: the shared keys — no plugin IDENTITY fields that would read as
    // a loaded plugin with an empty id. paramCount IS carried: for an internal slot it
    // is the defs-table size (the 2026-09-23 paramCount fix), which the router always
    // reported and the MCP tool omitted — dropping it would lose that readback.
    const QJsonObject internal = rows[0].toObject();
    EXPECT_EQ(internal.value("slotIndex").toInt(-1), 0);
    EXPECT_EQ(internal.value("fxType").toString().toStdString(), "eq");
    EXPECT_FALSE(internal.value("bypassed").toBool());
    EXPECT_EQ(internal.size(), 4) << "an internal slot reports slotIndex/fxType/paramCount/bypassed";
    for (const char* key : { "pluginId", "pluginName", "pluginFormat" })
        EXPECT_FALSE(internal.contains(key)) << "an internal slot must not carry " << key;
    EXPECT_TRUE(internal.contains("paramCount"))
        << "paramCount rides every slot, internal ones included";

    // The plugin slot: the plugin-only fields ride with it (values, not just keys).
    const QJsonObject plugin = rows[1].toObject();
    EXPECT_EQ(plugin.value("slotIndex").toInt(-1), 1);
    EXPECT_EQ(plugin.value("fxType").toString().toStdString(), "plugin");
    EXPECT_EQ(plugin.value("pluginId").toString().toStdString(), "test.plugin");
    EXPECT_EQ(plugin.value("pluginFormat").toString().toStdString(), "VST3");
    EXPECT_FALSE(plugin.value("bypassed").toBool());
    for (const char* key : { "pluginId", "pluginName", "pluginFormat", "paramCount" })
        EXPECT_TRUE(plugin.contains(key)) << "a plugin slot must carry " << key;
    EXPECT_EQ(plugin.value("paramCount").toInt(-1), 0)
        << "no live plugin instance in this fixture, so the count is 0";

    // Grounded in the tree + the LIVE chain: FX_CHAIN holds the two slots the payload
    // describes, and the rebuild built the internal one (the plugin slot's instance is
    // deliberately absent — setFxSlotPlugin writes tree properties only).
    const auto fxChain = engine->getProjectModel().getTrackListTree()
                             .getChild(0).getChildWithName(IDs::FX_CHAIN);
    ASSERT_EQ(fxChain.getNumChildren(), 2);
    EXPECT_EQ(fxChain.getChild(1).getProperty(IDs::fxType).toString(), juce::String("plugin"));
    auto* proc = engine->getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* track = proc->getTrack(0);
    ASSERT_NE(track, nullptr);
    ASSERT_GE(static_cast<int>(track->getFXChain().size()), 1);
    ASSERT_TRUE(track->getFXChain()[0] != nullptr);
    EXPECT_EQ(track->getFXChain()[0]->getType().toStdString(), "eq");
    // paramCount is the LIVE slot's count, not a key that merely exists: the internal
    // eq slot reports its defs-table size (3), so a shaper that zeroed or dropped the
    // field fails here rather than silently mis-reporting "not loaded".
    EXPECT_EQ(internal.value("paramCount").toInt(-1),
              track->getFXChain()[0]->paramCount())
        << "an internal slot's paramCount is the defs-table size, read from the live slot";
    EXPECT_GT(internal.value("paramCount").toInt(0), 0);
}

// The shared builder IS the MCP text path — byte for byte, distinctive level included —
// and each router payload is that same document parsed. A re-inlined serializer on
// either side fails here even if it happens to agree today, which is the drift class
// this slice exists to close.
TEST_F(BusSendRpcTest, SendAndFxShapingIsTheSharedBuilderOnBothSurfaces) {
    ASSERT_GE(engine->getProjectCommands().createSend(0, 1, 0.371f, false).sendIndex, 0);
    ASSERT_FALSE(rpc("project.addFxSlot",
                     QJsonObject{ { "trackIndex", 0 }, { "fxType", "delay" } }).isError);
    engine->drainPendingRoutingRebuild();

    const QJsonObject sendArgs{ { "trackId", 0 } };
    const QString sendText = mcpText("get_track_sends", sendArgs);
    EXPECT_EQ(sendText, QString::fromStdString(
        HDAW::shapeSendsJson(engine->getReadModel().getTrackSends(0))));
    EXPECT_EQ(rpcPayload("read.getTrackSends", sendArgs),
              QJsonValue(QJsonDocument::fromJson(sendText.toUtf8()).array()));

    const QJsonObject fxArgs{ { "trackId", 0 } };
    const QString fxText = mcpText("list_fx", fxArgs);
    EXPECT_EQ(fxText, QString::fromStdString(
        HDAW::shapeFxSlotsJson(engine->getReadModel().getFxSlots(0))));
    EXPECT_EQ(rpcPayload("read.getFxSlots", QJsonObject{ { "trackIndex", 0 } }),
              QJsonValue(QJsonDocument::fromJson(fxText.toUtf8()).array()));
}

// ── Stable ids (design B1) agree across the surfaces and survive a splice ───
// `trackID` / `sendID` are identities, not addresses: the MCP creation echo, the
// RPC snapshot and the ValueTree must all report the SAME number for the same
// entity, and that number must still name it after a removal renumbers the index.
// (`trackId` / `sendIndex` keep meaning the positional address — asserted here by
// reading the SAME entity back through the new index and getting the old id.)
TEST_F(BusSendRpcTest, StableIdsAgreeAcrossSurfacesAndSurviveASplice) {
    const auto addRes = mcpValue("add_track", QJsonObject{ { "name", "Stable" } });
    ASSERT_TRUE(addRes.isObject()) << "add_track must answer the creation payload";
    const int idx = addRes.toObject().value("trackId").toInt(-1);
    const int trackID = addRes.toObject().value("trackID").toInt(0);
    ASSERT_GE(idx, 0);
    ASSERT_GT(trackID, 0) << "a created track carries a stable id";

    // Same entity, other surface, other shape: the track snapshot.
    const auto snap = rpcPayload("read.getTrack", QJsonObject{ { "trackIndex", idx } });
    EXPECT_EQ(snap.toObject().value("trackID").toInt(0), trackID);
    EXPECT_EQ(snap.toObject().value("index").toInt(-1), idx)
        << "the positional index is still reported next to the identity";

    // Both are projections of the tree.
    const auto tl = engine->getProjectModel().getTrackListTree();
    ASSERT_LT(idx, tl.getNumChildren());
    EXPECT_EQ(static_cast<int>(tl.getChild(idx).getProperty(IDs::trackID, 0)), trackID);

    // A send: the stable id rides the shared row on both surfaces and is the
    // SEND node's own property.
    ASSERT_GE(engine->getProjectCommands().createSend(idx, 1, 0.4f, false).sendIndex, 0);
    engine->drainPendingRoutingRebuild();
    const auto sendList = tl.getChild(idx).getChildWithName(IDs::SEND_LIST);
    ASSERT_TRUE(sendList.isValid());
    ASSERT_EQ(sendList.getNumChildren(), 1);
    const int treeSendID = static_cast<int>(sendList.getChild(0).getProperty(IDs::sendID, 0));
    ASSERT_GT(treeSendID, 0);
    const QJsonObject sendArgs{ { "trackId", idx } };
    EXPECT_EQ(mcpValue("get_track_sends", sendArgs).toArray()[0].toObject()
                  .value("sendID").toInt(0), treeSendID);
    EXPECT_EQ(rpcPayload("read.getTrackSends", sendArgs).toArray()[0].toObject()
                  .value("sendID").toInt(0), treeSendID);

    // Remove a track ABOVE it: every index above the splice shifts, so the entity
    // is now addressed one slot lower — and the id still names it. (The fixture
    // seeds Kick + Bass, so "Stable" is at `idx` and lands at `idx - 1`.)
    const auto rm = rpc("project.removeTrack", QJsonObject{ { "trackId", 0 }, { "force", true } });
    ASSERT_FALSE(rm.isError);
    engine->drainPendingRoutingRebuild();
    const int nowIdx = idx - 1;
    ASSERT_GE(nowIdx, 0);
    EXPECT_EQ(static_cast<int>(tl.getChild(nowIdx).getProperty(IDs::trackID, 0)), trackID)
        << "the stable id is unchanged by a removal that moved the track";
    EXPECT_EQ(tl.getChild(nowIdx).getProperty(IDs::name).toString().toStdString(), "Stable")
        << "and it is still that track (not the neighbour that shifted into the slot)";
    const auto moved = rpcPayload("read.getTrack", QJsonObject{ { "trackIndex", nowIdx } });
    EXPECT_EQ(moved.toObject().value("trackID").toInt(0), trackID);
    EXPECT_EQ(moved.toObject().value("name").toString().toStdString(), "Stable");
}

// ── Stable ids as ARGUMENTS (design B2) ────────────────────────────────────
// B1 put `trackID` / `sendID` on the wire; B2 makes the number an agent actually
// holds usable as an ARGUMENT, additively. These tests hand `trackID`/`sendID`
// with NO positional key and require the tool AND the route to act on that
// entity — the SAME object to both surfaces, so a surface that quietly kept
// parsing only the index fails here instead of mutating the wrong track.

// G2: a track named ONLY by its id (no `trackId`) drives the read, a property
// write, a reorder and the guarded removal — on both surfaces.
TEST_F(BusSendRpcTest, StableTrackIdAloneDrivesBothSurfaces) {
    const auto tl = engine->getProjectModel().getTrackListTree();
    const int idKick = static_cast<int>(tl.getChild(0).getProperty(IDs::trackID, 0));
    const int idBass = static_cast<int>(tl.getChild(1).getProperty(IDs::trackID, 0));
    ASSERT_GT(idKick, 0);
    ASSERT_GT(idBass, 0);
    ASSERT_NE(idKick, idBass);

    // read.getTrackSends / get_track_sends: the fixture's send lives on BASS, so
    // an id that resolves to Kick reports [] while the Bass id reports the row —
    // which is what proves the id selected a track rather than defaulting to 0.
    ASSERT_GE(engine->getProjectCommands().createSend(1, 1, 0.42f, false).sendIndex, 0);
    engine->drainPendingRoutingRebuild();

    const QJsonObject readById{ { "trackID", idBass } };
    const QJsonValue viaMcp = mcpValue("get_track_sends", readById);
    ASSERT_TRUE(viaMcp.isArray());
    ASSERT_EQ(viaMcp.toArray().size(), 1) << "the id must select the track that HAS a send";
    EXPECT_NEAR(viaMcp.toArray()[0].toObject().value("level").toDouble(), 0.42, 1e-5);
    EXPECT_EQ(rpcPayload("read.getTrackSends", readById), viaMcp)
        << "one object, two surfaces, one payload";
    EXPECT_TRUE(mcpValue("get_track_sends", QJsonObject{ { "trackID", idKick } })
                    .toArray().isEmpty())
        << "…and the other track's id reports its own (empty) SEND_LIST";

    // set_track / project.setTrackMuted: the write lands on the id's track. The
    // MCP object is set_track's own spelling (`mute`), the route's is `muted` —
    // the documented divergence; both now accept `trackID`.
    const QJsonObject muteById{ { "trackID", idBass }, { "mute", true } };
    EXPECT_FALSE(mcpIsError("set_track", muteById))
        << mcpText("set_track", muteById).toStdString();
    EXPECT_TRUE(static_cast<bool>(tl.getChild(1).getProperty(IDs::isMuted)));
    EXPECT_FALSE(static_cast<bool>(tl.getChild(0).getProperty(IDs::isMuted)))
        << "the neighbouring track must be untouched";
    EXPECT_FALSE(rpc("project.setTrackMuted",
                     QJsonObject{ { "trackID", idKick }, { "muted", true } }).isError);
    engine->drainPendingRoutingRebuild();
    EXPECT_TRUE(static_cast<bool>(tl.getChild(0).getProperty(IDs::isMuted)));

    // move_track / project.moveTrack: reorder by id (the id survives the move).
    const QJsonObject moveArgs{ { "trackID", idBass }, { "newIndex", 0 } };
    EXPECT_FALSE(mcpIsError("move_track", moveArgs))
        << mcpText("move_track", moveArgs).toStdString();
    engine->drainPendingRoutingRebuild();
    EXPECT_EQ(tl.getChild(0).getProperty(IDs::name).toString().toStdString(), "Bass");
    EXPECT_EQ(static_cast<int>(tl.getChild(0).getProperty(IDs::trackID, 0)), idBass)
        << "the identity travelled with the track, the index changed";
    EXPECT_FALSE(rpc("project.moveTrack",
                     QJsonObject{ { "trackID", idKick }, { "newIndex", 0 } }).isError);
    engine->drainPendingRoutingRebuild();
    EXPECT_EQ(tl.getChild(0).getProperty(IDs::name).toString().toStdString(), "Kick");

    // remove_track / project.removeTrack by id — through the SHARED guard: a
    // clip-carrying track refuses, and the refusal names the track the id
    // resolved to (its CURRENT index), on both surfaces with one text.
    ASSERT_GT(engine->getProjectCommands().addMidiClip(0, 0.0, 4.0, "Guard"), 0);
    engine->drainPendingRoutingRebuild();
    const QJsonObject removeById{ { "trackID", idKick } };
    expectSameFailure("remove_track", "project.removeTrack", removeById);
    EXPECT_TRUE(mcpText("remove_track", removeById).contains("Kick"))
        << "the guard must name the track the id named: "
        << mcpText("remove_track", removeById).toStdString();
    EXPECT_EQ(tl.getNumChildren(), 2) << "a refusal mutates nothing";

    // dryRun by id: the preview text is the positional one (same guard path).
    const QJsonObject dryById{ { "trackID", idKick }, { "dryRun", true } };
    EXPECT_EQ(rpc("project.removeTrack", dryById).payload.toString(),
              mcpText("remove_track", dryById));

    // force:true removes the id's track, and the payload still speaks POSITIONS
    // (`removed` is the index the splice took out — the caller's id is gone).
    const QJsonObject forced{ { "trackID", idKick }, { "force", true } };
    const QJsonValue removed = mcpValue("remove_track", forced);
    ASSERT_TRUE(removed.isObject()) << mcpText("remove_track", forced).toStdString();
    EXPECT_TRUE(removed.toObject().value("ok").toBool());
    EXPECT_EQ(removed.toObject().value("removed").toInt(-1), 0);
    engine->drainPendingRoutingRebuild();
    ASSERT_EQ(tl.getNumChildren(), 1);
    EXPECT_EQ(tl.getChild(0).getProperty(IDs::name).toString().toStdString(), "Bass")
        << "the id's track was the one removed";
}

// G3 + G4: an unknown id and a disagreement fail IDENTICALLY on both surfaces
// (the text comes from one place, common/StableRefResolve.h) and mutate NOTHING.
TEST_F(BusSendRpcTest, StableRefFailuresMatchOnBothSurfaces) {
    const auto tl = engine->getProjectModel().getTrackListTree();
    const int idKick = static_cast<int>(tl.getChild(0).getProperty(IDs::trackID, 0));
    const int idBass = static_cast<int>(tl.getChild(1).getProperty(IDs::trackID, 0));
    ASSERT_GT(idKick, 0);
    ASSERT_NE(idKick, idBass);

    // G3 on the read: `unknown trackID <id>` on both surfaces.
    const QJsonObject readUnknown{ { "trackID", 4242 } };
    expectSameFailure("get_track_sends", "read.getTrackSends", readUnknown);
    EXPECT_EQ(mcpText("get_track_sends", readUnknown).toStdString(), "unknown trackID 4242");

    // G3 on a mutator: the route resolves BEFORE it looks at its property, so
    // the tool's object ({trackID, mute}) is answered by the same text the route
    // gives it — and the write never happens.
    const QJsonObject writeUnknown{ { "trackID", 4242 }, { "mute", true } };
    expectSameFailure("set_track", "project.setTrackMuted", writeUnknown);

    // G3 on the guarded removal (a separate entry point on the RPC side).
    expectSameFailure("remove_track", "project.removeTrack", QJsonObject{ { "trackID", 7 } });

    // G4: positional says Kick, the id says Bass. Both surfaces refuse with the
    // text naming BOTH numbers, and neither track is muted afterwards.
    const QJsonObject clash{ { "trackId", 0 }, { "trackID", idBass }, { "mute", true } };
    expectSameFailure("set_track", "project.setTrackMuted", clash);
    EXPECT_EQ(mcpText("set_track", clash).toStdString(),
              "trackId 0 and trackID " + std::to_string(idBass) + " disagree");
    EXPECT_FALSE(static_cast<bool>(tl.getChild(0).getProperty(IDs::isMuted)))
        << "a disagreement must not fall back to the positional track";
    EXPECT_FALSE(static_cast<bool>(tl.getChild(1).getProperty(IDs::isMuted)))
        << "…nor to the id's track";
    EXPECT_EQ(tl.getNumChildren(), 2);

    // The same rule on the read path: `trackId 1` and the Kick id disagree.
    const QJsonObject clashRead{ { "trackId", 1 }, { "trackID", idKick } };
    expectSameFailure("get_track_sends", "read.getTrackSends", clashRead);

    // …and on the sends: `sendIndex` and `sendID` name different sends.
    ASSERT_GE(engine->getProjectCommands().createSend(0, 1, 0.25f, false).sendIndex, 0);
    ASSERT_GE(engine->getProjectCommands().createSend(0, 1, 0.75f, false).sendIndex, 1);
    engine->drainPendingRoutingRebuild();
    const auto rows = mcpValue("get_track_sends", QJsonObject{ { "trackID", idKick } }).toArray();
    ASSERT_EQ(rows.size(), 2);
    const int sendID1 = rows[1].toObject().value("sendID").toInt(0);
    ASSERT_GT(sendID1, 0);
    const QJsonObject sendClash{ { "trackId", 0 }, { "sendIndex", 0 },
                                 { "sendID", sendID1 }, { "level", 0.1 } };
    expectSameFailure("set_track_send_level", "project.setTrackSendLevel", sendClash);
    EXPECT_EQ(mcpText("set_track_send_level", sendClash).toStdString(),
              "sendIndex 0 and sendID " + std::to_string(sendID1) + " disagree");
    EXPECT_FLOAT_EQ(engine->getReadModel().getTrackSends(0)[0].level, 0.25f)
        << "a refused write must leave the level alone";
}

// G5: `sendID` alone (no `sendIndex`) addresses the send — including after a
// removal renumbered it, which is the property B1 gave the id and B2 now makes
// usable. Both surfaces, and the removed send's id is refused as unknown.
TEST_F(BusSendRpcTest, SendIdAloneAddressesTheSurvivorAfterASplice) {
    auto& cmds = engine->getProjectCommands();
    ASSERT_EQ(cmds.createSend(0, 1, 0.25f, false).sendIndex, 0);   // A
    ASSERT_EQ(cmds.createSend(0, 1, 0.75f, false).sendIndex, 1);   // B
    engine->drainPendingRoutingRebuild();

    const QJsonObject trackById{ { "trackId", 0 } };
    auto rows = mcpValue("get_track_sends", trackById).toArray();
    ASSERT_EQ(rows.size(), 2);
    const int idA = rows[0].toObject().value("sendID").toInt(0);
    const int idB = rows[1].toObject().value("sendID").toInt(0);
    ASSERT_GT(idA, 0);
    ASSERT_NE(idA, idB);

    // The removal itself by INDEX (unchanged behaviour), so B renumbers to 0.
    const QJsonValue removed = mcpValue("remove_send",
                                        QJsonObject{ { "trackId", 0 }, { "sendIndex", 0 } });
    ASSERT_TRUE(removed.isObject());
    EXPECT_EQ(removed.toObject().value("removed").toInt(-1), 0);
    engine->drainPendingRoutingRebuild();
    rows = mcpValue("get_track_sends", trackById).toArray();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].toObject().value("sendIndex").toInt(-1), 0) << "B renumbered to 0";
    EXPECT_EQ(rows[0].toObject().value("sendID").toInt(0), idB) << "…keeping its identity";

    // Shape the survivor by ID ONLY — no `sendIndex` in the object at all.
    const QJsonObject byId{ { "trackId", 0 }, { "sendID", idB }, { "level", 0.125 } };
    EXPECT_FALSE(mcpIsError("set_track_send_level", byId))
        << mcpText("set_track_send_level", byId).toStdString();
    EXPECT_FLOAT_EQ(engine->getReadModel().getTrackSends(0)[0].level, 0.125f);
    EXPECT_FALSE(rpc("project.setTrackSendMode",
                     QJsonObject{ { "trackId", 0 }, { "sendID", idB }, { "isPreFader", true } })
                     .isError);
    engine->drainPendingRoutingRebuild();
    EXPECT_TRUE(engine->getReadModel().getTrackSends(0)[0].isPreFader);
    EXPECT_FALSE(mcpIsError("set_track_send_bypassed",
                            QJsonObject{ { "trackId", 0 }, { "sendID", idB },
                                         { "bypassed", true } }));
    EXPECT_TRUE(engine->getReadModel().getTrackSends(0)[0].bypassed);

    // The REMOVED send's id is unknown on both surfaces (never silently the send
    // that took its slot), and remove_send can be driven by id too.
    const QJsonObject gone{ { "trackId", 0 }, { "sendID", idA }, { "level", 0.5 } };
    expectSameFailure("set_track_send_level", "project.setTrackSendLevel", gone);
    EXPECT_EQ(mcpText("set_track_send_level", gone).toStdString(),
              "unknown sendID " + std::to_string(idA));
    const QJsonValue removedById = mcpValue("remove_send",
                                            QJsonObject{ { "trackId", 0 }, { "sendID", idB } });
    ASSERT_TRUE(removedById.isObject()) << mcpText("remove_send",
                                                   QJsonObject{ { "trackId", 0 }, { "sendID", idB } })
                                             .toStdString();
    EXPECT_EQ(removedById.toObject().value("removed").toInt(-1), 0)
        << "the removed send was the one the id named";
    EXPECT_TRUE(engine->getReadModel().getTrackSends(0).empty());

    // The stable id of the TRACK works in the same object as the send's.
    ASSERT_GE(cmds.createSend(0, 1, 0.5f, false).sendIndex, 0);
    engine->drainPendingRoutingRebuild();
    const int sendID = mcpValue("get_track_sends", trackById).toArray()[0].toObject()
                           .value("sendID").toInt(0);
    const int trackID = static_cast<int>(
        engine->getProjectModel().getTrackListTree().getChild(0).getProperty(IDs::trackID, 0));
    const QJsonObject bothIds{ { "trackID", trackID }, { "sendID", sendID },
                               { "isPreFader", false } };
    EXPECT_FALSE(mcpIsError("set_track_send_mode", bothIds))
        << mcpText("set_track_send_mode", bothIds).toStdString();
    EXPECT_FALSE(rpc("project.setTrackSendMode", bothIds).isError)
        << "trackID + sendID together, one object, both surfaces";
}

} // namespace
