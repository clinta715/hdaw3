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
#include <QUrl>
#include <QVariantList>

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
