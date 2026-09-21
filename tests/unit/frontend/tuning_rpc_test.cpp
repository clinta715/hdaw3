// MCP <-> RPC parity for the spectral tuning analysis — slice 4 (the last confirmed gap)
// of docs/plans/2026-09-21-rpc-parity-retrofit.md.
//
// analyze_tuning lived entirely in src/mcp/McpTools_Tuning.cpp (static helpers: role
// targets, descriptors, check/suggestion) and there was no `tuning` namespace at all, so
// the only tuning tool was unreachable over the RPC surface. The analysis now lives in
// src/common/TuningAnalysis.cpp and both surfaces call it, so the strongest assertion
// available is the direct one: call both and require the same payload.
//
// `tuning.jobStatus` covers the async mode (wait:false), reading the same process-wide
// job registry the MCP `poll_job` tool reads.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpJsonRpc.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "model/ProjectModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QThread>

#include <cmath>
#include <memory>
#include <string>

#include <juce_audio_formats/juce_audio_formats.h>

namespace {

// Same fixture tone the MCP job tests use: 110 Hz + 1200 Hz, 2 s mono, 48 kHz.
QString writeFixtureWav(const char* tag)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile(tag, ".wav", false);
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> fos(f.createOutputStream());
    if (fos == nullptr) return {};
    std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(fos.get(), 48000.0, 1, 16, {}, 0));
    if (w == nullptr) return {};
    fos.release();

    constexpr double sr = 48000.0;
    const int total = static_cast<int>(sr * 2.0);
    juce::AudioBuffer<float> buf(1, total);
    buf.clear();
    for (int i = 0; i < total; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        buf.setSample(0, i, static_cast<float>(
            0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * t)
            + 0.05 * std::sin(2.0 * juce::MathConstants<double>::pi * 1200.0 * t)));
    }
    w->writeFromAudioSampleBuffer(buf, 0, total);
    w.reset();
    return QString::fromStdString(f.getFullPathName().toStdString());
}

class TuningRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
    }

    void TearDown() override {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
    }

    // --- MCP surface -------------------------------------------------------
    QJsonObject mcpResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call",
                   QJsonObject { { "name", tool }, { "arguments", args } })
            .toObject();
    }
    QJsonObject mcpObject(const QString& tool, const QJsonObject& args) {
        const auto result = mcpResult(tool, args);
        EXPECT_FALSE(result.value("isError").toBool(true))
            << "MCP " << tool.toStdString() << " errored: "
            << result.value("content").toArray().at(0).toObject()
                   .value("text").toString().toStdString();
        const auto content = result.value("content").toArray();
        if (content.isEmpty()) return {};
        return QJsonDocument::fromJson(
                   content[0].toObject().value("text").toString().toUtf8())
            .object();
    }
    QString mcpText(const QString& tool, const QJsonObject& args) {
        const auto result = mcpResult(tool, args);
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

    // Poll tuning.jobStatus until the job leaves "running" (the analysis may run the
    // Python sidecar, so allow a generous bound).
    QJsonObject pollUntilFinished(int jobId) {
        for (int i = 0; i < 120; ++i) {
            const auto st = rpcPayload("tuning.jobStatus", QJsonObject { { "jobId", jobId } });
            const auto state = st.value("state").toString();
            if (state != "running") return st;
            QThread::msleep(100);
        }
        return {};
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
};

// G1: the analysis payload is identical across the two surfaces — for a known role, an
// unknown role (skipped:true) and the no-role (all-targets) form.
TEST_F(TuningRpcTest, AnalyzePayloadMatchesMcp) {
    const QString wav = writeFixtureWav("hdaw_tuning_rpc");
    ASSERT_FALSE(wav.isEmpty());

    const QJsonObject known { { "wavPath", wav }, { "role", "bass" } };
    const QJsonObject viaRpc = rpcPayload("tuning.analyze", known);
    EXPECT_FALSE(viaRpc.isEmpty());
    EXPECT_EQ(viaRpc, mcpObject("analyze_tuning", known));
    EXPECT_TRUE(viaRpc.contains("descriptors"));
    EXPECT_TRUE(viaRpc.contains("check"));
    EXPECT_TRUE(viaRpc.value("check").toObject().contains("pass"));

    const QJsonObject unknown { { "wavPath", wav }, { "role", "ambience" } };
    const QJsonObject unknownRpc = rpcPayload("tuning.analyze", unknown);
    EXPECT_EQ(unknownRpc, mcpObject("analyze_tuning", unknown));
    EXPECT_TRUE(unknownRpc.value("check").toObject().value("skipped").toBool())
        << "an unknown role must report skipped on both surfaces";

    const QJsonObject all { { "wavPath", wav } };
    const QJsonObject allRpc = rpcPayload("tuning.analyze", all);
    EXPECT_EQ(allRpc, mcpObject("analyze_tuning", all));
    // Shape note: with NO role the Python sidecar emits {wav, descriptors, summary} while
    // the C++ fallback adds per-role `checks` — a PRE-EXISTING difference between the two
    // analysis paths (measured 2026-09-21), which the shared function preserves verbatim
    // rather than papering over. Assert the invariant both paths share (the surfaces
    // agreeing is asserted above).
    EXPECT_TRUE(allRpc.value("descriptors").isObject()) << QJsonDocument(allRpc)
        .toJson(QJsonDocument::Compact).constData();

    juce::File(wav.toStdString()).deleteFile();
}

// G2: the async mode returns a job whose result equals the synchronous payload, and
// tuning.jobStatus reports it (same registry as the MCP poll_job tool).
TEST_F(TuningRpcTest, AsyncJobStatusMatchesSyncResult) {
    const QString wav = writeFixtureWav("hdaw_tuning_rpc_async");
    ASSERT_FALSE(wav.isEmpty());

    const QJsonObject syncArgs { { "wavPath", wav }, { "role", "bass" } };
    const QJsonObject sync = rpcPayload("tuning.analyze", syncArgs);

    QJsonObject asyncArgs = syncArgs;
    asyncArgs["wait"] = false;
    const QJsonObject started = rpcPayload("tuning.analyze", asyncArgs);
    const int jobId = started.value("jobId").toInt(0);
    ASSERT_GT(jobId, 0);
    EXPECT_EQ(started.value("state").toString(), QString("running"));
    EXPECT_EQ(started.value("pollWith").toString(), QString("tuning.jobStatus"));

    const QJsonObject status = pollUntilFinished(jobId);
    ASSERT_FALSE(status.isEmpty());
    EXPECT_EQ(status.value("state").toString(), QString("finished"))
        << QJsonDocument(status).toJson(QJsonDocument::Compact).constData();
    EXPECT_EQ(status.value("result").toObject(), sync);

    // The MCP side sees the same job through the shared registry.
    EXPECT_TRUE(mcpText("poll_job", QJsonObject { { "jobId", jobId } }).contains("finished"));

    juce::File(wav.toStdString()).deleteFile();
}

// G3: argument failures are clean -32602 on the RPC side.
TEST_F(TuningRpcTest, ErrorClasses) {
    EXPECT_EQ(rpcError("tuning.analyze", {}).value("code").toInt(), -32602);
    EXPECT_TRUE(rpcError("tuning.analyze", {})
                    .value("message").toString().contains("wavPath"));

    const auto missing = rpcError("tuning.analyze",
                                  QJsonObject { { "wavPath", "Z:/does/not/exist.wav" } });
    EXPECT_EQ(missing.value("code").toInt(), -32602);
    EXPECT_TRUE(missing.value("message").toString().contains("wav not found"))
        << missing.value("message").toString().toStdString();

    EXPECT_EQ(rpcError("tuning.jobStatus", {}).value("code").toInt(), -32602);
    const auto unknownJob = rpcError("tuning.jobStatus", QJsonObject { { "jobId", 987654321 } });
    EXPECT_EQ(unknownJob.value("code").toInt(), -32602);
    EXPECT_EQ(unknownJob.value("message").toString(), QString("unknown jobId"));

    const auto unknownMethod = rpcError("tuning.nope", {});
    EXPECT_EQ(unknownMethod.value("code").toInt(), -32601);
}

} // namespace
