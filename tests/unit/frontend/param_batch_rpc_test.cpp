// MCP <-> RPC parity for slice 2 of
// docs/plans/2026-10-05-param-batch-and-bugfixes.md: the three BATCHED param
// writers over the LANDED engine command layer (no engine change here) —
//   set_fx_params      {writes:[{trackId?,slotIndex,paramIndex?|paramName?|intent?,value,mode?}], mode?}
//                      <-> project.setFxParams
//   set_bus_fx_params  {writes:[{busID,paramIndex,value}]}   <-> project.setBusFxParams
//   set_lfo_params     {writes:[{trackId?,lfoIndex,paramName,value}]} <-> project.setLfoParams
//
// Both surfaces call ONE ProjectCommands entry point (setFxParams /
// setBusFxParams / setLfoParams) AND the SAME strict request parser + payload
// shaper (src/common/FxParamBatchJson.h), so the strongest assertions available
// are the direct ones:
//   * the same argument object on both surfaces returns the SAME payload
//     (AGENTS.md: argument names are part of the contract);
//   * the batch is ONE undo unit — a SINGLE undo restores EVERY value (a loop of
//     single-item calls could not pass this with one undo);
//   * PARTIAL-APPLY reports a per-write error row numbered by its ORIGINAL write
//     index, while the good writes still land (deliberately NOT setNotesGain's
//     validate-then-apply);
//   * a typo'd write key is REFUSED with the SAME bytes on both surfaces — the
//     MCP validator's own wording, which src/common/FxParamBatchJson.h
//     reproduces for the route (the twin tests compare them byte-for-byte).
//
// Harness idioms follow tests/unit/frontend/batch_edit_rpc_test.cpp (the fixture
// engine + McpServer, mcpValue/rpcPayload/expectSameFailure/undoDepth helpers).

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include <juce_core/juce_core.h>

#include "common/BusInfo.h"
#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <memory>
#include <string>
#include <vector>

namespace {

class ParamBatchRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(temp_.isValid());
        ASSERT_TRUE(writeMaps());
        qputenv("HDAW_DEVICE_MAP_DIR", temp_.path().toUtf8());
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        auto& cmds = engine->getProjectCommands();
        ASSERT_GE(cmds.addTrack("Kick"), 0);   // TRACK_LIST index 0
        ASSERT_GE(cmds.addTrack("Bass"), 0);   // TRACK_LIST index 1
        engine->drainPendingRoutingRebuild();
        // An internal eq slot on track 0 (defs: 0=Frequency Hz, 1=Q, 2=Gain dB).
        cmds.addFxSlot(0, "eq", 0, "");
        engine->drainPendingRoutingRebuild();
        // One LFO on track 0 (defaults: depth 0.3, targetParamID 1, waveform 0).
        cmds.addLfo(0);
        engine->drainPendingRoutingRebuild();
        // One fx bus (reverb: defs 0=Room Size, 2=Wet Level).
        const auto bus = cmds.createBus("fx", "Reverb Ret", "reverb", 0);
        ASSERT_TRUE(bus.ok) << bus.error;
        busID = bus.busID;
        engine->drainPendingRoutingRebuild();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    void TearDown() override {
        qunsetenv("HDAW_DEVICE_MAP_DIR");
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
    double slotParam(int trackIndex, int slotIndex, int paramIndex) {
        auto slot = engine->getProjectModel().getTrackListTree()
                        .getChild(trackIndex).getChildWithName(IDs::FX_CHAIN)
                        .getChild(slotIndex);
        return static_cast<double>(slot.getProperty(
            juce::Identifier(("param_" + std::to_string(paramIndex)).c_str()), -999.0));
    }
    juce::ValueTree busNode() {
        return HDAW::findBusNode(engine->getProjectModel().getBusListTree(), busID);
    }
    double busParam(int paramIndex) {
        return static_cast<double>(busNode().getProperty(
            juce::Identifier(("param_" + std::to_string(paramIndex)).c_str()), -999.0));
    }
    juce::ValueTree lfoTree(int trackIndex, int lfoIndex) {
        auto modList = engine->getProjectModel().getTrackListTree()
                           .getChild(trackIndex).getChildWithName(IDs::MODULATION_LIST);
        return modList.isValid() ? modList.getChild(lfoIndex) : juce::ValueTree();
    }
    int undoDepth() {
        return static_cast<int>(engine->getProjectCommands().getUndoDescriptions().size());
    }

    // The FX-slot tree node (the source of truth the single tools write).
    juce::ValueTree slotNode(int trackIndex, int slotIndex) {
        return engine->getProjectModel().getTrackListTree()
            .getChild(trackIndex).getChildWithName(IDs::FX_CHAIN).getChild(slotIndex);
    }
    void appendSlot(const char* type) {
        engine->getProjectCommands().addFxSlot(0, std::string(type), -1, "");
        engine->drainPendingRoutingRebuild();
    }

    // A deterministic Device Parameter Map corpus (the slice 3 intent cases),
    // written to a temp dir and pointed at by HDAW_DEVICE_MAP_DIR — the
    // fixture-corpus pattern of tests/integration/mcp/internal_fx_intent_test.cpp
    // so intent resolution is exercised WITHOUT the real generated corpus.
    // filter-sweep is declared by TWO eq params on purpose (the ambiguity
    // refusal); eq-shape by exactly one (the success).
    static bool writeFile(const QString& path, const QByteArray& bytes) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return f.write(bytes) == bytes.size();
    }
    static QJsonObject eqEntry(const char* name, int index, const QJsonArray& intents) {
        return QJsonObject{ { "name", name }, { "category", "eq" }, { "tier", "movement" },
                            { "intents", intents }, { "stages", QJsonArray{ "fx-automation-engineer" } },
                            { "index", index }, { "offset", QJsonValue::Null },
                            { "trapReason", QJsonValue::Null }, { "note", QJsonValue::Null } };
    }
    bool writeMaps() {
        const QJsonArray params{
            eqEntry("Frequency", 0, QJsonArray{ "filter-sweep" }),
            eqEntry("Q",         1, QJsonArray{ "eq-shape" }),
            eqEntry("Gain",      2, QJsonArray{ "filter-sweep" }) };
        return writeFile(temp_.filePath("eq.params.json"),
                         QJsonDocument(QJsonObject{ { "schema", "hdaw.device.param.map.v1" },
                                                    { "engine", "eq" },
                                                    { "params", params } })
                             .toJson(QJsonDocument::Indented));
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    int busID = -1;
    QTemporaryDir temp_;
};

// ---------------------------------------------------------------------------
// set_fx_params <-> project.setFxParams
// ---------------------------------------------------------------------------

// (a) identical payload on both surfaces for the same args, in REAL units.
TEST_F(ParamBatchRpcTest, SetFxParamsPayloadMatchesOnBothSurfaces) {
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 440.0}},
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 1}, {"value", 3.5}},
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 2}, {"value", -8.0}}}}};

    const QJsonValue viaMcp = mcpValue("set_fx_params", args);
    const QJsonValue viaRpc = rpcPayload("project.setFxParams", args);
    EXPECT_EQ(viaRpc, viaMcp) << "one shared command, so the payloads must be identical";
    ASSERT_TRUE(viaMcp.isObject());
    const QJsonObject o = viaMcp.toObject();
    EXPECT_TRUE(o.value("ok").toBool());
    EXPECT_EQ(o.value("written").toInt(), 3);
    EXPECT_EQ(o.value("failed").toInt(), 0);
    EXPECT_TRUE(o.value("errors").toArray().isEmpty());

    EXPECT_FLOAT_EQ(slotParam(0, 0, 0), 440.0);
    EXPECT_FLOAT_EQ(slotParam(0, 0, 1), 3.5);
    EXPECT_FLOAT_EQ(slotParam(0, 0, 2), -8.0);
}

// (a') `mode:"normalized"` denormalizes through the def (eq Gain dB max 24).
TEST_F(ParamBatchRpcTest, SetFxParamsNormalizedModeMatchesOnBothSurfaces) {
    const QJsonObject args{{"mode", "normalized"}, {"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 2}, {"value", 1.0}}}}};
    const QJsonValue viaMcp = mcpValue("set_fx_params", args);
    const QJsonValue viaRpc = rpcPayload("project.setFxParams", args);
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaMcp.toObject().value("written").toInt(), 1);
    EXPECT_FLOAT_EQ(slotParam(0, 0, 2), 24.0) << "normalized 1.0 -> the def max";
}

// (b) ONE UNDO UNIT, proven directly: three params change, then a SINGLE undo
// restores EVERY value — a loop of single-item calls could not.
TEST_F(ParamBatchRpcTest, SetFxParamsIsOneUndoUnit) {
    const double before0 = slotParam(0, 0, 0);
    const double before1 = slotParam(0, 0, 1);
    const double before2 = slotParam(0, 0, 2);
    const int depth = undoDepth();

    rpcPayload("project.setFxParams", QJsonObject{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 900.0}},
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 1}, {"value", 0.9}},
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 2}, {"value", 12.0}}}}});
    EXPECT_EQ(undoDepth(), depth + 1) << "the batch must be exactly one transaction";
    EXPECT_FLOAT_EQ(slotParam(0, 0, 0), 900.0);
    EXPECT_FLOAT_EQ(slotParam(0, 0, 1), 0.9);
    EXPECT_FLOAT_EQ(slotParam(0, 0, 2), 12.0);

    engine->getProjectCommands().undo();   // ONE undo
    EXPECT_FLOAT_EQ(slotParam(0, 0, 0), before0);
    EXPECT_FLOAT_EQ(slotParam(0, 0, 1), before1);
    EXPECT_FLOAT_EQ(slotParam(0, 0, 2), before2);
}

// (c) PARTIAL-APPLY: an unknown paramName in the middle is a per-write error row
// (numbered by its ORIGINAL index) while the good writes land; the payload is
// byte-identical on both surfaces.
TEST_F(ParamBatchRpcTest, SetFxParamsPartialApplyReportsErrorRows) {
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 800.0}},
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramName", "NoSuchParam"}, {"value", 1.0}},
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 2}, {"value", 5.0}}}}};

    const QJsonValue viaMcp = mcpValue("set_fx_params", args);
    const QJsonValue viaRpc = rpcPayload("project.setFxParams", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    const QJsonObject o = viaMcp.toObject();
    EXPECT_TRUE(o.value("ok").toBool());
    EXPECT_EQ(o.value("written").toInt(), 2) << "the two valid writes must land";
    EXPECT_EQ(o.value("failed").toInt(), 1);
    const QJsonArray rows = o.value("errors").toArray();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].toObject().value("index").toInt(), 1)
        << "a row is numbered by its ORIGINAL write index";
    EXPECT_TRUE(rows[0].toObject().value("error").toString().contains("unknown paramName"))
        << rows[0].toObject().value("error").toString().toStdString();
    EXPECT_FLOAT_EQ(slotParam(0, 0, 0), 800.0);
    EXPECT_FLOAT_EQ(slotParam(0, 0, 2), 5.0);
}

// (c') Every write bad => ok:false with the failures list, still a PAYLOAD (not
// a JSON-RPC error) so both surfaces agree byte-for-byte even here.
TEST_F(ParamBatchRpcTest, SetFxParamsAllBadIsAPayloadNotAnError) {
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"slotIndex", 99}, {"paramIndex", 0}, {"value", 1.0}},
        QJsonObject{{"trackId", 42}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 1.0}}}}};
    const QJsonValue viaMcp = mcpValue("set_fx_params", args);
    const QJsonValue viaRpc = rpcPayload("project.setFxParams", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_FALSE(viaMcp.toObject().value("ok").toBool());
    EXPECT_EQ(viaMcp.toObject().value("written").toInt(), 0);
    EXPECT_EQ(viaMcp.toObject().value("failed").toInt(), 2);
    EXPECT_EQ(viaMcp.toObject().value("errors").toArray().size(), 2);
}

// (d) a TYPO'd write key is refused with the SAME bytes on both surfaces — the
// MCP validator's own wording, reproduced by the shared parser.
TEST_F(ParamBatchRpcTest, SetFxParamsTypoKeyRefusedIdentically) {
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0},
                    {"value", 1.0}, {"bogus", 1}}}}};
    expectSameFailure("set_fx_params", "project.setFxParams", args);
    EXPECT_EQ(mcpText("set_fx_params", args),
              QString("invalid params: writes[0].bogus: unknown property"));
}

// (d') structural refusals share the bytes too: an empty batch and a non-array
// `writes`.
TEST_F(ParamBatchRpcTest, SetFxParamsEmptyAndNonArrayRefusedIdentically) {
    const QJsonObject empty{{"writes", QJsonArray{}}};
    expectSameFailure("set_fx_params", "project.setFxParams", empty);
    EXPECT_EQ(mcpText("set_fx_params", empty), QString("writes must not be empty"));

    const QJsonObject notArray{{"writes", "x"}};
    expectSameFailure("set_fx_params", "project.setFxParams", notArray);
    EXPECT_EQ(mcpText("set_fx_params", notArray), QString("invalid params: writes: expected array"));
}

// (d'') an unknown `mode` value is refused with the validator's exact enum bytes
// on BOTH surfaces — top-level AND per-write.
TEST_F(ParamBatchRpcTest, SetFxParamsUnknownModeRefusedIdentically) {
    const QJsonObject topLevel{{"mode", "bogus"}, {"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 1.0}}}}};
    expectSameFailure("set_fx_params", "project.setFxParams", topLevel);
    EXPECT_EQ(mcpText("set_fx_params", topLevel),
              QString("invalid params: mode: value \"bogus\" not in enum "
                      "(allowed: \"real\", \"normalized\")"));

    const QJsonObject perWrite{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0},
                    {"value", 1.0}, {"mode", "bogus"}}}}};
    expectSameFailure("set_fx_params", "project.setFxParams", perWrite);
    EXPECT_EQ(mcpText("set_fx_params", perWrite),
              QString("invalid params: writes[0].mode: value \"bogus\" not in enum "
                      "(allowed: \"real\", \"normalized\")"));
}

// ---------------------------------------------------------------------------
// set_bus_fx_params <-> project.setBusFxParams
// ---------------------------------------------------------------------------

TEST_F(ParamBatchRpcTest, SetBusFxParamsPayloadMatchesOnBothSurfaces) {
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"busID", busID}, {"paramIndex", 0}, {"value", 0.8}},
        QJsonObject{{"busID", busID}, {"paramIndex", 2}, {"value", 0.45}}}}};
    const QJsonValue viaMcp = mcpValue("set_bus_fx_params", args);
    const QJsonValue viaRpc = rpcPayload("project.setBusFxParams", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_EQ(viaMcp.toObject().value("written").toInt(), 2);
    EXPECT_FLOAT_EQ(busParam(0), 0.8);
    EXPECT_FLOAT_EQ(busParam(2), 0.45);
}

TEST_F(ParamBatchRpcTest, SetBusFxParamsIsOneUndoUnit) {
    const double before0 = busParam(0);
    const double before2 = busParam(2);
    const int depth = undoDepth();

    rpcPayload("project.setBusFxParams", QJsonObject{{"writes", QJsonArray{
        QJsonObject{{"busID", busID}, {"paramIndex", 0}, {"value", 0.6}},
        QJsonObject{{"busID", busID}, {"paramIndex", 2}, {"value", 0.3}}}}});
    EXPECT_EQ(undoDepth(), depth + 1);
    EXPECT_FLOAT_EQ(busParam(0), 0.6);
    EXPECT_FLOAT_EQ(busParam(2), 0.3);

    engine->getProjectCommands().undo();   // ONE undo
    EXPECT_FLOAT_EQ(busParam(0), before0);
    EXPECT_FLOAT_EQ(busParam(2), before2);
}

TEST_F(ParamBatchRpcTest, SetBusFxParamsPartialApplyReportsErrorRows) {
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"busID", 999}, {"paramIndex", 0}, {"value", 0.5}},    // no such bus
        QJsonObject{{"busID", busID}, {"paramIndex", 0}, {"value", 0.7}}}}};
    const QJsonValue viaMcp = mcpValue("set_bus_fx_params", args);
    const QJsonValue viaRpc = rpcPayload("project.setBusFxParams", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_EQ(viaMcp.toObject().value("written").toInt(), 1);
    EXPECT_EQ(viaMcp.toObject().value("failed").toInt(), 1);
    const QJsonArray rows = viaMcp.toObject().value("errors").toArray();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].toObject().value("index").toInt(), 0);
    EXPECT_FLOAT_EQ(busParam(0), 0.7);
}

TEST_F(ParamBatchRpcTest, SetBusFxParamsTypoKeyRefusedIdentically) {
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"busID", busID}, {"paramIndex", 0}, {"value", 0.5}, {"trackId", 0}}}}};
    expectSameFailure("set_bus_fx_params", "project.setBusFxParams", args);
    EXPECT_EQ(mcpText("set_bus_fx_params", args),
              QString("invalid params: writes[0].trackId: unknown property"));
}

TEST_F(ParamBatchRpcTest, SetBusFxParamsEmptyRefusedIdentically) {
    const QJsonObject empty{{"writes", QJsonArray{}}};
    expectSameFailure("set_bus_fx_params", "project.setBusFxParams", empty);
    EXPECT_EQ(mcpText("set_bus_fx_params", empty), QString("writes must not be empty"));
}

// ---------------------------------------------------------------------------
// set_lfo_params <-> project.setLfoParams
// ---------------------------------------------------------------------------

TEST_F(ParamBatchRpcTest, SetLfoParamsPayloadMatchesOnBothSurfaces) {
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"lfoIndex", 0}, {"paramName", "depth"}, {"value", 0.9}},
        QJsonObject{{"trackId", 0}, {"lfoIndex", 0}, {"paramName", "targetParamID"}, {"value", 2}}}}};
    const QJsonValue viaMcp = mcpValue("set_lfo_params", args);
    const QJsonValue viaRpc = rpcPayload("project.setLfoParams", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_EQ(viaMcp.toObject().value("written").toInt(), 2);
    auto lfo = lfoTree(0, 0);
    ASSERT_TRUE(lfo.isValid());
    EXPECT_DOUBLE_EQ(static_cast<double>(lfo.getProperty(IDs::depth, -999.0)), 0.9);
    EXPECT_EQ(static_cast<int>(lfo.getProperty(IDs::targetParamID, -1)), 2);
}

TEST_F(ParamBatchRpcTest, SetLfoParamsIsOneUndoUnit) {
    auto lfo = lfoTree(0, 0);
    ASSERT_TRUE(lfo.isValid());
    const double beforeDepth = static_cast<double>(lfo.getProperty(IDs::depth, -999.0));
    const int beforeTarget = static_cast<int>(lfo.getProperty(IDs::targetParamID, -1));
    const int depth = undoDepth();

    rpcPayload("project.setLfoParams", QJsonObject{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"lfoIndex", 0}, {"paramName", "depth"}, {"value", 0.75}},
        QJsonObject{{"trackId", 0}, {"lfoIndex", 0}, {"paramName", "targetParamID"}, {"value", 3}}}}});
    EXPECT_EQ(undoDepth(), depth + 1);
    EXPECT_DOUBLE_EQ(static_cast<double>(lfo.getProperty(IDs::depth, -999.0)), 0.75);
    EXPECT_EQ(static_cast<int>(lfo.getProperty(IDs::targetParamID, -1)), 3);

    engine->getProjectCommands().undo();   // ONE undo
    EXPECT_DOUBLE_EQ(static_cast<double>(lfo.getProperty(IDs::depth, -999.0)), beforeDepth);
    EXPECT_EQ(static_cast<int>(lfo.getProperty(IDs::targetParamID, -1)), beforeTarget);
}

TEST_F(ParamBatchRpcTest, SetLfoParamsPartialApplyReportsErrorRows) {
    // An unknown param NAME is REFUSED per write (the single setLfoParam silently
    // no-ops on one — lesson 38), while the good write lands.
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"lfoIndex", 0}, {"paramName", "depth"}, {"value", 0.55}},
        QJsonObject{{"trackId", 0}, {"lfoIndex", 0}, {"paramName", "bogusParam"}, {"value", 1.0}}}}};
    const QJsonValue viaMcp = mcpValue("set_lfo_params", args);
    const QJsonValue viaRpc = rpcPayload("project.setLfoParams", args);
    EXPECT_EQ(viaRpc, viaMcp);
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_EQ(viaMcp.toObject().value("written").toInt(), 1);
    EXPECT_EQ(viaMcp.toObject().value("failed").toInt(), 1);
    const QJsonArray rows = viaMcp.toObject().value("errors").toArray();
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows[0].toObject().value("index").toInt(), 1);
    EXPECT_TRUE(rows[0].toObject().value("error").toString().contains("unknown param"))
        << rows[0].toObject().value("error").toString().toStdString();
    EXPECT_DOUBLE_EQ(static_cast<double>(lfoTree(0, 0).getProperty(IDs::depth, -999.0)), 0.55);
}

TEST_F(ParamBatchRpcTest, SetLfoParamsTypoKeyRefusedIdentically) {
    const QJsonObject args{{"writes", QJsonArray{
        QJsonObject{{"trackId", 0}, {"lfoIndex", 0}, {"paramName", "depth"},
                    {"value", 0.5}, {"param", "depth"}}}}};
    expectSameFailure("set_lfo_params", "project.setLfoParams", args);
    EXPECT_EQ(mcpText("set_lfo_params", args),
              QString("invalid params: writes[0].param: unknown property"));
}

TEST_F(ParamBatchRpcTest, SetLfoParamsEmptyRefusedIdentically) {
    const QJsonObject empty{{"writes", QJsonArray{}}};
    expectSameFailure("set_lfo_params", "project.setLfoParams", empty);
    EXPECT_EQ(mcpText("set_lfo_params", empty), QString("writes must not be empty"));
}

// ---------------------------------------------------------------------------
// Slice 3 — the SINGLE-write tools' responses are byte-identical after being
// routed through the shared resolver + ONE writer. Every expectation below is
// the text the pre-slice handlers produced (pinned literally), so a moved clamp,
// a reordered precedence, or a swapped message family fails here.
// ---------------------------------------------------------------------------

// set_fx_param, valid internal NORMALIZED write -> bare "ok", no clamp report.
TEST_F(ParamBatchRpcTest, SetFxParamInternalOkIsBareOk) {
    EXPECT_FALSE(mcpIsError("set_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 2}, {"value", 1.0}}));
    EXPECT_EQ(mcpText("set_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 2}, {"value", 1.0}}), QString("ok"));
    EXPECT_FLOAT_EQ(slotParam(0, 0, 2), 24.0) << "normalized 1.0 -> the def max";
}

// set_internal_fx_param, valid internal REAL write -> bare "ok".
TEST_F(ParamBatchRpcTest, SetInternalFxParamRealOkIsBareOk) {
    EXPECT_FALSE(mcpIsError("set_internal_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 440.0}}));
    EXPECT_EQ(mcpText("set_internal_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 440.0}}), QString("ok"));
    EXPECT_FLOAT_EQ(slotParam(0, 0, 0), 440.0);
}

// set_internal_fx_param is the ONLY surface that reports a lesson-23 clamp.
TEST_F(ParamBatchRpcTest, SetInternalFxParamReportsClamp) {
    EXPECT_EQ(mcpText("set_internal_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 1}, {"value", 99.0}}),
        QString("ok (paramIndex 1 clamped: 99 -> 10)"));
    EXPECT_FLOAT_EQ(slotParam(0, 0, 1), 10.0);   // Q max
}

// set_internal_fx_param on a PLUGIN slot keeps its own "not an internal FX".
TEST_F(ParamBatchRpcTest, SetInternalFxParamPluginSlotRefused) {
    engine->getProjectCommands().addFxSlot(0, "plugin", -1, "fixture.test");
    engine->drainPendingRoutingRebuild();
    EXPECT_TRUE(mcpIsError("set_internal_fx_param",
        {{"trackId", 0}, {"slotIndex", 1}, {"paramIndex", 0}, {"value", 1.0}}));
    EXPECT_EQ(mcpText("set_internal_fx_param",
        {{"trackId", 0}, {"slotIndex", 1}, {"paramIndex", 0}, {"value", 1.0}}),
        QString("slot is not an internal FX"));
}

// set_internal_fx_param on a "none" slot keeps the same text (writeFxParam would
// have said "slot is empty" — the surface's own message must survive).
TEST_F(ParamBatchRpcTest, SetInternalFxParamNoneSlotRefused) {
    slotNode(0, 0).setProperty(IDs::fxType, juce::String("none"), nullptr);
    engine->drainPendingRoutingRebuild();
    EXPECT_TRUE(mcpIsError("set_internal_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 1.0}}));
    EXPECT_EQ(mcpText("set_internal_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 1.0}}),
        QString("slot is not an internal FX"));
}

// set_fx_param on a "none" slot -> "slot is empty" (its own pre-check).
TEST_F(ParamBatchRpcTest, SetFxParamNoneSlotIsEmpty) {
    slotNode(0, 0).setProperty(IDs::fxType, juce::String("none"), nullptr);
    engine->drainPendingRoutingRebuild();
    EXPECT_EQ(mcpText("set_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"paramIndex", 0}, {"value", 0.5}}),
        QString("slot is empty"));
}

// The shared refusals are byte-identical on BOTH setters.
TEST_F(ParamBatchRpcTest, SingleSettersShareParamRequiredAndUnknownName) {
    const QJsonObject noAddr{{"trackId", 0}, {"slotIndex", 0}, {"value", 1.0}};
    EXPECT_EQ(mcpText("set_internal_fx_param", noAddr),
              QString("paramIndex, paramName or intent required"));
    EXPECT_EQ(mcpText("set_fx_param", noAddr),
              QString("paramIndex, paramName or intent required"));

    const QJsonObject badName{{"trackId", 0}, {"slotIndex", 0},
                              {"paramName", "NoSuchParam"}, {"value", 1.0}};
    EXPECT_EQ(mcpText("set_internal_fx_param", badName), QString("unknown paramName: NoSuchParam"));
    EXPECT_EQ(mcpText("set_fx_param", badName), QString("unknown paramName: NoSuchParam"));

    const QJsonObject badIndex{{"trackId", 0}, {"slotIndex", 0},
                               {"paramIndex", 9}, {"value", 1.0}};
    EXPECT_EQ(mcpText("set_internal_fx_param", badIndex), QString("param index out of range"));
    EXPECT_EQ(mcpText("set_fx_param", badIndex), QString("param index out of range"));
}

// Intent addressing through the shared resolver: a unique intent writes, an
// unknown / ambiguous one is refused with the resolver's own text, both setters.
TEST_F(ParamBatchRpcTest, SingleSettersIntentResolutionAndRefusals) {
    // eq-shape is declared by exactly one eq param (Q, index 1).
    EXPECT_EQ(mcpText("set_internal_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"intent", "eq-shape"}, {"value", 4.0}}), QString("ok"));
    EXPECT_FLOAT_EQ(slotParam(0, 0, 1), 4.0);
    EXPECT_EQ(mcpText("set_fx_param",
        {{"trackId", 0}, {"slotIndex", 0}, {"intent", "eq-shape"}, {"value", 1.0}}), QString("ok"));
    EXPECT_FLOAT_EQ(slotParam(0, 0, 1), 10.0);

    const QString unknown =
        "unknown intent 'no-such-intent' on eq: no param declares it "
        "(available intents: eq-shape, filter-sweep)";
    const auto unknownArgs = QJsonObject{{"trackId", 0}, {"slotIndex", 0},
                                         {"intent", "no-such-intent"}, {"value", 1.0}};
    EXPECT_EQ(mcpText("set_internal_fx_param", unknownArgs), unknown);
    EXPECT_EQ(mcpText("set_fx_param", unknownArgs), unknown);

    const QString ambiguous =
        "ambiguous intent 'filter-sweep' on eq: 2 params declare it "
        "(0 Frequency, 2 Gain); pass paramIndex or paramName";
    const auto ambiguousArgs = QJsonObject{{"trackId", 0}, {"slotIndex", 0},
                                           {"intent", "filter-sweep"}, {"value", 1.0}};
    EXPECT_EQ(mcpText("set_internal_fx_param", ambiguousArgs), ambiguous);
    EXPECT_EQ(mcpText("set_fx_param", ambiguousArgs), ambiguous);
}

// A PLUGIN slot + intent: set_fx_param reports the shared plugin refusal;
// set_internal_fx_param reports its "not an internal FX" first.
TEST_F(ParamBatchRpcTest, PluginSlotIntentAndEnabledWrites) {
    engine->getProjectCommands().addFxSlot(0, "plugin", -1, "fixture.test");
    engine->drainPendingRoutingRebuild();
    const auto intentArgs = QJsonObject{{"trackId", 0}, {"slotIndex", 1},
                                        {"intent", "eq-shape"}, {"value", 0.5}};
    EXPECT_EQ(mcpText("set_fx_param", intentArgs),
              QString("intent is not supported for a plugin FX slot: pass paramIndex or paramName"));
    EXPECT_EQ(mcpText("set_internal_fx_param", intentArgs),
              QString("slot is not an internal FX"));

    // Valid plugin write -> "ok overrides=N" (the durable ledger count).
    EXPECT_EQ(mcpText("set_fx_param",
        {{"trackId", 0}, {"slotIndex", 1}, {"paramIndex", 59}, {"value", 0.25}}),
        QString("ok overrides=1"));
    EXPECT_EQ(mcpText("set_fx_param",
        {{"trackId", 0}, {"slotIndex", 1}, {"paramIndex", 60}, {"value", 0.5}}),
        QString("ok overrides=2"));

    // Unknown name against the (empty) live cache is refused, not written blind.
    EXPECT_EQ(mcpText("set_fx_param",
        {{"trackId", 0}, {"slotIndex", 1}, {"paramName", "NoSuchPluginParam"}, {"value", 0.5}}),
        QString("unknown paramName: NoSuchPluginParam"));
}

// set_bus_fx_param / set_lfo_param stay byte-identical (their own surfaces).
TEST_F(ParamBatchRpcTest, BusAndLfoSinglesStayByteIdentical) {
    EXPECT_EQ(mcpText("set_bus_fx_param", {{"busID", busID}, {"paramIndex", 0}, {"value", 0.8}}),
              QString("ok"));
    EXPECT_FLOAT_EQ(busParam(0), 0.8);
    EXPECT_TRUE(mcpIsError("set_bus_fx_param", {{"busID", 999}, {"paramIndex", 0}, {"value", 0.5}}));

    EXPECT_EQ(mcpText("set_lfo_param",
        {{"trackId", 0}, {"lfoIndex", 0}, {"param", "depth"}, {"value", 0.75}}), QString("ok"));
    EXPECT_DOUBLE_EQ(static_cast<double>(lfoTree(0, 0).getProperty(IDs::depth, -999.0)), 0.75);

    // The unknown-name vocabulary (sorted) is the same text the batch reports.
    EXPECT_EQ(mcpText("set_lfo_param",
        {{"trackId", 0}, {"lfoIndex", 0}, {"param", "bogus"}, {"value", 1.0}}),
        QString("unknown param 'bogus' (valid: bipolar, depth, enabled, phaseOffset, "
                "rate, rateSync, targetParamID, waveform)"));
}

} // namespace
