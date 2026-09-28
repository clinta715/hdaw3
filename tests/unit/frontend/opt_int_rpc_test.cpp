// MCP <-> RPC parity for the OPTIONAL integer argument helper (optInt), item 2 of
// the 2026-09-28 mechanization follow-up: a PRESENT non-integral optional int must
// REFUSE with the MCP validator's exact bytes on BOTH surfaces instead of silently
// becoming the fallback (which is what happened while every call site passed
// `nullptr`). An ABSENT key keeps the tolerant optional behaviour (fallback, no
// error) — that is the whole point of the optional form.
//
// Covered here with one QUERY route (read.getWaveformPeaks / get_waveform_peaks,
// optional `numBins`) and one MUTATION route (project.addTrack / add_track,
// optional `color`): a fractional value refuses identically and touches nothing;
// the same call with the key ABSENT uses the documented fallback on both surfaces;
// an integral value still works on both.

#include <gtest/gtest.h>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "common/ReadModel.h"
#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ProjectModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <string>

namespace {

class OptIntRpcTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        auto& cmds = engine->getProjectCommands();
        ASSERT_GE(cmds.addTrack("Audio"), 0);

        // A real, short WAV so the QUERY route has an audio clip to read.
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
        const auto file = dir.getChildFile(
            juce::String("hdaw_optint_fixture_")
            + juce::String(juce::Random::getSystemRandom().nextInt()) + ".wav");
        const double sr = 48000.0;
        const int len = static_cast<int>(sr * 1.0);
        juce::AudioBuffer<float> buf(2, len);
        for (int s = 0; s < len; ++s)
        {
            const float v = 0.1f * std::sin(2.0f * juce::MathConstants<float>::pi
                                            * 440.0f * static_cast<float>(s) / static_cast<float>(sr));
            buf.setSample(0, s, v);
            buf.setSample(1, s, v);
        }
        juce::WavAudioFormat wavFmt;
        std::unique_ptr<juce::AudioFormatWriter> w(
            wavFmt.createWriterFor(new juce::FileOutputStream(file), sr, 2, 24, {}, 0));
        ASSERT_NE(w, nullptr);
        w->writeFromAudioSampleBuffer(buf, 0, len);
        w.reset();
        wavPath = QString::fromUtf8(file.getFullPathName().toRawUTF8());

        audioClip = cmds.addAudioClip(0, 0.0, 4.0, wavPath.toStdString(), "Audio");
        ASSERT_GT(audioClip, 0);
        engine->drainPendingRoutingRebuild();

        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    void TearDown() override {
        if (!wavPath.isEmpty())
            juce::File(wavPath.toStdString()).deleteFile();
    }

    // --- MCP surface -------------------------------------------------------
    QJsonObject mcpResult(const QString& tool, const QJsonObject& args) {
        return server->handleRequestOnTestThread(
                   1, "tools/call", QJsonObject{{"name", tool}, {"arguments", args}})
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
        EXPECT_FALSE(r.isError) << "RPC " << method.toStdString() << " errored: "
                                << r.payload.toObject().value("message").toString().toStdString();
        return r.payload;
    }
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
    int trackCount() {
        return engine->getProjectModel().getTrackListTree().getNumChildren();
    }
    int trackColor(int index) {
        return static_cast<int>(engine->getProjectModel().getTrackListTree()
                                    .getChild(index).getProperty(IDs::color, -1));
    }
    int undoDepth() {
        return static_cast<int>(engine->getProjectCommands().getUndoDescriptions().size());
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    QString wavPath;
    int audioClip = -1;
};

// QUERY route: read.getWaveformPeaks / get_waveform_peaks, optional `numBins`.
TEST_F(OptIntRpcTest, PresentFractionalNumBinsRefusesWhileAbsentUsesTheFallback) {
    // Present, fractional — refused identically; the query never runs.
    const QJsonObject fractional{{"clipId", audioClip}, {"numBins", 1.5}};
    expectSameFailure("get_waveform_peaks", "read.getWaveformPeaks", fractional);
    EXPECT_EQ(mcpText("get_waveform_peaks", fractional),
              QString("invalid params: numBins: expected integer"));

    // Absent — the documented fallback (1000) works on BOTH surfaces.
    const QJsonObject absent{{"clipId", audioClip}};
    const QJsonValue viaMcp = mcpValue("get_waveform_peaks", absent);
    const QJsonValue viaRpc = rpcPayload("read.getWaveformPeaks", absent);
    EXPECT_EQ(viaRpc, viaMcp) << "the absent-key fallback must be identical on both surfaces";
    ASSERT_TRUE(viaMcp.isObject());
    EXPECT_FALSE(viaMcp.toObject().value("peaks").toArray().isEmpty());

    // Integral — still works on both (identical payloads).
    const QJsonObject integral{{"clipId", audioClip}, {"numBins", 8}};
    const QJsonValue mcp8 = mcpValue("get_waveform_peaks", integral);
    const QJsonValue rpc8 = rpcPayload("read.getWaveformPeaks", integral);
    EXPECT_EQ(rpc8, mcp8);
    ASSERT_TRUE(mcp8.isObject());
    EXPECT_FALSE(mcp8.toObject().value("peaks").toArray().isEmpty())
        << "an integral numBins must reach the query";
}

// MUTATION route: project.addTrack / add_track, optional `color`.
TEST_F(OptIntRpcTest, PresentFractionalColorRefusesWithoutMutating) {
    const int tracks = trackCount();
    const int depth = undoDepth();

    const QJsonObject fractional{{"name", "Offender"}, {"color", 1.5}};
    expectSameFailure("add_track", "project.addTrack", fractional);
    EXPECT_EQ(mcpText("add_track", fractional),
              QString("invalid params: color: expected integer"));
    EXPECT_EQ(trackCount(), tracks) << "a refused argument must not add a track";
    EXPECT_EQ(undoDepth(), depth) << "a refused argument must not open an undo unit";

    // Absent — the documented palette fallback works on BOTH surfaces. (The MCP
    // tool answers {trackId,routed,trackID} while the RPC route answers the bare
    // index — a pre-existing shape split; assert the EFFECT on both.)
    const QJsonObject absent{{"name", "Fallback"}};
    const QJsonValue viaMcp = mcpValue("add_track", absent);
    ASSERT_TRUE(viaMcp.isObject());
    const int mcpIdx = viaMcp.toObject().value("trackId").toInt();
    EXPECT_EQ(trackColor(mcpIdx),
              static_cast<int>(ProjectModel::trackColorForIndex(mcpIdx)))
        << "an absent color must take the palette default";

    const auto rpcAbsent = rpc("project.addTrack", absent);
    ASSERT_FALSE(rpcAbsent.isError);
    const int rpcIdx = rpcAbsent.payload.toInt();
    EXPECT_EQ(trackColor(rpcIdx),
              static_cast<int>(ProjectModel::trackColorForIndex(rpcIdx)));

    // Integral — reaches the command on both surfaces.
    const QJsonObject integral{{"name", "Explicit"}, {"color", 123456}};
    const QJsonValue mcpInt = mcpValue("add_track", integral);
    ASSERT_TRUE(mcpInt.isObject());
    EXPECT_EQ(trackColor(mcpInt.toObject().value("trackId").toInt()), 123456);

    const auto rpcInt = rpc("project.addTrack", integral);
    ASSERT_FALSE(rpcInt.isError);
    EXPECT_EQ(trackColor(rpcInt.payload.toInt()), 123456);
}

} // namespace
