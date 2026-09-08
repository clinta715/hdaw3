// RPC-layer tests for composition.generateRhythmPattern (v0.15.2+).
// Exercises the exact JSON-RPC dispatch path the frontend uses:
// frontend::dispatch() with parsed QJsonValue params.

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

QJsonObject findClipJson(const QJsonObject& snap, int clipId)
{
    for (const auto& v : snap.value("clips").toArray())
    {
        auto o = v.toObject();
        if (static_cast<int>(o.value("clipId").toDouble()) == clipId)
            return o;
    }
    ADD_FAILURE() << "clip " << clipId << " missing from snapshot JSON";
    return {};
}

} // namespace

TEST(RhythmGenerationRpc, DefaultPolyrhythmCreatesClip)
{
    AudioEngine engine;
    engine.initialize();

    auto resp = rpc(engine, "composition.generateRhythmPattern",
                    QJsonObject{ { "trackIndex", 0 } });
    ASSERT_TRUE(resp.isObject());
    const int clipId = resp.toObject().value("clipId").toInt();
    const int noteCount = resp.toObject().value("noteCount").toInt();
    ASSERT_GT(clipId, 0);
    EXPECT_EQ(noteCount, 6);

    auto snap = rpc(engine, "read.snapshot").toObject();
    auto clip = findClipJson(snap, clipId);
    EXPECT_EQ(static_cast<int>(clip.value("trackIndex").toDouble()), 0);
    EXPECT_DOUBLE_EQ(clip.value("startBeat").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(clip.value("durationBeats").toDouble(), 4.0);
}

TEST(RhythmGenerationRpc, DslVoiceRespected)
{
    AudioEngine engine;
    engine.initialize();
    auto resp = rpc(engine, "composition.generateRhythmPattern",
                    QJsonObject{ { "trackIndex", 0 },
                                 { "pulseA", 0 }, { "pulseB", 0 },
                                 { "dsl", "E(3,8)" } });
    ASSERT_TRUE(resp.isObject());
    EXPECT_EQ(resp.toObject().value("noteCount").toInt(), 3);
}

TEST(RhythmGenerationRpc, MalformedDslReturnsError)
{
    AudioEngine engine;
    engine.initialize();
    auto r = frontend::dispatch(engine, "composition.generateRhythmPattern",
                                QJsonObject{ { "trackIndex", 0 }, { "dsl", "E(3,8" } });
    EXPECT_TRUE(r.isError);
    EXPECT_TRUE(r.payload.toObject().value("message").toString().contains("dsl"));
}

TEST(RhythmGenerationRpc, MissingTrackIndexErrors)
{
    AudioEngine engine;
    engine.initialize();
    auto r = frontend::dispatch(engine, "composition.generateRhythmPattern", QJsonObject{});
    EXPECT_TRUE(r.isError);
}

TEST(RhythmGenerationRpc, CorpusPhraseCreatesClip)
{
    // snare_s2_4bar: 4-bar offbeat phrase with 7 hits (see bank test).
    AudioEngine engine;
    engine.initialize();
    auto resp = rpc(engine, "composition.generateRhythmPattern",
                    QJsonObject{ { "trackIndex", 0 }, { "phrase", "snare_s2_4bar" } });
    ASSERT_TRUE(resp.isObject());
    const int clipId = resp.toObject().value("clipId").toInt();
    EXPECT_EQ(resp.toObject().value("noteCount").toInt(), 7);
    ASSERT_GT(clipId, 0);

    auto snap = rpc(engine, "read.snapshot").toObject();
    auto clip = findClipJson(snap, clipId);
    // phrase is 4 bars -> 4-bar clip
    EXPECT_DOUBLE_EQ(clip.value("durationBeats").toDouble(), 16.0);
}

TEST(RhythmGenerationRpc, CorpusPhraseByRoleCreatesClip)
{
    AudioEngine engine;
    engine.initialize();
    auto resp = rpc(engine, "composition.generateRhythmPattern",
                    QJsonObject{ { "trackIndex", 0 }, { "phraseRole", "clap" }, { "phraseIndex", 0 } });
    ASSERT_TRUE(resp.isObject());
    // clap_c1_1bar has exactly one hit
    EXPECT_EQ(resp.toObject().value("noteCount").toInt(), 1);
}

TEST(RhythmGenerationRpc, UnknownCorpusPhraseErrors)
{
    AudioEngine engine;
    engine.initialize();
    auto r = frontend::dispatch(engine, "composition.generateRhythmPattern",
                                QJsonObject{ { "trackIndex", 0 }, { "phrase", "nope" } });
    EXPECT_TRUE(r.isError);
    EXPECT_TRUE(r.payload.toObject().value("message").toString().contains("unknown phrase"));
}
