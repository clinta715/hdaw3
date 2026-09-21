// RPC-layer tests for the psy_fm modulation-matrix methods — the retrofit of the
// confirmed MCP↔RPC gap (docs/plans/2026-09-21-rpc-parity-retrofit.md).
//
// Router_PsyFm previously exposed only getAnalysis/loadPreset while
// McpTools_PsyFm served five tools, so set_mod_route / clear_mod_matrix /
// mod_matrix_debug were unreachable over the frontend RPC surface that the
// AGENTS.md parity rule requires.
//
// modMatrixDebug now delegates to src/common/PsyFmModMatrixView.cpp, the SAME
// builder the MCP tool uses, so the two surfaces cannot drift. The strongest
// assertion available is therefore the direct one: call both surfaces and
// require identical JSON (ParityWithMcpTool).

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools_Private.h"
#include "model/ProjectModel.h"

#include <memory>
#include <string>

namespace {

class PsyFmRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        engine->getProjectCommands().addTrack("Test");
        engine->getProjectCommands().addFxSlot(0, "psy_fm", 0, "");
        engine->drainPendingRoutingRebuild();
        server.setEngine(engine.get());
        mcp::registerPsyFmTools(server, engine.get());
    }

    frontend::DispatchResult callRaw(const QString& method, const QJsonObject& args = {}) {
        return frontend::dispatch(*engine, method, args);
    }

    QJsonObject call(const QString& method, const QJsonObject& args = {}) {
        auto r = callRaw(method, args);
        EXPECT_FALSE(r.isError)
            << "dispatch(" << method.toStdString() << ") errored: "
            << (r.payload.isObject()
                    ? r.payload.toObject().value("message").toString().toStdString()
                    : std::string("non-object error"));
        return r.payload.toObject();
    }

    // The MCP tool's payload, parsed back to an object (bare-result contract:
    // content[0].text holds the JSON).
    QJsonObject mcpDebugView() {
        const auto r = server.handleRequestOnTestThread(
            1, "tools/call",
            QJsonObject{ { "name", "psy_fm_mod_matrix_debug" },
                         { "arguments", QJsonObject{ { "trackId", 0 }, { "slotIndex", 0 } } } });
        EXPECT_FALSE(r.toObject().value("isError").toBool());
        const auto text = r.toObject().value("content").toArray().at(0).toObject()
                              .value("text").toString();
        return QJsonDocument::fromJson(text.toUtf8()).object();
    }

    QJsonObject route(const char* source, const char* dest, double depth) {
        return QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 },
                            { "source", source }, { "dest", dest }, { "depth", depth } };
    }

    std::unique_ptr<AudioEngine> engine;
    mcp::McpServer server;
};

// setModRoute reaches the command layer: the debug view then reports it.
TEST_F(PsyFmRpcTest, SetModRouteWritesThroughTheCommandLayer)
{
    call("psy_fm.setModRoute", route("modWheel", "op1Ratio", 0.5));
    call("psy_fm.setModRoute", route("feedbackLFO", "op6Feedback", 0.4));

    const auto view = call("psy_fm.modMatrixDebug",
                           QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 } });
    ASSERT_EQ(view.value("routes").toArray().size(), 2);

    const auto r0 = view.value("routes").toArray()[0].toObject();
    EXPECT_EQ(r0.value("source").toString(), QString("ModWheel"));
    EXPECT_EQ(r0.value("dest").toString(), QString("Op1Ratio"));
    EXPECT_NEAR(r0.value("depth").toDouble(), 0.5, 1e-5);

    const auto r1 = view.value("routes").toArray()[1].toObject();
    EXPECT_EQ(r1.value("source").toString(), QString("FeedbackLFO"));
    EXPECT_EQ(r1.value("dest").toString(), QString("Op6Feedback"));
    EXPECT_NEAR(r1.value("depth").toDouble(), 0.4, 1e-5);
}

// clearModMatrix drops every route.
TEST_F(PsyFmRpcTest, ClearModMatrixRemovesAllRoutes)
{
    call("psy_fm.setModRoute", route("modWheel", "op1Ratio", 0.5));
    call("psy_fm.setModRoute", route("feedbackLFO", "op6Feedback", 0.4));
    call("psy_fm.clearModMatrix", QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 } });

    const auto view = call("psy_fm.modMatrixDebug",
                           QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 } });
    EXPECT_TRUE(view.value("routes").toArray().isEmpty());
    EXPECT_NEAR(view.value("feedbackBudget").toObject().value("totalDepth").toDouble(), 0.0, 1e-6);
}

// The parity gate: RPC and MCP must produce byte-identical payloads, including
// the Op6Feedback depth-budget scaling (the part most likely to drift).
TEST_F(PsyFmRpcTest, ParityWithMcpTool)
{
    call("psy_fm.setModRoute", route("modWheel", "op6Feedback", 0.6));
    call("psy_fm.setModRoute", route("velocity", "op6Feedback", 0.6));
    call("psy_fm.setModRoute", route("modWheel", "op1Ratio", 0.25));

    const auto viaRpc = call("psy_fm.modMatrixDebug",
                             QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 } });
    const auto viaMcp = mcpDebugView();

    ASSERT_FALSE(viaRpc.isEmpty());
    EXPECT_EQ(viaRpc, viaMcp);
    // And the shared builder is actually exercising the budget path.
    EXPECT_TRUE(viaRpc.value("feedbackBudget").toObject().value("budgetHit").toBool());
}

// Error paths mirror getAnalysis/loadPreset: bad slot, bad args, unknown method.
TEST_F(PsyFmRpcTest, ErrorPaths)
{
    const auto unknown = callRaw("psy_fm.nope", QJsonObject{});
    EXPECT_TRUE(unknown.isError);
    EXPECT_TRUE(unknown.payload.toObject().value("message").toString()
                    .contains("unknown psy_fm method"));

    const auto missingArgs = callRaw("psy_fm.setModRoute",
                                     QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 0 } });
    EXPECT_TRUE(missingArgs.isError);
    EXPECT_TRUE(missingArgs.payload.toObject().value("message").toString()
                    .contains("source and dest required"));

    const auto badSlot = callRaw("psy_fm.modMatrixDebug",
                                 QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 9 } });
    EXPECT_TRUE(badSlot.isError);
    EXPECT_TRUE(badSlot.payload.toObject().value("message").toString()
                    .contains("not a psy_fm synth"));

    // A non-psy_fm slot is rejected the same way.
    engine->getProjectCommands().addFxSlot(0, "reverb", 1, "");
    engine->drainPendingRoutingRebuild();
    const auto wrongType = callRaw("psy_fm.clearModMatrix",
                                   QJsonObject{ { "trackIndex", 0 }, { "slotIndex", 1 } });
    EXPECT_TRUE(wrongType.isError);
    EXPECT_TRUE(wrongType.payload.toObject().value("message").toString()
                    .contains("not a psy_fm synth"));
}

} // namespace
