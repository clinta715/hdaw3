// Integration tests for the WebSocket frontend server.
//
// These spin up a real FrontendServer on an OS-assigned port (port 0) and
// drive it with a QWebSocket client, exercising the JSON-RPC 2.0 round-trip
// end to end. The engine is fully initialized so the command/read interfaces
// are wired exactly as they will be in production headless mode.
//
// Determinism rules (per AGENTS.md testing section): no sleep() — use
// QSignalSpy::wait() / a connected QEventLoop for synchronization. The server
// binds to port 0 so each test gets a fresh free port and the tests are
// independent.

#include <gtest/gtest.h>

#include "engine/AudioEngine.h"
#include "frontend/FrontendServer.h"

#include <QtWebSockets/QWebSocket>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSignalSpy>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstring>
#include <vector>

namespace {

// A tiny JSON-RPC client used only by these tests. It keeps a persistent
// connection and collects every incoming text message, so push notifications
// (notify.meters / notify.transport / notify.treeChanged) and responses can
// interleave without being lost.
class TestClient {
public:
    bool connect(const QUrl& url) {
        QObject::connect(&socket_, &QWebSocket::textMessageReceived,
                         &socket_, [this](const QString& msg) {
            messages_.append(msg);
        });
        QSignalSpy connectedSpy(&socket_, &QWebSocket::connected);
        socket_.open(url);
        if (!connectedSpy.wait(2000)) return false;
        return socket_.state() == QAbstractSocket::ConnectedState;
    }

    void close() { socket_.close(); }

    // Abruptly drop the connection (like a killed client process) without the
    // graceful close handshake. The server sees an immediate disconnect.
    void abortConnection() { socket_.abort(); }

    // Send a JSON-RPC request and wait for the response with this id.
    // Push notifications arriving in the meantime are tolerated and skipped.
    QJsonObject call(int id, const QString& method, const QJsonValue& params = {},
                     int timeoutMs = 2000) {
        QJsonObject envelope;
        envelope.insert("jsonrpc", "2.0");
        envelope.insert("id", id);
        envelope.insert("method", method);
        if (!params.isNull()) envelope.insert("params", params);
        socket_.sendTextMessage(QString::fromUtf8(
            QJsonDocument(envelope).toJson(QJsonDocument::Compact)));

        // Poll the message queue, pumping the event loop in bounded slices.
        const int sliceMs = 20;
        int waited = 0;
        while (waited < timeoutMs) {
            for (int i = 0; i < messages_.size(); ++i) {
                auto obj = QJsonDocument::fromJson(messages_[i].toUtf8()).object();
                if (obj.contains("id") && obj.value("id").toInt(-1) == id) {
                    messages_.removeAt(i);
                    return obj;
                }
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, sliceMs);
            waited += sliceMs;
        }
        return {};  // timed out
    }

    // Send a JSON-RPC notification (no id). Returns immediately — the caller
    // may then drain push notifications or send a follow-up request.
    void notify(const QString& method, const QJsonValue& params = {}) {
        QJsonObject envelope;
        envelope.insert("jsonrpc", "2.0");
        envelope.insert("method", method);
        if (!params.isNull()) envelope.insert("params", params);
        socket_.sendTextMessage(QString::fromUtf8(
            QJsonDocument(envelope).toJson(QJsonDocument::Compact)));
    }

    // Drain and return all currently-queued messages (used to inspect push
    // notifications the server broadcasts independently of requests).
    QStringList drainMessages() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QStringList out;
        out.swap(messages_);
        return out;
    }

    // Wait for a specific notification method to arrive.
    // Returns true if found within the timeout.
    bool waitForNotification(const QString& method, int timeoutMs = 3000) {
        auto deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
        while (QDateTime::currentMSecsSinceEpoch() < deadline) {
            auto msgs = drainMessages();
            for (const auto& m : msgs) {
                auto obj = QJsonDocument::fromJson(m.toUtf8()).object();
                if (obj.value("method").toString() == method)
                    return true;
            }
        }
        return false;
    }

    // Wait for a specific notification method and return its `params` payload,
    // or a null QJsonValue on timeout. Like waitForNotification but captures the
    // payload so callers can assert on the delta contents.
    QJsonValue waitForNotificationParams(const QString& method, int timeoutMs = 3000) {
        auto deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
        while (QDateTime::currentMSecsSinceEpoch() < deadline) {
            auto msgs = drainMessages();
            for (const auto& m : msgs) {
                auto obj = QJsonDocument::fromJson(m.toUtf8()).object();
                if (obj.value("method").toString() == method)
                    return obj.value("params");
            }
        }
        return QJsonValue();
    }

private:
    QWebSocket socket_;
    QStringList messages_;
};

// RAII helper to scope an engine + server pair.
struct EngineAndServer {
    AudioEngine engine;
    frontend::FrontendServer* server = nullptr;
    quint16 port = 0;

    void setUp() {
        engine.initialize();
        server = new frontend::FrontendServer(engine);
        ASSERT_TRUE(server->start(0)) << "server failed to bind";
        port = server->port();
    }
    void tearDown() {
        if (server) { server->stop(); delete server; server = nullptr; }
        engine.shutdown();
    }
};

} // namespace

// Smoke test: server binds, client connects, a read returns the default
// project. Validates the full WebSocket → router → ReadModel → JSON path.
TEST(FrontendServer, SnapshotRoundTrip) {
    EngineAndServer s;
    s.setUp();
    auto expectedTracks = s.engine.getReadModel().getTrackCount();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    auto resp = client.call(1, "read.snapshot");
    ASSERT_FALSE(resp.isEmpty()) << "no response";
    ASSERT_FALSE(resp.contains("error")) << resp.value("error").toObject()
                                               .value("message").toString().toStdString();
    auto snap = resp.value("result").toObject();
    EXPECT_EQ(snap.value("tracks").toArray().size(), expectedTracks);

    client.close();
    s.tearDown();
}

// Mutation round-trip: addTrack returns the new index and the model changed.
TEST(FrontendServer, AddTrackMutation) {
    EngineAndServer s;
    s.setUp();
    int before = s.engine.getReadModel().getTrackCount();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    QJsonObject params;
    params.insert("name", "Synth");
    params.insert("color", static_cast<double>(0xFFAABBCC));
    auto resp = client.call(7, "project.addTrack", params);
    ASSERT_FALSE(resp.isEmpty());
    ASSERT_FALSE(resp.contains("error")) << resp.value("error").toObject()
                                               .value("message").toString().toStdString();
    ASSERT_TRUE(resp.value("result").isDouble()) << "expected numeric trackIndex";
    int newIndex = resp.value("result").toInt(-1);
    EXPECT_GE(newIndex, 0);

    auto snap = client.call(8, "read.snapshot").value("result").toObject();
    EXPECT_EQ(snap.value("tracks").toArray().size(), before + 1);

    client.close();
    s.tearDown();
}

// Property write round-trip: setTrackName + read.getTrack echoes the new name.
// Exercises the path the mixer / track-header UI will use heavily.
TEST(FrontendServer, SetTrackNameAndRead) {
    EngineAndServer s;
    s.setUp();
    ASSERT_GT(s.engine.getReadModel().getTrackCount(), 0);

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    QJsonObject setParams{ { "trackIndex", 0 }, { "name", "Lead" } };
    auto setResp = client.call(1, "project.setTrackName", setParams);
    ASSERT_FALSE(setResp.contains("error"));

    QJsonObject getParams{ { "trackIndex", 0 } };
    auto getResp = client.call(2, "read.getTrack", getParams);
    ASSERT_FALSE(getResp.contains("error"));
    EXPECT_EQ(getResp.value("result").toObject().value("name").toString().toStdString(),
              std::string("Lead"));

    client.close();
    s.tearDown();
}

// Unknown method → JSON-RPC error with code -32601 (MethodNotFound).
// Mirrors the contract mcp::McpServer exposes over stdio/HTTP.
TEST(FrontendServer, UnknownMethodReturnsError) {
    EngineAndServer s;
    s.setUp();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    auto resp = client.call(42, "project.doesNotExist");
    ASSERT_TRUE(resp.contains("error"));
    auto err = resp.value("error").toObject();
    EXPECT_EQ(err.value("code").toInt(), -32601);
    EXPECT_TRUE(err.value("message").toString().contains("unknown project method"));

    client.close();
    s.tearDown();
}

// Missing required parameter → JSON-RPC error with code -32602 (InvalidParams).
TEST(FrontendServer, MissingParamReturnsError) {
    EngineAndServer s;
    s.setUp();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    // setTrackName without a name is an error.
    QJsonObject badParams{ { "trackIndex", 0 } };
    auto resp = client.call(99, "project.setTrackName", badParams);
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp.value("error").toObject().value("code").toInt(), -32602);

    client.close();
    s.tearDown();
}

// New RPC method contract: audio.fm_synthImportSysex loads a DX7 .syx file
// into an FM synth FX slot. Exercises the full path the drop handler uses:
// addFxSlot → read.getFxSlots → audio.fm_synthImportSysex → live engine.
TEST(FrontendServer, FmSynthImportSysexRpc) {
    EngineAndServer s;
    s.setUp();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    // Add an FM synth slot on track 0 via the same RPC the UI/drop uses.
    QJsonObject addParams{ { "trackIndex", 0 }, { "fxType", "fm_synth" } };
    auto addResp = client.call(1, "project.addFxSlot", addParams);
    ASSERT_FALSE(addResp.contains("error")) << addResp.value("error").toObject()
                                                  .value("message").toString().toStdString();

    // Locate the slot index (robust to pre-existing slots).
    QJsonObject readParams{ { "trackIndex", 0 } };
    auto readResp = client.call(2, "read.getFxSlots", readParams);
    ASSERT_FALSE(readResp.contains("error"));
    int slotIndex = -1;
    for (const auto& v : readResp.value("result").toArray()) {
        auto o = v.toObject();
        if (o.value("fxType").toString() == "fm_synth") {
            slotIndex = o.value("slotIndex").toInt(-1);
            break;
        }
    }
    ASSERT_GE(slotIndex, 0) << "fm_synth slot not found";

    // Write a minimal single-voice DX7 SysEx file (algorithm 5, feedback 3,
    // name "E.PIANO") — same bytes the E2E test generates.
    std::vector<uint8_t> syx(163);
    syx[0] = 0xF0; syx[1] = 0x43; syx[2] = 0x00; syx[3] = 0x00;
    syx[4] = 0x00; syx[5] = 0x9B;                    // 155 bytes of voice data
    uint8_t voice[155] = {};
    voice[134] = 5;                                  // algorithm
    voice[135] = 3;                                  // feedback
    const char* name = "E.PIANO";
    for (int i = 0; i < 7; ++i) voice[145 + i] = static_cast<uint8_t>(name[i]);
    std::memcpy(syx.data() + 6, voice, 155);
    int sum = 0;
    for (int i = 6; i <= 160; ++i) sum += syx[i];
    syx[161] = static_cast<uint8_t>((~sum + 1) & 0x7F);  // checksum
    syx[162] = 0xF7;                                 // SysEx end

    auto syxFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_test_voice.syx");
    syxFile.replaceWithData(syx.data(), static_cast<int>(syx.size()));

    // Import via RPC.
    QJsonObject importParams{
        { "trackIndex", 0 },
        { "slotIndex", slotIndex },
        { "filePath", syxFile.getFullPathName().toStdString().c_str() },
    };
    auto importResp = client.call(3, "audio.fm_synthImportSysex", importParams);
    ASSERT_FALSE(importResp.contains("error")) << importResp.value("error").toObject()
                                                    .value("message").toString().toStdString();
    auto result = importResp.value("result").toObject();
    EXPECT_TRUE(result.value("ok").toBool());
    EXPECT_EQ(result.value("algorithm").toInt(), 5);
    EXPECT_TRUE(result.value("voiceName").toString().contains("E.PIANO"));

    // Verify the LIVE processor loaded the patch (not just the ReadModel).
    auto* proc = s.engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* track = proc->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_GT(static_cast<int>(chain.size()), slotIndex);
    auto* slot = chain[slotIndex].get();
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getType().toStdString(), "fm_synth");
    ASSERT_NE(slot->fmSynthEngine(), nullptr);

    syxFile.deleteFile();
    client.close();
    s.tearDown();
}

// 32-voice cartridge import: the response must carry the full parsed voice
// list (index/name/algorithm), totalVoices, and the resolved voiceIndex so the
// frontend can render a voice picker. Exercises the same path the drop handler
// will use: addFxSlot → read.getFxSlots → audio.fm_synthImportSysex → live
// engine.
TEST(FrontendServer, FmSynthImportSysexCartridgeVoices) {
    EngineAndServer s;
    s.setUp();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    // Add an FM synth slot on track 0 via the same RPC the UI/drop uses.
    QJsonObject addParams{ { "trackIndex", 0 }, { "fxType", "fm_synth" } };
    auto addResp = client.call(1, "project.addFxSlot", addParams);
    ASSERT_FALSE(addResp.contains("error")) << addResp.value("error").toObject()
                                                  .value("message").toString().toStdString();

    // Locate the slot index (robust to pre-existing slots).
    QJsonObject readParams{ { "trackIndex", 0 } };
    auto readResp = client.call(2, "read.getFxSlots", readParams);
    ASSERT_FALSE(readResp.contains("error"));
    int slotIndex = -1;
    for (const auto& v : readResp.value("result").toArray()) {
        auto o = v.toObject();
        if (o.value("fxType").toString() == "fm_synth") {
            slotIndex = o.value("slotIndex").toInt(-1);
            break;
        }
    }
    ASSERT_GE(slotIndex, 0) << "fm_synth slot not found";

    // Build a 4104-byte 32-voice cartridge: F0 43 00 09 20 00 + 4096 data
    // bytes + checksum + F7. Each 128-byte voice block sits at syx[6 + v*128].
    std::vector<uint8_t> syx(4104);
    syx[0] = 0xF0; syx[1] = 0x43; syx[2] = 0x00; syx[3] = 0x09;
    syx[4] = 0x20; syx[5] = 0x00;                    // 4096 bytes of voice data
    for (int i = 6; i < 4102; ++i)
        syx[i] = static_cast<uint8_t>(i & 0x7F);

    // Voice 0: name "BASS" (packed offsets 118-127), algorithm 31 (offset 110).
    const char* bassName = "BASS";
    for (int i = 0; i < 4; ++i) syx[6 + 118 + i] = static_cast<uint8_t>(bassName[i]);
    for (int i = 4; i < 10; ++i) syx[6 + 118 + i] = ' ';
    syx[6 + 110] = 31;

    // Voice 5: name "LEADX" (packed offsets 118-127), algorithm 7 (offset 110).
    const char* leadName = "LEADX";
    for (int i = 0; i < 5; ++i) syx[6 + 5 * 128 + 118 + i] = static_cast<uint8_t>(leadName[i]);
    for (int i = 5; i < 10; ++i) syx[6 + 5 * 128 + 118 + i] = ' ';
    syx[6 + 5 * 128 + 110] = 7;

    int sum = 0;
    for (int i = 6; i <= 4101; ++i) sum += syx[i];
    syx[4102] = static_cast<uint8_t>((~sum + 1) & 0x7F);  // checksum
    syx[4103] = 0xF7;                                     // SysEx end

    auto syxFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_test_cartridge.syx");
    syxFile.replaceWithData(syx.data(), static_cast<int>(syx.size()));

    // Import voice 5 from the cartridge via RPC.
    QJsonObject importParams{
        { "trackIndex", 0 },
        { "slotIndex", slotIndex },
        { "filePath", syxFile.getFullPathName().toStdString().c_str() },
        { "voiceIndex", 5 },
    };
    auto importResp = client.call(3, "audio.fm_synthImportSysex", importParams);
    ASSERT_FALSE(importResp.contains("error")) << importResp.value("error").toObject()
                                                    .value("message").toString().toStdString();
    auto result = importResp.value("result").toObject();
    EXPECT_TRUE(result.value("ok").toBool());
    EXPECT_EQ(result.value("totalVoices").toInt(), 32);
    EXPECT_EQ(result.value("voiceIndex").toInt(), 5);
    EXPECT_TRUE(result.value("voiceName").toString().contains("LEADX"));

    auto voices = result.value("voices").toArray();
    ASSERT_EQ(static_cast<int>(voices.size()), 32);
    EXPECT_EQ(voices.at(0).toObject().value("name").toString(), "BASS");
    EXPECT_EQ(voices.at(5).toObject().value("name").toString(), "LEADX");
    EXPECT_EQ(voices.at(5).toObject().value("algorithm").toInt(), 7);

    // Verify the LIVE processor loaded the patch (not just the ReadModel).
    auto* proc = s.engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* track = proc->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_GT(static_cast<int>(chain.size()), slotIndex);
    auto* slot = chain[slotIndex].get();
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->getType().toStdString(), "fm_synth");
    ASSERT_NE(slot->fmSynthEngine(), nullptr);

    syxFile.deleteFile();
    client.close();
    s.tearDown();
}

// JSON-RPC notification (no id): fire-and-forget. The server must not send a
// response. Verify by sending a notification then a request; confirm only the
// request gets a response (the notification may produce a push notification
// via the tree watcher, but never a response with an id).
TEST(FrontendServer, NotificationGetsNoResponse) {
    EngineAndServer s;
    s.setUp();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    // Send a notification (transport.toggleLoop has side effects but no id).
    client.notify("transport.toggleLoop");

    // Now send a real request and verify we get exactly the response for it.
    auto resp = client.call(5, "read.getTrackCount");
    ASSERT_FALSE(resp.isEmpty());
    EXPECT_EQ(resp.value("id").toInt(), 5);
    EXPECT_TRUE(resp.value("result").isDouble());

    client.close();
    s.tearDown();
}

// Push channel: a mutation broadcasts notify.treeChanged. Verifies the
// FrontendTreeWatcher → broadcastNotification path end-to-end.
TEST(FrontendServer, MutationBroadcastsTreeChanged) {
    EngineAndServer s;
    s.setUp();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    // Drain any pre-existing push notifications.
    client.drainMessages();

    // Trigger a model mutation. Build params without initializer-list to
    // avoid MSVC ambiguous-conversion issues with QJsonObject.
    QJsonObject params;
    params.insert("name", "Pushed");
    auto resp = client.call(1, "project.addTrack", params);
    ASSERT_FALSE(resp.contains("error"));

    // Wait for the tree watcher's debounced notify.treeChanged (~16ms +
    // WebSocket in-process delivery). Times out after 3s if the broadcast
    // never arrives.
    EXPECT_TRUE(client.waitForNotification("notify.treeChanged"))
        << "did not receive notify.treeChanged after mutation";

    client.close();
    s.tearDown();
}

// Linchpin of incremental tree sync: a real clip add/remove must propagate to
// the root watcher and broadcast as an incremental delta (not a fullSync).
TEST(FrontendServer, ClipAddRemoveBroadcastsIncrementalDelta) {
    EngineAndServer s;
    s.setUp();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    // Drain any push notifications queued during connection setup so the first
    // treeChanged we capture is provably from our mutation. (The server sends
    // nothing on connect, but meter/transport broadcasts may already be queued.)
    client.drainMessages();

    // --- Add a MIDI clip -> expect a delta with clipsUpserted ---
    // Track 0 in the default project already has an (empty) CLIP_LIST, so this
    // is a pure CLIP child-add -> upsertClip -> incremental delta (no fullSync).
    QJsonObject addParams;
    addParams.insert("trackIndex", 0);
    addParams.insert("start", 0.0);
    addParams.insert("duration", 4.0);
    addParams.insert("name", "DeltaTestClip");
    auto addResp = client.call(1, "project.addMidiClip", addParams);
    ASSERT_FALSE(addResp.isEmpty());
    ASSERT_FALSE(addResp.contains("error"))
        << addResp.value("error").toObject().value("message").toString().toStdString();
    const int newClipId = addResp.value("result").toInt(-1);
    ASSERT_GT(newClipId, 0) << "addMidiClip should return the new clip id";

    QJsonValue addNotify = client.waitForNotificationParams("notify.treeChanged");
    ASSERT_TRUE(addNotify.isObject()) << "no notify.treeChanged after addMidiClip";
    QJsonObject addDelta = addNotify.toObject();
    EXPECT_FALSE(addDelta.value("fullSync").toBool(false))
        << "clip add should be an incremental delta, got: "
        << QString::fromUtf8(QJsonDocument(addDelta).toJson(QJsonDocument::Compact)).toStdString();
    bool foundUpserted = false;
    for (const auto& c : addDelta.value("clipsUpserted").toArray())
        if (c.toObject().value("clipId").toInt(-1) == newClipId) foundUpserted = true;
    EXPECT_TRUE(foundUpserted) << "clipsUpserted should contain the new clip id";

    // --- Remove that clip -> expect a delta with clipsRemoved ---
    QJsonObject rmParams;
    rmParams.insert("clipId", newClipId);
    auto rmResp = client.call(2, "project.removeClip", rmParams);
    ASSERT_FALSE(rmResp.isEmpty());
    ASSERT_FALSE(rmResp.contains("error"))
        << rmResp.value("error").toObject().value("message").toString().toStdString();

    QJsonValue rmNotify = client.waitForNotificationParams("notify.treeChanged");
    ASSERT_TRUE(rmNotify.isObject()) << "no notify.treeChanged after removeClip";
    QJsonObject rmDelta = rmNotify.toObject();
    EXPECT_FALSE(rmDelta.value("fullSync").toBool(false))
        << "clip remove should be an incremental delta, got: "
        << QString::fromUtf8(QJsonDocument(rmDelta).toJson(QJsonDocument::Compact)).toStdString();
    bool foundRemoved = false;
    for (const auto& id : rmDelta.value("clipsRemoved").toArray())
        if (id.toInt(-1) == newClipId) foundRemoved = true;
    EXPECT_TRUE(foundRemoved) << "clipsRemoved should contain the removed clip id";

    client.close();
    s.tearDown();
}

// New sampler RPC namespace contract (sampler.*): add a sampler slot, load a
// sample, switch to slice mode, detect slices, read state back, and audition a
// slice. Exercises the full router path the SamplerEditor uses and asserts the
// LIVE engine (sound slicePoints + mode) after the rebuilds — not just the
// ReadModel.
TEST(FrontendServer, SamplerRpcFamily) {
    EngineAndServer s;
    s.setUp();

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));

    // 1. Add a sampler FX slot on track 0 via the same RPC the UI uses.
    QJsonObject addParams{ { "trackIndex", 0 }, { "fxType", "sampler" } };
    auto addResp = client.call(1, "project.addFxSlot", addParams);
    ASSERT_FALSE(addResp.contains("error")) << addResp.value("error").toObject()
                                                  .value("message").toString().toStdString();

    // Locate the slot index (robust to pre-existing slots).
    QJsonObject readParams{ { "trackIndex", 0 } };
    auto readResp = client.call(2, "read.getFxSlots", readParams);
    ASSERT_FALSE(readResp.contains("error"));
    int slotIndex = -1;
    for (const auto& v : readResp.value("result").toArray()) {
        auto o = v.toObject();
        if (o.value("fxType").toString() == "sampler") {
            slotIndex = o.value("slotIndex").toInt(-1);
            break;
        }
    }
    ASSERT_GE(slotIndex, 0) << "sampler slot not found";

    // Close the coalesced async routing-rebuild window from addFxSlot before
    // the synchronous rebuildTrackFX calls below (audio_pool_dedup_test
    // pattern) so the live-processor reads cannot race a RoutingManager swap.
    s.engine.drainPendingRoutingRebuild();

    // 2. Write a small mono WAV whose signal is a full-buffer rising ramp, so
    // the transient detector reliably finds slice boundaries (the envelope
    // follower tracks the rise and fires above threshold).
    const int len = 8000;
    const double sr = 44100.0;
    auto wavFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_sampler_rpc_test.wav");
    wavFile.deleteFile();
    {
        juce::WavAudioFormat wav;
        auto* fileOut = new juce::FileOutputStream(wavFile);
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(fileOut, sr, 1, 16, {}, 0));
        if (writer == nullptr) delete fileOut;
        ASSERT_NE(writer, nullptr);
        juce::AudioBuffer<float> data(1, len);
        for (int i = 0; i < len; ++i)
            data.setSample(0, i, static_cast<float>(i) / static_cast<float>(len));
        writer->writeFromAudioSampleBuffer(data, 0, len);
        writer->flush();
    }

    // 3. Load the sample.
    QJsonObject setSampleParams{
        { "trackIndex", 0 },
        { "slotIndex", slotIndex },
        { "filePath", wavFile.getFullPathName().toStdString().c_str() },
    };
    auto setSampleResp = client.call(3, "sampler.setSample", setSampleParams);
    ASSERT_FALSE(setSampleResp.contains("error")) << setSampleResp.value("error").toObject()
                                                        .value("message").toString().toStdString();

    // 4. Switch to slice mode.
    QJsonObject setModeParams{ { "trackIndex", 0 }, { "slotIndex", slotIndex }, { "mode", "slice" } };
    auto setModeResp = client.call(4, "sampler.setMode", setModeParams);
    ASSERT_FALSE(setModeResp.contains("error")) << setModeResp.value("error").toObject()
                                                      .value("message").toString().toStdString();

    // 5. Configure transient detection.
    QJsonObject setSliceParams{ { "trackIndex", 0 }, { "slotIndex", slotIndex },
                                { "sliceMode", "transient" }, { "sliceSensitivity", 0.3 } };
    auto setSliceResp = client.call(5, "sampler.setSliceMode", setSliceParams);
    ASSERT_FALSE(setSliceResp.contains("error")) << setSliceResp.value("error").toObject()
                                                       .value("message").toString().toStdString();

    s.engine.drainPendingRoutingRebuild();

    // 6. Detect slices.
    QJsonObject detectParams{ { "trackIndex", 0 }, { "slotIndex", slotIndex } };
    auto detectResp = client.call(6, "sampler.detectSlices", detectParams);
    ASSERT_FALSE(detectResp.contains("error")) << detectResp.value("error").toObject()
                                                     .value("message").toString().toStdString();
    auto detectResult = detectResp.value("result").toObject();
    EXPECT_TRUE(detectResult.value("ok").toBool());
    const int totalSlices = detectResult.value("totalSlices").toInt();
    EXPECT_GE(totalSlices, 1) << "rising ramp should yield at least one slice";
    auto slicePoints = detectResult.value("slicePoints").toArray();
    EXPECT_EQ(static_cast<int>(slicePoints.size()), totalSlices + 1);

    s.engine.drainPendingRoutingRebuild();

    // 7. Read the state back — the slice fields reflect the detection.
    QJsonObject stateParams{ { "trackIndex", 0 }, { "slotIndex", slotIndex } };
    auto stateResp = client.call(7, "sampler.getState", stateParams);
    ASSERT_FALSE(stateResp.contains("error")) << stateResp.value("error").toObject()
                                                    .value("message").toString().toStdString();
    auto state = stateResp.value("result").toObject();
    EXPECT_EQ(state.value("mode").toString().toStdString(), std::string("slice"));
    EXPECT_EQ(state.value("sliceMode").toString().toStdString(), std::string("transient"));
    EXPECT_FALSE(state.value("slicePoints").toArray().isEmpty());

    // 8-9. Audition a slice. The engine stages the rebuilt sound into the
    // SamplerEngine's pendingSound_ and only adopts it into activeSound_ (what
    // currentSound()/triggerSamplerSlice read) at the next audio-thread render.
    // Start playback — with the track volume at 0 (NOT mute, which early-outs
    // the track before the FX chain renders) — so the device callback renders
    // a block, adopts the swap, and the output stays silent.
    QJsonObject volParams{ { "trackIndex", 0 }, { "volume", 0.0 } };
    client.call(8, "project.setTrackVolume", volParams);
    auto playResp = client.call(9, "transport.play");
    ASSERT_FALSE(playResp.contains("error")) << playResp.value("error").toObject()
                                                   .value("message").toString().toStdString();

    auto* proc = s.engine.getMainProcessor();
    ASSERT_NE(proc, nullptr);
    auto* track = proc->getTrack(0);
    ASSERT_NE(track, nullptr);
    auto& chain = track->getFXChain();
    ASSERT_GT(static_cast<int>(chain.size()), slotIndex);
    auto* slot = chain[slotIndex].get();
    ASSERT_NE(slot, nullptr);
    auto* sampler = slot->samplerEngineForTest();
    ASSERT_NE(sampler, nullptr);

    // Poll bounded for the audio thread to adopt the staged sound.
    auto deadline = QDateTime::currentMSecsSinceEpoch() + 3000;
    while (sampler->currentSound() == nullptr && QDateTime::currentMSecsSinceEpoch() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    ASSERT_NE(sampler->currentSound(), nullptr)
        << "audio thread never adopted the staged sampler sound";

    QJsonObject trigParams{ { "trackIndex", 0 }, { "slotIndex", slotIndex }, { "sliceIndex", 0 } };
    auto trigResp = client.call(10, "sampler.triggerSlice", trigParams);
    ASSERT_FALSE(trigResp.contains("error")) << trigResp.value("error").toObject()
                                                   .value("message").toString().toStdString();
    auto trigResult = trigResp.value("result").toObject();
    EXPECT_TRUE(trigResult.value("ok").toBool());
    EXPECT_GE(trigResult.value("totalSlices").toInt(), 1);

    // The LIVE engine's adopted sound carries the detected slice boundaries.
    EXPECT_GE(static_cast<int>(sampler->currentSound()->slicePoints.size()), 2);

    client.call(11, "transport.stop");

    // 12-13. setParam property-path round-trip (Mono checkbox contract): write
    // the named "mono" slot property and read it back through getState. The
    // rebuild applies it to the live engine via loadSamplerState.
    QJsonObject monoParams{ { "trackIndex", 0 }, { "slotIndex", slotIndex },
                            { "property", "mono" }, { "value", true } };
    auto monoResp = client.call(12, "sampler.setParam", monoParams);
    ASSERT_FALSE(monoResp.contains("error")) << monoResp.value("error").toObject()
                                                   .value("message").toString().toStdString();

    auto monoStateResp = client.call(13, "sampler.getState", stateParams);
    ASSERT_FALSE(monoStateResp.contains("error")) << monoStateResp.value("error").toObject()
                                                         .value("message").toString().toStdString();
    EXPECT_TRUE(monoStateResp.value("result").toObject().value("mono").toBool())
        << "sampler.setParam property:mono should be reflected in getState";

    wavFile.deleteFile();
    client.close();
    s.tearDown();
}
// Kill-switch: with HDAW_FORCE_FULL_SYNC armed, a change that would normally
// broadcast an incremental delta (a pure clip add) is routed to a fullSync
// instead, so the delta path can be disabled in the field if drift surfaces.
TEST(FrontendServer, ForceFullSyncKillSwitchBroadcastsFullSync) {
    // The watcher reads the flag once at construction (inside setUp), so set it
    // before setUp and clear it immediately after to avoid leaking into other
    // tests.
    qputenv("HDAW_FORCE_FULL_SYNC", "1");
    EngineAndServer s;
    s.setUp();
    qunsetenv("HDAW_FORCE_FULL_SYNC");

    TestClient client;
    ASSERT_TRUE(client.connect(QUrl(QString("ws://127.0.0.1:%1").arg(s.port))));
    client.drainMessages();

    // A pure CLIP child-add is an incremental delta when the kill-switch is off
    // (see ClipAddRemoveBroadcastsIncrementalDelta). With it armed -> fullSync.
    QJsonObject addParams;
    addParams.insert("trackIndex", 0);
    addParams.insert("start", 0.0);
    addParams.insert("duration", 4.0);
    addParams.insert("name", "KillSwitchClip");
    auto addResp = client.call(1, "project.addMidiClip", addParams);
    ASSERT_FALSE(addResp.isEmpty());
    ASSERT_FALSE(addResp.contains("error"))
        << addResp.value("error").toObject().value("message").toString().toStdString();

    QJsonValue notify = client.waitForNotificationParams("notify.treeChanged");
    ASSERT_TRUE(notify.isObject()) << "no notify.treeChanged after addMidiClip";
    EXPECT_TRUE(notify.toObject().value("fullSync").toBool(false))
        << "HDAW_FORCE_FULL_SYNC should force fullSync=true, got: "
        << QString::fromUtf8(QJsonDocument(notify.toObject()).toJson(QJsonDocument::Compact)).toStdString();

    client.close();
    s.tearDown();
}

// Regression: a client killed mid-export must not crash the server. The
// export.audio handler blocks inside a nested QEventLoop (Router_Export.cpp
// loop.exec()); a disconnect during it runs the server's `disconnected`
// lambda re-entrantly, and the socket's DeferredDelete is processed by the
// nested loop — so the raw `socket` pointer held across the dispatch is freed
// before the response send. The guarded send (QPointer) turns that
// use-after-free into a no-op. Without the fix this test crashes the engine.
TEST(FrontendServer, DisconnectDuringExportDoesNotCrash) {
    EngineAndServer env;
    env.setUp();

    TestClient c1;
    ASSERT_TRUE(c1.connect(QUrl(QString("ws://127.0.0.1:%1").arg(env.port))));

    // Abruptly kill the client shortly after the export handler enters its
    // nested event loop. The timer fires from WITHIN that nested loop, so the
    // disconnect + deleteLater + DeferredDelete all run while handleOneMessage
    // is still on the stack. (A graceful close() does not reproduce the bug —
    // the close handshake doesn't finish inside the export window; only an
    // abrupt kill, like a killed browser tab, tears the connection down.)
    QTimer closer;
    closer.setSingleShot(true);
    closer.setInterval(200);
    QObject::connect(&closer, &QTimer::timeout, [&]() { c1.abortConnection(); });
    closer.start();

    auto outFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("hdaw_fs_disconnect_test.wav");
    outFile.deleteFile();

    QJsonObject params;
    params.insert("outputPath", QString::fromUtf8(outFile.getFullPathName().toRawUTF8()));
    params.insert("format", "wav");
    params.insert("sampleRate", 44100.0);
    params.insert("bitDepth", 16);
    params.insert("start", 0.0);
    params.insert("end", 60.0);

    // Start a real export of the default (tiny, empty) project. The handler
    // blocks inside loop.exec() for the export duration; the timer above
    // closes the socket mid-way, so the response is lost. That is expected:
    // do not assert on it.
    c1.call(1001, "export.audio", params, 5000);

    // The export handler excludes socket notifiers while it runs, so the
    // server cannot accept new connections until it has fully unwound. Pump
    // the loop until the engine reports the export finished (and the handler
    // has returned), then connect the fresh client.
    {
        auto* proc = env.engine.getMainProcessor();
        ASSERT_NE(proc, nullptr);
        auto deadline = QDateTime::currentMSecsSinceEpoch() + 30000;
        while (proc->isExporting() && QDateTime::currentMSecsSinceEpoch() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        EXPECT_FALSE(proc->isExporting())
            << "export never finished after disconnect-during-export";
    }

    // The server must have survived: a brand-new client can connect, and a
    // trivial request round-trips.
    TestClient c2;
    ASSERT_TRUE(c2.connect(QUrl(QString("ws://127.0.0.1:%1").arg(env.port))));

    auto resp = c2.call(2001, "export.isExporting", {}, 5000);
    EXPECT_FALSE(resp.isEmpty()) << "server died after disconnect-during-export";
    if (!resp.isEmpty())
        EXPECT_FALSE(resp.value("result").toBool());

    c2.close();
    outFile.deleteFile();
    env.tearDown();
}
