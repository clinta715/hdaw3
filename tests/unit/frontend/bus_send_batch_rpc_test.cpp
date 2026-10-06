// MCP <-> RPC parity for the batched bus/send CREATORS — the deferred follow-up
// of docs/plans/2026-10-05-param-batch-and-bugfixes.md §"Not done":
//   add_buses  {buses:[{busType, name?, fxType?, busTarget?}]}  <-> project.addBuses
//   add_sends  {sends:[{trackId?|trackID?, busTarget, level?, isPreFader?}]}
//                                                               <-> project.addSends
//
// Both surfaces call ONE ProjectCommands entry point (createBuses / createSends)
// AND the SAME strict request parser + payload shaper
// (src/common/BusSendBatchJson.h), so the strongest assertions available are the
// direct ones:
//   * the same argument object on both surfaces returns the SAME payload bytes
//     — literally for an all-failing batch (every id is -1), and with the
//     freshly-allocated id array normalized for a successful one (each call
//     creates its own objects, so the ids MUST differ);
//   * each batch is ONE undo unit — a SINGLE undo removes BOTH created buses,
//     and a single undo removes BOTH created sends;
//   * the creators JOIN an already-open outer batch (begin_batch), so a bus
//     batch + a send batch inside ONE outer batch is a single undo unit that
//     removes the created bus(es) AND their sends;
//   * PARTIAL-APPLY reports a per-item error row numbered by its ORIGINAL index,
//     while the good items still land;
//   * a typo'd item key is REFUSED with the SAME bytes on both surfaces — the
//     MCP validator's own wording, reproduced by the shared parser.
//
// Harness idioms follow tests/unit/frontend/param_batch_rpc_test.cpp and
// bus_send_rpc_test.cpp (fixture engine + McpServer, mcpValue/rpcPayload/
// expectSameFailure/undoDepth helpers).

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "common/BusInfo.h"
#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <memory>
#include <string>

namespace {

class BusSendBatchRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        auto& cmds = engine->getProjectCommands();
        ASSERT_GE(cmds.addTrack("Kick"), 0);   // TRACK_LIST index 0
        ASSERT_GE(cmds.addTrack("Bass"), 0);   // TRACK_LIST index 1
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
    // A failing pair: the shared args must reach the same command and come back
    // as the same text on both surfaces.
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

    // --- Tree readers ------------------------------------------------------
    int busCount() { return engine->getProjectModel().getBusListTree().getNumChildren(); }
    juce::ValueTree busNode(int busID) {
        return HDAW::findBusNode(engine->getProjectModel().getBusListTree(), busID);
    }
    int sendCount(int trackIndex) {
        auto sendList = engine->getProjectModel().getTrackListTree()
                            .getChild(trackIndex).getChildWithName(IDs::SEND_LIST);
        return sendList.isValid() ? sendList.getNumChildren() : 0;
    }
    int undoDepth() {
        return static_cast<int>(engine->getProjectCommands().getUndoDescriptions().size());
    }

    // A creation payload carries freshly allocated ids (each call creates its own
    // objects), so only an all-failing batch is byte-comparable verbatim. Expose
    // the created ids and strip them for the structural comparison.
    static QJsonObject withoutIdArray(const QJsonObject& o, const char* key) {
        QJsonObject copy = o;
        copy[key] = QJsonArray{};
        return copy;
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

// ---------------------------------------------------------------------------
// add_buses <-> project.addBuses
// ---------------------------------------------------------------------------

// (a) an ALL-FAILING batch is byte-identical on both surfaces (every id is -1,
// so there is nothing call-specific in the payload).
TEST_F(BusSendBatchRpcTest, AddBusesAllFailPayloadMatchesOnBothSurfaces) {
    const QJsonObject args{{"buses", QJsonArray{
        QJsonObject{{"busType", "fx"}, {"name", "Orphan A"}, {"fxType", "delay"}, {"busTarget", 999}},
        QJsonObject{{"busType", "fx"}, {"name", "Orphan B"}, {"fxType", "delay"}, {"busTarget", 998}}}}};
    const int before = busCount();

    const QJsonValue viaMcp = mcpValue("add_buses", args);
    const QJsonValue viaRpc = rpcPayload("project.addBuses", args);
    EXPECT_EQ(viaRpc, viaMcp) << "one shared command + shaper: the payloads must be identical";
    ASSERT_TRUE(viaMcp.isObject());
    const QJsonObject o = viaMcp.toObject();
    EXPECT_FALSE(o.value("ok").toBool());
    EXPECT_EQ(o.value("created").toInt(), 0);
    EXPECT_EQ(o.value("failed").toInt(), 2);
    EXPECT_EQ(o.value("busIDs").toArray(), QJsonArray({-1, -1}));
    EXPECT_EQ(o.value("errors").toArray().size(), 2);
    EXPECT_EQ(busCount(), before) << "a fully rejected batch must not touch BUS_LIST";
}

// (b) a PARTIAL batch: the good bus lands (a fresh id), the bad one reports a
// row numbered by its ORIGINAL index; structural equality with the id array
// normalized (each surface allocated its own bus).
TEST_F(BusSendBatchRpcTest, AddBusesPartialApplyReportsErrorRows) {
    const QJsonObject args{{"buses", QJsonArray{
        QJsonObject{{"busType", "fx"}, {"name", "Good"}, {"fxType", "delay"}, {"busTarget", 0}},
        QJsonObject{{"busType", "fx"}, {"name", "Bad"},  {"fxType", "delay"}, {"busTarget", 999}}}}};

    const QJsonValue viaMcp = mcpValue("add_buses", args);
    const QJsonValue viaRpc = rpcPayload("project.addBuses", args);
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_EQ(withoutIdArray(viaRpc.toObject(), "busIDs"),
              withoutIdArray(viaMcp.toObject(), "busIDs"));
    const QJsonObject o = viaMcp.toObject();
    EXPECT_TRUE(o.value("ok").toBool());
    EXPECT_EQ(o.value("created").toInt(), 1);
    EXPECT_EQ(o.value("failed").toInt(), 1);
    const QJsonArray ids = o.value("busIDs").toArray();
    ASSERT_EQ(ids.size(), 2);
    EXPECT_GE(ids[0].toInt(), 0) << "the good bus landed with a real id";
    EXPECT_EQ(ids[1].toInt(), -1) << "the failed slot carries -1";
    const QJsonArray rows = o.value("errors").toArray();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].toObject().value("index").toInt(), 1)
        << "a row is numbered by its ORIGINAL index";
    EXPECT_TRUE(rows[0].toObject().value("error").toString().contains("busTarget"))
        << rows[0].toObject().value("error").toString().toStdString();
    ASSERT_TRUE(busNode(ids[0].toInt()).isValid());
    EXPECT_EQ(busNode(ids[0].toInt()).getProperty(IDs::name).toString().toStdString(), "Good");
    // The other surface's bus also landed (one code path, no drift).
    const int rpcID = viaRpc.toObject().value("busIDs").toArray()[0].toInt();
    ASSERT_TRUE(busNode(rpcID).isValid());
    EXPECT_NE(rpcID, ids[0].toInt()) << "each call allocates its own bus";
}

// (c) ONE UNDO UNIT: two buses in one add_buses call, a SINGLE undo removes both.
TEST_F(BusSendBatchRpcTest, AddBusesIsOneUndoUnit) {
    const int before = busCount();
    const int depth = undoDepth();

    const QJsonValue viaRpc = rpcPayload("project.addBuses", QJsonObject{{"buses", QJsonArray{
        QJsonObject{{"busType", "fx"}, {"name", "A"}, {"fxType", "delay"}, {"busTarget", 0}},
        QJsonObject{{"busType", "fx"}, {"name", "B"}, {"fxType", "reverb"}, {"busTarget", 0}}}}});
    EXPECT_EQ(undoDepth(), depth + 1) << "the batch must be exactly one transaction";
    ASSERT_TRUE(viaRpc.isObject());
    EXPECT_EQ(viaRpc.toObject().value("created").toInt(), 2);
    EXPECT_EQ(busCount(), before + 2);
    const int a = viaRpc.toObject().value("busIDs").toArray()[0].toInt();
    const int b = viaRpc.toObject().value("busIDs").toArray()[1].toInt();
    ASSERT_TRUE(busNode(a).isValid());
    ASSERT_TRUE(busNode(b).isValid());

    engine->getProjectCommands().undo();   // ONE undo
    EXPECT_EQ(busCount(), before) << "one undo must remove BOTH buses";
    EXPECT_FALSE(busNode(a).isValid());
    EXPECT_FALSE(busNode(b).isValid());
}

// (d) a typo'd item key is refused with the SAME bytes on both surfaces.
TEST_F(BusSendBatchRpcTest, AddBusesTypoKeyRefusedIdentically) {
    const QJsonObject args{{"buses", QJsonArray{
        QJsonObject{{"busType", "fx"}, {"fxType", "delay"}, {"bogus", 1}}}}};
    expectSameFailure("add_buses", "project.addBuses", args);
    EXPECT_EQ(mcpText("add_buses", args),
              QString("invalid params: buses[0].bogus: unknown property"));
}

// (d') structural refusals share the bytes: an empty batch, a non-array, a
// missing required busType, and a non-integral busTarget.
TEST_F(BusSendBatchRpcTest, AddBusesStructuralRefusalsShareBytes) {
    const QJsonObject empty{{"buses", QJsonArray{}}};
    expectSameFailure("add_buses", "project.addBuses", empty);
    EXPECT_EQ(mcpText("add_buses", empty), QString("buses must not be empty"));

    const QJsonObject notArray{{"buses", "x"}};
    expectSameFailure("add_buses", "project.addBuses", notArray);
    EXPECT_EQ(mcpText("add_buses", notArray),
              QString("invalid params: buses: expected array"));

    const QJsonObject noType{{"buses", QJsonArray{QJsonObject{{"fxType", "delay"}}}}};
    expectSameFailure("add_buses", "project.addBuses", noType);
    EXPECT_EQ(mcpText("add_buses", noType),
              QString("invalid params: buses[0].busType: missing required property 'busType'"));

    const QJsonObject fracTarget{{"buses", QJsonArray{
        QJsonObject{{"busType", "fx"}, {"fxType", "delay"}, {"busTarget", 1.5}}}}};
    expectSameFailure("add_buses", "project.addBuses", fracTarget);
    EXPECT_EQ(mcpText("add_buses", fracTarget),
              QString("invalid params: buses[0].busTarget: expected integer"));
}

// ---------------------------------------------------------------------------
// add_sends <-> project.addSends
// ---------------------------------------------------------------------------

// (a) an ALL-FAILING batch is byte-identical on both surfaces.
TEST_F(BusSendBatchRpcTest, AddSendsAllFailPayloadMatchesOnBothSurfaces) {
    const QJsonObject args{{"sends", QJsonArray{
        QJsonObject{{"trackId", 0}, {"busTarget", 999}},
        QJsonObject{{"trackId", 1}, {"busTarget", 998}}}}};
    const int before0 = sendCount(0), before1 = sendCount(1);

    const QJsonValue viaMcp = mcpValue("add_sends", args);
    const QJsonValue viaRpc = rpcPayload("project.addSends", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    const QJsonObject o = viaMcp.toObject();
    EXPECT_FALSE(o.value("ok").toBool());
    EXPECT_EQ(o.value("created").toInt(), 0);
    EXPECT_EQ(o.value("failed").toInt(), 2);
    EXPECT_EQ(o.value("sendIndexes").toArray(), QJsonArray({-1, -1}));
    EXPECT_EQ(sendCount(0), before0);
    EXPECT_EQ(sendCount(1), before1);
}

// (b) PARTIAL: the good send lands, an out-of-range track reports a per-item row
// (a COMMAND-layer error, so the parser let it through).
TEST_F(BusSendBatchRpcTest, AddSendsPartialApplyReportsErrorRows) {
    auto& cmds = engine->getProjectCommands();
    const auto bus = cmds.createBus("fx", "Ret", "delay", 0);
    ASSERT_TRUE(bus.ok) << bus.error;
    engine->drainPendingRoutingRebuild();
    const int busID = bus.busID;

    const QJsonObject args{{"sends", QJsonArray{
        QJsonObject{{"trackId", 0}, {"busTarget", busID}, {"level", 0.5}},
        QJsonObject{{"trackId", 99}, {"busTarget", busID}}}}};

    const QJsonValue viaMcp = mcpValue("add_sends", args);
    const QJsonValue viaRpc = rpcPayload("project.addSends", args);
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_EQ(withoutIdArray(viaRpc.toObject(), "sendIndexes"),
              withoutIdArray(viaMcp.toObject(), "sendIndexes"));
    const QJsonObject o = viaMcp.toObject();
    EXPECT_TRUE(o.value("ok").toBool());
    EXPECT_EQ(o.value("created").toInt(), 1);
    EXPECT_EQ(o.value("failed").toInt(), 1);
    const QJsonArray ids = o.value("sendIndexes").toArray();
    ASSERT_EQ(ids.size(), 2);
    EXPECT_GE(ids[0].toInt(), 0);
    EXPECT_EQ(ids[1].toInt(), -1);
    const QJsonArray rows = o.value("errors").toArray();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].toObject().value("index").toInt(), 1);
    EXPECT_TRUE(rows[0].toObject().value("error").toString().contains("out of range"))
        << rows[0].toObject().value("error").toString().toStdString();
    EXPECT_EQ(sendCount(0), 2) << "each surface landed its own good send on track 0";
}

// (c) ONE UNDO UNIT: two sends in one add_sends call, a SINGLE undo removes both.
TEST_F(BusSendBatchRpcTest, AddSendsIsOneUndoUnit) {
    auto& cmds = engine->getProjectCommands();
    const auto busA = cmds.createBus("fx", "A", "delay", 0);
    const auto busB = cmds.createBus("fx", "B", "reverb", 0);
    ASSERT_TRUE(busA.ok && busB.ok);
    engine->drainPendingRoutingRebuild();
    const int depth = undoDepth();

    const QJsonValue viaRpc = rpcPayload("project.addSends", QJsonObject{{"sends", QJsonArray{
        QJsonObject{{"trackId", 0}, {"busTarget", busA.busID}, {"level", 0.25}},
        QJsonObject{{"trackId", 1}, {"busTarget", busB.busID}, {"isPreFader", true}}}}});
    EXPECT_EQ(undoDepth(), depth + 1) << "the batch must be exactly one transaction";
    ASSERT_TRUE(viaRpc.isObject());
    EXPECT_EQ(viaRpc.toObject().value("created").toInt(), 2);
    EXPECT_EQ(sendCount(0), 1);
    EXPECT_EQ(sendCount(1), 1);

    engine->getProjectCommands().undo();   // ONE undo
    EXPECT_EQ(sendCount(0), 0) << "one undo must remove BOTH sends";
    EXPECT_EQ(sendCount(1), 0);
}

// (d) a typo'd item key is refused with the SAME bytes on both surfaces.
TEST_F(BusSendBatchRpcTest, AddSendsTypoKeyRefusedIdentically) {
    const QJsonObject args{{"sends", QJsonArray{
        QJsonObject{{"trackId", 0}, {"busTarget", 0}, {"bogus", 1}}}}};
    expectSameFailure("add_sends", "project.addSends", args);
    EXPECT_EQ(mcpText("add_sends", args),
              QString("invalid params: sends[0].bogus: unknown property"));
}

// (d') structural refusals share the bytes: empty, non-array, missing busTarget,
// a missing track argument (the send parser's shared "trackId required"), and a
// non-integral busTarget.
TEST_F(BusSendBatchRpcTest, AddSendsStructuralRefusalsShareBytes) {
    const QJsonObject empty{{"sends", QJsonArray{}}};
    expectSameFailure("add_sends", "project.addSends", empty);
    EXPECT_EQ(mcpText("add_sends", empty), QString("sends must not be empty"));

    const QJsonObject notArray{{"sends", "x"}};
    expectSameFailure("add_sends", "project.addSends", notArray);
    EXPECT_EQ(mcpText("add_sends", notArray),
              QString("invalid params: sends: expected array"));

    const QJsonObject noBus{{"sends", QJsonArray{QJsonObject{{"trackId", 0}}}}};
    expectSameFailure("add_sends", "project.addSends", noBus);
    EXPECT_EQ(mcpText("add_sends", noBus),
              QString("invalid params: sends[0].busTarget: missing required property 'busTarget'"));

    const QJsonObject noTrack{{"sends", QJsonArray{QJsonObject{{"busTarget", 0}}}}};
    expectSameFailure("add_sends", "project.addSends", noTrack);
    EXPECT_EQ(mcpText("add_sends", noTrack), QString("trackId required"));

    // A {} item: the missing-required-property pass fires BEFORE the track-ref
    // resolution (the validator's order), so both surfaces answer busTarget —
    // never the resolver's "trackId required".
    const QJsonObject nothing{{"sends", QJsonArray{QJsonObject{}}}};
    expectSameFailure("add_sends", "project.addSends", nothing);
    EXPECT_EQ(mcpText("add_sends", nothing),
              QString("invalid params: sends[0].busTarget: missing required property 'busTarget'"));

    const QJsonObject fracBus{{"sends", QJsonArray{
        QJsonObject{{"trackId", 0}, {"busTarget", 1.5}}}}};
    expectSameFailure("add_sends", "project.addSends", fracBus);
    EXPECT_EQ(mcpText("add_sends", fracBus),
              QString("invalid params: sends[0].busTarget: expected integer"));
}

// ---------------------------------------------------------------------------
// The JOIN branch: an already-open outer batch swallows both creators, so ONE
// undo removes the created bus(es) AND their sends (the batch's contract).
// ---------------------------------------------------------------------------

TEST_F(BusSendBatchRpcTest, CreatorsJoinAnOuterBatchIntoOneUndoUnit) {
    const int busBefore = busCount();
    const int depth = undoDepth();

    // The outer batch is opened over RPC (the process's own client).
    ASSERT_FALSE(rpc("project.beginBatch", QJsonObject{{"name", "outer"}}).isError);

    // Each creator must JOIN (not seal) the open batch — exercised through BOTH
    // surfaces to prove the join is command-layer, not surface-layer.
    const QJsonValue buses = mcpValue("add_buses", QJsonObject{{"buses", QJsonArray{
        QJsonObject{{"busType", "fx"}, {"name", "Feed A"}, {"fxType", "delay"}, {"busTarget", 0}},
        QJsonObject{{"busType", "fx"}, {"name", "Feed B"}, {"fxType", "reverb"}, {"busTarget", 0}}}}});
    ASSERT_TRUE(buses.isObject());
    const QJsonArray busIDs = buses.toObject().value("busIDs").toArray();
    ASSERT_EQ(busIDs.size(), 2);

    const QJsonValue sends = rpcPayload("project.addSends", QJsonObject{{"sends", QJsonArray{
        QJsonObject{{"trackId", 0}, {"busTarget", busIDs[0].toInt()}},
        QJsonObject{{"trackId", 1}, {"busTarget", busIDs[1].toInt()}}}}});
    ASSERT_TRUE(sends.isObject());
    EXPECT_EQ(sends.toObject().value("created").toInt(), 2);

    ASSERT_FALSE(rpc("project.endBatch", QJsonObject{}).isError);

    EXPECT_EQ(undoDepth(), depth + 1)
        << "the joined creators must collapse into the outer batch's ONE unit";
    EXPECT_EQ(busCount(), busBefore + 2);
    EXPECT_EQ(sendCount(0), 1);
    EXPECT_EQ(sendCount(1), 1);

    engine->getProjectCommands().undo();   // ONE undo
    EXPECT_EQ(busCount(), busBefore) << "one undo removes BOTH created buses";
    EXPECT_EQ(sendCount(0), 0) << "and their sends";
    EXPECT_EQ(sendCount(1), 0);
}

} // namespace
