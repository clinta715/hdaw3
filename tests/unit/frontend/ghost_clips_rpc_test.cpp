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

// ─── removeClips RPC ──────────────────────────────────────────────────

TEST(RemoveClipsRpc, RemovesMultipleClips)
{
    AudioEngine engine;
    engine.initialize();

    auto addClip = [&](const char* name, double start) -> int {
        return static_cast<int>(rpc(engine, "project.addMidiClip",
            QJsonObject{ { "trackIndex", 0 }, { "start", start },
                         { "duration", 4.0 }, { "name", name } }).toDouble());
    };

    int a = addClip("A", 0.0);
    int b = addClip("B", 8.0);
    int c = addClip("C", 16.0);
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);
    ASSERT_GT(c, 0);

    auto before = rpc(engine, "read.snapshot").toObject().value("clips").toArray();
    int beforeCount = before.size();

    rpc(engine, "project.removeClips",
        QJsonObject{ { "clipIds", QJsonArray{ a, c } } });

    auto after = rpc(engine, "read.snapshot").toObject().value("clips").toArray();
    EXPECT_EQ(after.size(), beforeCount - 2);

    // Verify the remaining clip is B.
    bool foundB = false;
    for (const auto& clip : after) {
        if (clip.toObject().value("clipId").toInt() == b) foundB = true;
    }
    EXPECT_TRUE(foundB);
}

TEST(RemoveClipsRpc, EmptyArrayNoOp)
{
    AudioEngine engine;
    engine.initialize();

    auto before = rpc(engine, "read.snapshot").toObject().value("clips").toArray();

    frontend::dispatch(engine, "project.removeClips",
        QJsonObject{ { "clipIds", QJsonArray{} } });

    auto after = rpc(engine, "read.snapshot").toObject().value("clips").toArray();
    EXPECT_EQ(after.size(), before.size());
}

// ─── addClips RPC ─────────────────────────────────────────────────────

TEST(AddClipsRpc, AddsMultipleClips)
{
    AudioEngine engine;
    engine.initialize();

    auto resp = rpc(engine, "project.addClips",
        QJsonObject{
            { "trackIndex", 0 },
            { "starts", QJsonArray{ 0.0, 4.0, 8.0 } },
            { "durations", QJsonArray{ 2.0, 2.0, 4.0 } },
            { "names", QJsonArray{ "A", "B", "C" } },
        });
    ASSERT_TRUE(resp.isArray());
    auto arr = resp.toArray();
    ASSERT_EQ(arr.size(), 3);
    for (const auto& v : arr) EXPECT_GT(v.toInt(), 0);

    // Verify clips exist in snapshot.
    auto snapshot = rpc(engine, "read.snapshot").toObject();
    auto clips = snapshot.value("clips").toArray();
    for (const auto& v : arr) {
        int id = v.toInt();
        bool found = false;
        for (const auto& c : clips) {
            if (c.toObject().value("clipId").toInt() == id) { found = true; break; }
        }
        EXPECT_TRUE(found) << "clipId " << id << " not found in snapshot";
    }
}

TEST(AddClipsRpc, EmptyArrayReturnsEmpty)
{
    AudioEngine engine;
    engine.initialize();

    auto resp = rpc(engine, "project.addClips",
        QJsonObject{
            { "trackIndex", 0 },
            { "starts", QJsonArray{} },
            { "durations", QJsonArray{} },
            { "names", QJsonArray{} },
        });
    ASSERT_TRUE(resp.isArray());
    EXPECT_EQ(resp.toArray().size(), 0);
}

TEST(AddClipsRpc, MismatchedArrayLengthsErrors)
{
    AudioEngine engine;
    engine.initialize();

    auto r = frontend::dispatch(engine, "project.addClips",
        QJsonObject{
            { "trackIndex", 0 },
            { "starts", QJsonArray{ 0.0 } },
            { "durations", QJsonArray{ 2.0, 4.0 } },
            { "names", QJsonArray{ "A" } },
        });
    EXPECT_TRUE(r.isError);
}

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

// ─── duplicateClips RPC ───────────────────────────────────────────────

TEST(DuplicateClipsRpc, ReturnsIdArray)
{
    AudioEngine engine;
    engine.initialize();

    auto addClip = [&](const char* name, double start) -> int {
        return static_cast<int>(rpc(engine, "project.addMidiClip",
            QJsonObject{ { "trackIndex", 0 }, { "start", start },
                         { "duration", 4.0 }, { "name", name } }).toDouble());
    };

    int a = addClip("A", 0.0);
    int b = addClip("B", 8.0);
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    auto resp = rpc(engine, "project.duplicateClips",
        QJsonObject{
            { "clipIds", QJsonArray{ a, b } },
            { "newStarts", QJsonArray{ 16.0, 24.0 } },
            { "newTrackIndices", QJsonArray{ 0, 0 } },
        });
    ASSERT_TRUE(resp.isArray());
    auto arr = resp.toArray();
    ASSERT_EQ(arr.size(), 2);
    EXPECT_GT(arr[0].toInt(), 0);
    EXPECT_GT(arr[1].toInt(), 0);
    EXPECT_NE(arr[0].toInt(), a);
    EXPECT_NE(arr[1].toInt(), b);
}

TEST(DuplicateClipsRpc, EmptyArrayReturnsEmpty)
{
    AudioEngine engine;
    engine.initialize();

    auto resp = rpc(engine, "project.duplicateClips",
        QJsonObject{
            { "clipIds", QJsonArray{} },
            { "newStarts", QJsonArray{} },
            { "newTrackIndices", QJsonArray{} },
        });
    ASSERT_TRUE(resp.isArray());
    EXPECT_EQ(resp.toArray().size(), 0);
}

TEST(DuplicateClipsRpc, MismatchedArrayLengthsErrors)
{
    AudioEngine engine;
    engine.initialize();

    auto r = frontend::dispatch(engine, "project.duplicateClips",
        QJsonObject{
            { "clipIds", QJsonArray{ 1 } },
            { "newStarts", QJsonArray{ 0.0, 4.0 } },
            { "newTrackIndices", QJsonArray{ 0 } },
        });
    EXPECT_TRUE(r.isError);
}

TEST(DuplicateClipsRpc, InvalidClipIdReturnsNegativeOne)
{
    AudioEngine engine;
    engine.initialize();

    auto resp = rpc(engine, "project.duplicateClips",
        QJsonObject{
            { "clipIds", QJsonArray{ 99999 } },
            { "newStarts", QJsonArray{ 0.0 } },
            { "newTrackIndices", QJsonArray{ 0 } },
        });
    ASSERT_TRUE(resp.isArray());
    auto arr = resp.toArray();
    ASSERT_EQ(arr.size(), 1);
    EXPECT_EQ(arr[0].toInt(), -1);
}

TEST(DuplicateClipsRpc, CrossTrackDuplicate)
{
    AudioEngine engine;
    engine.initialize();

    auto addResp = rpc(engine, "project.addMidiClip",
        QJsonObject{ { "trackIndex", 0 }, { "start", 0.0 },
                     { "duration", 4.0 }, { "name", "X" } });
    int srcId = static_cast<int>(addResp.toDouble());

    // Add a second track
    rpc(engine, "project.addTrack", QJsonObject{ { "name", "B" } });

    auto resp = rpc(engine, "project.duplicateClips",
        QJsonObject{
            { "clipIds", QJsonArray{ srcId } },
            { "newStarts", QJsonArray{ 16.0 } },
            { "newTrackIndices", QJsonArray{ 1 } },
        });
    ASSERT_TRUE(resp.isArray());
    auto arr = resp.toArray();
    ASSERT_EQ(arr.size(), 1);
    EXPECT_GT(arr[0].toInt(), 0);

    // Verify the duplicate is on track 1
    auto snapshot = rpc(engine, "read.snapshot").toObject();
    auto clips = snapshot.value("clips").toArray();
    bool found = false;
    for (const auto& c : clips) {
        if (c.toObject().value("clipId").toInt() == arr[0].toInt()) {
            EXPECT_EQ(c.toObject().value("trackIndex").toInt(), 1);
            EXPECT_NEAR(c.toObject().value("startBeat").toDouble(), 16.0, 0.01);
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ─── moveClips RPC ────────────────────────────────────────────────────

TEST(MoveClipsRpc, MovesClipsAtomically)
{
    AudioEngine engine;
    engine.initialize();

    auto addClip = [&](const char* name, double start) -> int {
        return static_cast<int>(rpc(engine, "project.addMidiClip",
            QJsonObject{ { "trackIndex", 0 }, { "start", start },
                         { "duration", 4.0 }, { "name", name } }).toDouble());
    };

    int a = addClip("A", 0.0);
    int b = addClip("B", 8.0);
    ASSERT_GT(a, 0);
    ASSERT_GT(b, 0);

    rpc(engine, "project.moveClips",
        QJsonObject{
            { "clipIds", QJsonArray{ a, b } },
            { "newStarts", QJsonArray{ 16.0, 24.0 } },
            { "newTrackIndices", QJsonArray{ 0, 0 } },
        });

    auto snapshot = rpc(engine, "read.snapshot").toObject();
    auto clips = snapshot.value("clips").toArray();
    for (const auto& c : clips) {
        int id = c.toObject().value("clipId").toInt();
        if (id == a)
            EXPECT_NEAR(c.toObject().value("startBeat").toDouble(), 16.0, 0.01);
        if (id == b)
            EXPECT_NEAR(c.toObject().value("startBeat").toDouble(), 24.0, 0.01);
    }
}

TEST(MoveClipsRpc, EmptyArrayNoOp)
{
    AudioEngine engine;
    engine.initialize();

    // Should not crash
    frontend::dispatch(engine, "project.moveClips",
        QJsonObject{
            { "clipIds", QJsonArray{} },
            { "newStarts", QJsonArray{} },
            { "newTrackIndices", QJsonArray{} },
        });
}

TEST(MoveClipsRpc, MismatchedArrayLengthsErrors)
{
    AudioEngine engine;
    engine.initialize();

    auto r = frontend::dispatch(engine, "project.moveClips",
        QJsonObject{
            { "clipIds", QJsonArray{ 1 } },
            { "newStarts", QJsonArray{ 0.0, 4.0 } },
            { "newTrackIndices", QJsonArray{ 0 } },
        });
    EXPECT_TRUE(r.isError);
}
