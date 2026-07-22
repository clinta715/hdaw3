// RPC-layer tests for ghost clips & paint/repeat (v0.12.0+).
//
// These exercise the exact JSON-RPC dispatch path the frontend uses —
// frontend::dispatch() with parsed QJsonValue params — so the parameter
// parsing, return-value shaping, and snapshot JSON serialization
// (isGhost / ghostSourceId fields the React client reads) are all
// verified without spinning up a WebSocket. Mirrors the dispatch seam
// documented in src/frontend/FrontendRouter.h (server may be nullptr).

#include <gtest/gtest.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"

namespace {

// Call a project.* method through the router and return its result payload.
// Fails the test on dispatch error.
QJsonValue rpc(AudioEngine& engine, const QString& method, const QJsonValue& params = {})
{
    auto r = frontend::dispatch(engine, method, params);
    EXPECT_FALSE(r.isError)
        << "dispatch(" << method.toStdString() << ") returned error: "
        << (r.payload.isObject() ? r.payload.toObject().value("message").toString().toStdString()
                                 : std::string("non-object error"));
    return r.payload;
}

// Find a single clip object in a read.snapshot result by id.
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

// ─── createGhostClip RPC ───────────────────────────────────────────────

TEST(GhostClipsRpc, CreateGhostClipRoundTrip)
{
    AudioEngine engine;
    engine.initialize();

    auto srcResp = rpc(engine, "project.addMidiClip",
                       QJsonObject{ { "trackIndex", 0 }, { "start", 0.0 },
                                    { "duration", 4.0 }, { "name", "Src" } });
    int srcId = static_cast<int>(srcResp.toDouble());
    ASSERT_GT(srcId, 0);

    QJsonObject params{
        { "sourceClipId", srcId },
        { "newStart", 8.0 },
        { "newTrackIndex", 1 },
    };
    auto ghostResp = rpc(engine, "project.createGhostClip", params);
    ASSERT_TRUE(ghostResp.isDouble());
    int ghostId = static_cast<int>(ghostResp.toDouble());
    ASSERT_GT(ghostId, 0);
    EXPECT_NE(ghostId, srcId);

    // The ghost's JSON snapshot must carry the new ghost fields.
    auto snap = rpc(engine, "read.snapshot").toObject();
    auto clip = findClipJson(snap, ghostId);
    EXPECT_TRUE(clip.value("isGhost").toBool());
    EXPECT_EQ(static_cast<int>(clip.value("ghostSourceId").toDouble()), srcId);
    EXPECT_DOUBLE_EQ(clip.value("startBeat").toDouble(), 8.0);
    EXPECT_EQ(static_cast<int>(clip.value("trackIndex").toDouble()), 1);
}

TEST(GhostClipsRpc, CreateGhostClipMissingParamsErrors)
{
    AudioEngine engine;
    engine.initialize();

    // Missing newTrackIndex.
    auto r = frontend::dispatch(engine, "project.createGhostClip",
                                QJsonObject{ { "sourceClipId", 1 }, { "newStart", 4.0 } });
    EXPECT_TRUE(r.isError);
    EXPECT_LT(r.payload.toObject().value("code").toInt(), 0);
}

// ─── paintClips RPC ────────────────────────────────────────────────────

TEST(PaintClipsRpc, PaintClipsReturnsIdArray)
{
    AudioEngine engine;
    engine.initialize();

    auto srcResp = rpc(engine, "project.addMidiClip",
                       QJsonObject{ { "trackIndex", 0 }, { "start", 0.0 },
                                    { "duration", 4.0 }, { "name", "Src" } });
    int srcId = static_cast<int>(srcResp.toDouble());

    QJsonObject params{
        { "sourceClipIds", QJsonArray{ srcId } },
        { "originBeat", 0.0 },
        { "spacing", 4.0 },
        { "targetTrackIndex", 0 },
        { "count", 3 },
    };
    auto resp = rpc(engine, "project.paintClips", params);
    ASSERT_TRUE(resp.isArray());
    auto arr = resp.toArray();
    ASSERT_EQ(arr.size(), 3);
    for (const auto& v : arr)
    {
        int id = static_cast<int>(v.toDouble());
        EXPECT_GT(id, 0);
        EXPECT_NE(id, srcId);
    }
}

TEST(PaintClipsRpc, PaintClipsNonArraySourceIdsErrors)
{
    AudioEngine engine;
    engine.initialize();
    auto r = frontend::dispatch(engine, "project.paintClips",
                                QJsonObject{ { "sourceClipIds", 5 } });
    EXPECT_TRUE(r.isError);
}

TEST(PaintClipsRpc, PaintClipsMissingCountErrors)
{
    AudioEngine engine;
    engine.initialize();
    auto r = frontend::dispatch(engine, "project.paintClips",
                                QJsonObject{ { "sourceClipIds", QJsonArray{ 1 } },
                                             { "originBeat", 0.0 },
                                             { "spacing", 4.0 },
                                             { "targetTrackIndex", 0 } });
    EXPECT_TRUE(r.isError);
}

// ─── Snapshot JSON: isGhost/ghostSourceId always present ───────────────

TEST(GhostClipsRpc, NonGhostClipHasDefaultGhostFields)
{
    AudioEngine engine;
    engine.initialize();

    auto srcResp = rpc(engine, "project.addMidiClip",
                       QJsonObject{ { "trackIndex", 0 }, { "start", 0.0 },
                                    { "duration", 4.0 }, { "name", "Plain" } });
    int id = static_cast<int>(srcResp.toDouble());

    auto clip = findClipJson(rpc(engine, "read.snapshot").toObject(), id);
    EXPECT_FALSE(clip.value("isGhost").toBool(true));      // must exist & be false
    EXPECT_EQ(static_cast<int>(clip.value("ghostSourceId").toDouble(-999)), -1);
}
