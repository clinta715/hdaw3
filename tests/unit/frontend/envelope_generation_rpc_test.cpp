// RPC-layer tests for envelope generation (Unit B).
// Exercises the JSON-RPC dispatch path for generateAutomationEnvelope,
// generateClipGainEnvelope, and generateClipCcLane through frontend::dispatch().
// Mirrors ghost_clips_rpc_test.cpp pattern.

#include <gtest/gtest.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"

namespace {

QJsonValue rpc(AudioEngine& engine, const QString& method, const QJsonValue& params = {})
{
    auto r = frontend::dispatch(engine, method, params);
    EXPECT_FALSE(r.isError)
        << "dispatch(" << method.toStdString() << ") returned error: "
        << (r.payload.isObject() ? r.payload.toObject().value("message").toString().toStdString()
                                 : std::string("non-object error"));
    return r.payload;
}

} // namespace

// ─── G5: generateAutomationEnvelope RPC ───────────────────────────

TEST(EnvelopeGenerationRpc, G5_GenerateAutomationEnvelope_HappyPath)
{
    AudioEngine engine;
    engine.initialize();

    // Add automation lane first.
    rpc(engine, "project.addAutomationLane",
        QJsonObject{ { "trackIndex", 0 }, { "laneName", "Volume" } });

    auto resp = rpc(engine, "project.generateAutomationEnvelope",
                    QJsonObject{ { "trackIndex", 0 }, { "lane", "Volume" }, { "shape", "ramp" } });
    // Should not error (empty object is success).
    EXPECT_TRUE(resp.isObject());

    // Verify points were generated.
    auto points = rpc(engine, "read.getAutomationPoints",
                      QJsonObject{ { "trackIndex", 0 }, { "laneName", "Volume" } });
    EXPECT_TRUE(points.isArray());
    EXPECT_GT(points.toArray().size(), 0);
}

TEST(EnvelopeGenerationRpc, G5_GenerateClipGainEnvelope_HappyPath)
{
    AudioEngine engine;
    engine.initialize();

    // Create audio clip.
    auto clipResp = rpc(engine, "project.addAudioClip",
                        QJsonObject{ { "trackIndex", 0 }, { "start", 0.0 },
                                     { "duration", 8.0 }, { "sourceFile", "test.wav" },
                                     { "name", "Test" } });
    int clipId = static_cast<int>(clipResp.toDouble());
    ASSERT_GT(clipId, 0);

    auto resp = rpc(engine, "project.generateClipGainEnvelope",
                    QJsonObject{ { "clipId", clipId }, { "shape", "adsr" } });
    EXPECT_TRUE(resp.isObject());
}

TEST(EnvelopeGenerationRpc, G5_GenerateClipCcLane_HappyPath)
{
    AudioEngine engine;
    engine.initialize();

    // Create MIDI clip.
    auto clipResp = rpc(engine, "project.addMidiClip",
                        QJsonObject{ { "trackIndex", 1 }, { "start", 0.0 },
                                     { "duration", 8.0 }, { "name", "MidiClip" } });
    int clipId = static_cast<int>(clipResp.toDouble());
    ASSERT_GT(clipId, 0);

    auto resp = rpc(engine, "project.generateClipCcLane",
                    QJsonObject{ { "clipId", clipId }, { "controllerNumber", 1 },
                                 { "shape", "sine" } });
    EXPECT_TRUE(resp.isObject());
}

TEST(EnvelopeGenerationRpc, G5_InvalidShape_ReturnsError)
{
    AudioEngine engine;
    engine.initialize();

    rpc(engine, "project.addAutomationLane",
        QJsonObject{ { "trackIndex", 0 }, { "laneName", "Volume" } });

    auto r = frontend::dispatch(engine, "project.generateAutomationEnvelope",
                                QJsonObject{ { "trackIndex", 0 }, { "lane", "Volume" },
                                             { "shape", "bogus" } });
    EXPECT_TRUE(r.isError);
    EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
}

TEST(EnvelopeGenerationRpc, G5_DefaultsApplied)
{
    AudioEngine engine;
    engine.initialize();

    rpc(engine, "project.addAutomationLane",
        QJsonObject{ { "trackIndex", 0 }, { "laneName", "Volume" } });

    // Call with just shape — all other params should use defaults.
    auto resp = rpc(engine, "project.generateAutomationEnvelope",
                    QJsonObject{ { "trackIndex", 0 }, { "lane", "Volume" }, { "shape", "ramp" } });
    EXPECT_TRUE(resp.isObject());

    // Verify points were generated (defaults: start=0, end=16 beats).
    auto points = rpc(engine, "read.getAutomationPoints",
                      QJsonObject{ { "trackIndex", 0 }, { "laneName", "Volume" } });
    EXPECT_TRUE(points.isArray());
    EXPECT_GT(points.toArray().size(), 0);
}
