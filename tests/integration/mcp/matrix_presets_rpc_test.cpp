// MCP <-> RPC parity for the matrix-preset domain — slice 2 of
// docs/plans/2026-09-21-rpc-parity-retrofit.md.
//
// Before this slice, list_matrix_presets / apply_matrix_preset existed only in
// src/mcp/McpTools_Matrix.cpp: the whole domain was unreachable over the frontend
// RPC surface the AGENTS.md parity rule requires. Both surfaces now call the shared
// HDAW::MatrixPresetService, so the strongest available assertion is the direct
// one: call both surfaces and require IDENTICAL payloads (and identical error text
// + class).
//
// Fixture sheets go to a temp dir with HDAW_MATRIX_PRESETS_DIR pointing at it, so
// this runs without real plugins (the live apply/ear pass stays with the manual
// session, per the matrix-presets plan).

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpJsonRpc.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "model/ProjectModel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include <memory>
#include <string>

namespace {

class MatrixRpcParityTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(temp_.isValid());
        ASSERT_TRUE(writeSheets());
        qputenv("HDAW_MATRIX_PRESETS_DIR", temp_.path().toUtf8());
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
        // The default project ships ZERO tracks: create the one used here, then a
        // plugin slot (a fixture id resolves to no live instance — enough for the
        // accounting/parity assertions, no real plugin needed).
        engine->getProjectCommands().addTrack("Track");
        engine->getProjectCommands().addFxSlot(0, "plugin", 0, "fixture.plugin.id");
        engine->drainPendingRoutingRebuild();
    }

    void TearDown() override {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
        qunsetenv("HDAW_MATRIX_PRESETS_DIR");
    }

    static bool writeFile(const QString& path, const QByteArray& bytes) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return f.write(bytes) == bytes.size();
    }

    bool writeSheets() {
        const QJsonObject preset {
            { "id", "fx00000000000001" },
            { "name", "fixture mapped lfo" },
            { "role", "lfo" },
            { "appliesVia", "set_fx_param" },
            { "evidence", "fixture" },
            { "params", QJsonObject {
                { "CutoffFrequency", 100 },
                { "AmpLfo1Depth", QJsonValue::Null },
                { "AutoPanManualPanSwitch", 1 } } } };
        const QJsonObject sheet {
            { "schema", "hdaw.matrix.preset.v1" },
            { "engine", "fixture" },
            { "presets", QJsonArray { preset } } };
        if (!writeFile(temp_.filePath("fixture.json"),
                       QJsonDocument(sheet).toJson(QJsonDocument::Compact)))
            return false;
        const QJsonObject map {
            { "schema", "hdaw.matrix.param_index_map.v1" },
            { "engine", "fixture" },
            { "map", QJsonObject { { "CutoffFrequency",
                                     QJsonObject { { "index", 59 } } } } } };
        if (!writeFile(temp_.filePath("fixture_param_index_map.json"),
                       QJsonDocument(map).toJson(QJsonDocument::Compact)))
            return false;
        // One morph pair whose step references a loose .syx that is intentionally
        // NOT written: a deterministic environment-class error on both surfaces.
        const QJsonObject step {
            { "preset", QJsonObject { { "id", "ms00000000000001" },
                                      { "file", "step1.syx" } } } };
        const QJsonObject morphs {
            { "schema", "hdaw.matrix.preset.morph.v1" },
            { "engine", "fixture" },
            { "pairs", QJsonArray { QJsonObject {
                { "pair", "3:4" }, { "steps", QJsonArray { step } } } } } };
        return writeFile(temp_.filePath("fixture_morphs.json"),
                         QJsonDocument(morphs).toJson(QJsonDocument::Compact));
    }

    // --- MCP surface -------------------------------------------------------
    // handleRequestOnTestThread returns the BARE tool result ({isError, content}),
    // not a JSON-RPC envelope (same contract engine_tools_test.cpp uses).
    QJsonObject mcpToolResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject { { "name", tool }, { "arguments", args } })
            .toObject();
    }
    QJsonObject mcpPayload(const QString& tool, const QJsonObject& args) {
        const auto result = mcpToolResult(tool, args);
        EXPECT_FALSE(result.value("isError").toBool(true)) << "MCP " << tool.toStdString() << " errored";
        const auto content = result.value("content").toArray();
        if (content.isEmpty()) return {};
        return QJsonDocument::fromJson(
                   content[0].toObject().value("text").toString().toUtf8())
            .object();
    }
    QString mcpErrorText(const QString& tool, const QJsonObject& args) {
        const auto result = mcpToolResult(tool, args);
        EXPECT_TRUE(result.value("isError").toBool(false))
            << "expected MCP " << tool.toStdString() << " to fail";
        const auto content = result.value("content").toArray();
        return content.isEmpty() ? QString()
                                 : content[0].toObject().value("text").toString();
    }

    // --- RPC surface -------------------------------------------------------
    frontend::DispatchResult rpc(const QString& method, const QJsonObject& args) {
        return frontend::dispatch(*engine, method, args);
    }
    QJsonObject rpcPayload(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_FALSE(r.isError)
            << "RPC " << method.toStdString() << " errored: "
            << r.payload.toObject().value("message").toString().toStdString();
        return r.payload.toObject();
    }
    QJsonObject rpcError(const QString& method, const QJsonObject& args) {
        const auto r = rpc(method, args);
        EXPECT_TRUE(r.isError) << "expected RPC " << method.toStdString() << " to fail";
        return r.payload.toObject();
    }

    QTemporaryDir temp_;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
};

// G1: the listing payload is byte-identical across the two surfaces.
TEST_F(MatrixRpcParityTest, ListPresetsPayloadMatchesMcp) {
    const QJsonObject args { { "engine", "fixture" } };
    const QJsonObject viaMcp = mcpPayload("list_matrix_presets", args);
    const QJsonObject viaRpc = rpcPayload("matrix.listPresets", args);
    EXPECT_FALSE(viaMcp.isEmpty());
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaRpc.value("engine").toString(), QString("fixture"));
    EXPECT_EQ(viaRpc.value("sheet").toString(), QString("fixture.json"));
    ASSERT_EQ(viaRpc.value("presets").toArray().size(), 1);
    EXPECT_EQ(viaRpc.value("morphs").toArray().size(), 1);
    EXPECT_EQ(viaRpc.value("morphs").toArray()[0].toObject().value("apply").toString(),
              QString("file"));
}

// G2: the parameter-apply accounting is identical across the two surfaces.
// captureToTree:false keeps the payload fully deterministic (the capture receipt is
// timing/device dependent and is covered by the MCP matrix suite instead).
TEST_F(MatrixRpcParityTest, ApplyPresetPayloadMatchesMcp) {
    const QJsonObject args { { "engine", "fixture" }, { "id", "fx00000000000001" },
                             { "trackId", 0 }, { "slotIndex", 0 },
                             { "captureToTree", false } };
    const QJsonObject viaMcp = mcpPayload("apply_matrix_preset", args);
    const QJsonObject viaRpc = rpcPayload("matrix.applyPreset", args);
    EXPECT_FALSE(viaMcp.isEmpty());
    EXPECT_EQ(viaRpc, viaMcp);
    EXPECT_EQ(viaRpc.value("applied").toInt(), 1);   // CutoffFrequency -> index 59
    EXPECT_EQ(viaRpc.value("skipped").toInt(), 2);   // null value + unmapped name
    ASSERT_EQ(viaRpc.value("unmapped").toArray().size(), 1);
    EXPECT_EQ(viaRpc.value("unmapped").toArray()[0].toString(),
              QString("AutoPanManualPanSwitch"));
}

// G3: argument-class failures carry the same text and the same JSON-RPC code on the
// RPC side (the MCP side reports text only, so the text is the shared contract).
TEST_F(MatrixRpcParityTest, ErrorTextAndClassMatchMcp) {
    const QJsonObject unknownEngine { { "engine", "nosuchengine" },
                                      { "id", "fx00000000000001" },
                                      { "trackId", 0 }, { "slotIndex", 0 } };
    const QString mcpText = mcpErrorText("apply_matrix_preset", unknownEngine);
    const auto rpcErr = rpcError("matrix.applyPreset", unknownEngine);
    EXPECT_EQ(rpcErr.value("message").toString(), mcpText);
    EXPECT_EQ(rpcErr.value("code").toInt(), -32602);
    EXPECT_TRUE(mcpText.contains("nosuchengine"));

    const QJsonObject unknownId { { "engine", "fixture" }, { "id", "ffffffffffffffff" },
                                  { "trackId", 0 }, { "slotIndex", 0 } };
    EXPECT_EQ(rpcError("matrix.applyPreset", unknownId).value("message").toString(),
              mcpErrorText("apply_matrix_preset", unknownId));

    // Missing arguments are a surface concern on both sides.
    EXPECT_EQ(rpcError("matrix.applyPreset",
                       QJsonObject { { "engine", "fixture" } })
                  .value("code").toInt(),
              -32602);
}

// G3b: an environment-class failure (a morph step whose .syx is absent) is -32603 on
// the RPC side and keeps the resolved pair path in the message on both sides.
TEST_F(MatrixRpcParityTest, EnvironmentFailureClassAndPath) {
    const QJsonObject args { { "engine", "fixture" }, { "id", "3:4:step1" },
                             { "trackId", 0 }, { "slotIndex", 0 },
                             { "captureToTree", false } };
    const QString mcpText = mcpErrorText("apply_matrix_preset", args);
    const auto rpcErr = rpcError("matrix.applyPreset", args);
    EXPECT_EQ(rpcErr.value("message").toString(), mcpText);
    EXPECT_EQ(rpcErr.value("code").toInt(), -32603);
    EXPECT_TRUE(mcpText.contains("file not found"));
    EXPECT_TRUE(mcpText.contains("3-4")); // the pair directory the step resolves into
}

// G4: an unknown sub-method is a clean -32601, like every other namespace.
TEST_F(MatrixRpcParityTest, UnknownMethodIs32601) {
    const auto r = rpc("matrix.nope", QJsonObject {});
    EXPECT_TRUE(r.isError);
    EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32601);
}

} // namespace
