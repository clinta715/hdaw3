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

// Zero-track default contract (v0.33+): createDefaultProject() ships an empty
// TRACK_LIST — tests dispatch track-indexed RPCs, so seed via the same
// project.addTrack RPC the UI uses.
void seedTracks(AudioEngine& engine, int n)
{
    for (int i = 0; i < n; ++i)
        rpc(engine, "project.addTrack", QJsonObject{ { "name", "Track" } });
}

} // namespace

// ─── G5: generateAutomationEnvelope RPC ───────────────────────────

TEST(EnvelopeGenerationRpc, G5_GenerateAutomationEnvelope_HappyPath)
{
    AudioEngine engine;
    engine.initialize();

    seedTracks(engine, 1);

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

    seedTracks(engine, 1);

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

    seedTracks(engine, 2);  // midi clip lands on track 1

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

    seedTracks(engine, 1);

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

    seedTracks(engine, 1);

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

// D1 (post-arrangement pass): the RPC twin accepts the same `replace` key as
// the MCP tool. Without it the conflict guard fires with the historical text
// byte-for-byte; with it the lane bound to paramID is taken over (renamed in
// place, its points kept), which is what makes the pass re-runnable.
TEST(EnvelopeGenerationRpc, AddAutomationLaneReplaceTakesOwnership)
{
    AudioEngine engine;
    engine.initialize();

    seedTracks(engine, 1);

    rpc(engine, "project.addAutomationLane",
        QJsonObject{ { "trackIndex", 0 }, { "laneName", "Old" }, { "paramID", 139 } });
    rpc(engine, "project.addAutomationPoint",
        QJsonObject{ { "trackIndex", 0 }, { "lane", "Old" }, { "time", 4.0 }, { "value", 0.25 } });

    // Legacy form: still the same error, name and paramID untouched.
    auto clash = frontend::dispatch(engine, "project.addAutomationLane",
        QJsonObject{ { "trackIndex", 0 }, { "laneName", "DubThrow" }, { "paramID", 139 } });
    ASSERT_TRUE(clash.isError);
    EXPECT_EQ(clash.payload.toObject().value("message").toString().toStdString(),
              "lane name or paramID already exists");

    // replace:true renames the lane bound to 139 in place.
    auto r = frontend::dispatch(engine, "project.addAutomationLane",
        QJsonObject{ { "trackIndex", 0 }, { "laneName", "DubThrow" }, { "paramID", 139 },
                     { "replace", true } });
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();

    auto lanes = rpc(engine, "read.getAutomationLanes",
                     QJsonObject{ { "trackIndex", 0 } }).toArray();
    bool renamed = false, oldGone = true;
    int boundTo139 = 0;
    for (const auto& lv : lanes)
    {
        const auto lane = lv.toObject();
        if (lane.value("name").toString() == "DubThrow")
        {
            renamed = true;
            EXPECT_EQ(lane.value("paramID").toInt(), 139);
        }
        if (lane.value("name").toString() == "Old")
            oldGone = false;
        if (lane.value("paramID").toInt() == 139)
            ++boundTo139;
    }
    EXPECT_TRUE(renamed);
    EXPECT_TRUE(oldGone);
    EXPECT_EQ(boundTo139, 1);

    // Points survived the rename (beats domain through the read path).
    auto pts = rpc(engine, "read.getAutomationPoints",
                   QJsonObject{ { "trackIndex", 0 }, { "laneName", "DubThrow" } }).toArray();
    ASSERT_EQ(pts.size(), 1);
    EXPECT_NEAR(pts[0].toObject().value("value").toDouble(), 0.25, 1e-6);
}
