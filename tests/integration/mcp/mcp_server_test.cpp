#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpTransportHttp.h"
#include "mcp/McpJsonRpc.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QUrl>
#include <thread>

namespace {
QJsonObject parseOne(const QByteArray& buf) {
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

// Find the first response line (object with an "id" field) in a multi-line
// buffer. Notifications ("method" without "id") are skipped.
QJsonObject parseResponse(const QByteArray& buf) {
    int start = 0;
    while (start < buf.size()) {
        int nl = buf.indexOf('\n', start);
        QByteArray line = (nl >= 0) ? buf.mid(start, nl - start) : buf.mid(start);
        start = (nl >= 0) ? nl + 1 : buf.size();
        QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        QJsonObject obj = QJsonDocument::fromJson(trimmed).object();
        if (obj.contains("id")) return obj;
    }
    return {};
}
}

// HTTP round-trip: POST a JSON-RPC request to the running TransportHttp and
// read the response. Exercises the full HTTP → McpServer::dispatchRequest →
// HTTP response path, replacing the v1 sync stub.
//
// This test is placed FIRST in the McpServer test suite because the
// integration tests that follow (FxAddRemoveBypass and the
// export_audio tests) call engine.initialize(), which on Windows brings up
// a JUCE WASAPI audio device. The teardown of that device across test
// boundaries can leave stale socket-notifier state in Qt's event loop
// that, when interleaved with a fresh QNetworkAccessManager, crashes
// with SEH 0xc0000005. Running this test first avoids the crash for a
// single run of the test binary.
//
// The transport binds to 127.0.0.1:18765; the unit smoke test
// (HttpTransport.StartStopLifecycle) uses a different port so the two
// can coexist in the same test binary.
TEST(McpServer, HttpRoundTrip) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportHttp t(18765);
    ASSERT_TRUE(t.start(&s)) << "start failed: " << t.lastError().toStdString();

    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl("http://127.0.0.1:18765/mcp"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";

    QEventLoop loop;
    QObject::connect(&nam, &QNetworkAccessManager::finished, &loop, &QEventLoop::quit);
    QNetworkReply* reply = nam.post(req, body);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    ASSERT_EQ(reply->error(), QNetworkReply::NoError)
        << "HTTP error: " << reply->errorString().toStdString();
    auto resp = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    t.stop();

    EXPECT_EQ(resp.value("jsonrpc").toString().toStdString(), std::string("2.0"));
    EXPECT_EQ(resp.value("id").toInt(), 1);
    EXPECT_TRUE(resp.value("result").isObject());
    EXPECT_TRUE(resp.value("error").isUndefined() || resp.value("error").isNull());
}

TEST(McpServer, InitializeAndList) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
    mcp::TransportLoopback tp;
    tp.start(&s);
    s.setTransport(&tp);
    s.start();

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_EQ(r.value("id").toInt(), 1);
    EXPECT_FALSE(r.value("result").toObject().value("capabilities").toObject().value("tools").isUndefined());

    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto listed = parseOne(out).value("result").toObject().value("tools").toArray();
    EXPECT_GE(listed.size(), 12); // 8 read + 2 transport + 2 undo (Phase 1 v1)
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, GetProjectSummary) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"get_project_summary","arguments":{}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_TRUE(r.value("result").toObject().value("content").toArray()
                .at(0).toObject().value("text").toString().contains("tracks="));
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, UndoAddThenUndoRemoves) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"undo","arguments":{"count":1}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out).value("result").toObject();
    EXPECT_FALSE(r.value("isError").toBool(true));
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, HandlerRunsOnMainThread) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();
    std::thread::id mainTid = std::this_thread::get_id();
    s.registerTool({"whereami","test", QJsonObject{{"type","object"}},
        [mainTid](const QJsonObject&) {
            return mcp::McpToolResult::text(
                (std::this_thread::get_id() == mainTid) ? "main" : "other");
        }});
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"whereami","arguments":{}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto txt = parseOne(out).value("result").toObject()
                  .value("content").toArray().at(0).toObject().value("text").toString();
    EXPECT_EQ(txt, QString("main"));
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, FxAddRemoveBypass) {
    AudioEngine engine;
    // initialize() builds the routing manager and instantiates Tracks with
    // back-pointers (setProjectContext) into the project model. The FX
    // mutation methods require those back-pointers to be live.
    engine.initialize();

    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

    auto* tr0 = engine.getMainProcessor()->getTrack(0);
    ASSERT_NE(tr0, nullptr);
    EXPECT_EQ(tr0->getNumFXSlots(), 0);

    auto callTool = [&](int id, const char* name, const char* args) {
        tp.drainOutgoing();
        QString req = QString(R"({"jsonrpc":"2.0","id":%1,"method":"tools/call",)"
                              R"("params":{"name":"%2","arguments":%3}})")
                          .arg(id).arg(name).arg(args);
        tp.pumpIncoming(req.toUtf8());
        QByteArray out; EXPECT_TRUE(tp.waitForOutgoing(500, &out));
        return parseOne(out);
    };

    auto text = [](const QJsonObject& r) -> QString {
        return r.value("result").toObject()
                .value("content").toArray().at(0).toObject()
                .value("text").toString();
    };

    // add_fx: eq at position 0
    auto r = callTool(1, "add_fx", R"({"trackId":0,"fxType":"eq","position":0})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_EQ(text(r).toStdString(), std::string("slot=0"));
    ASSERT_EQ(tr0->getNumFXSlots(), 1);
    EXPECT_EQ(tr0->getFXChain().at(0)->getType().toStdString(), std::string("eq"));

    // add_fx: compressor (default position = append)
    r = callTool(2, "add_fx", R"({"trackId":0,"fxType":"compressor"})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_EQ(text(r).toStdString(), std::string("slot=1"));
    ASSERT_EQ(tr0->getNumFXSlots(), 2);
    EXPECT_EQ(tr0->getFXChain().at(1)->getType().toStdString(), std::string("compressor"));

    // add_fx: reverb
    r = callTool(3, "add_fx", R"({"trackId":0,"fxType":"reverb"})");
    EXPECT_FALSE(r.value("error").isObject());
    ASSERT_EQ(tr0->getNumFXSlots(), 3);
    EXPECT_EQ(tr0->getFXChain().at(2)->getType().toStdString(), std::string("reverb"));

    // add_fx: delay
    r = callTool(4, "add_fx", R"({"trackId":0,"fxType":"delay"})");
    EXPECT_FALSE(r.value("error").isObject());
    ASSERT_EQ(tr0->getNumFXSlots(), 4);
    EXPECT_EQ(tr0->getFXChain().at(3)->getType().toStdString(), std::string("delay"));

    // Model state should be in sync: FX_CHAIN subtree has 4 children
    {
        auto fxChainTree = engine.getProjectModel().getTrackListTree()
                              .getChild(0).getChildWithName(IDs::FX_CHAIN);
        EXPECT_EQ(fxChainTree.getNumChildren(), 4);
    }

    // set_fx_bypass: slot 1 (compressor) -> true
    r = callTool(5, "set_fx_bypass", R"({"trackId":0,"slotIndex":1,"bypassed":true})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_TRUE(tr0->getFXChain().at(1)->isBypassed());
    EXPECT_FALSE(tr0->getFXChain().at(0)->isBypassed());
    EXPECT_FALSE(tr0->getFXChain().at(2)->isBypassed());
    EXPECT_FALSE(tr0->getFXChain().at(3)->isBypassed());
    // model side: bypassed property persisted
    {
        auto fxChainTree = engine.getProjectModel().getTrackListTree()
                              .getChild(0).getChildWithName(IDs::FX_CHAIN);
        EXPECT_TRUE(static_cast<bool>(fxChainTree.getChild(1).getProperty(IDs::bypassed)));
    }

    // set_fx_bypass: slot 1 -> false
    r = callTool(6, "set_fx_bypass", R"({"trackId":0,"slotIndex":1,"bypassed":false})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(tr0->getFXChain().at(1)->isBypassed());

    // remove_fx: dryRun=true (no mutation)
    r = callTool(7, "remove_fx", R"({"trackId":0,"slotIndex":2,"dryRun":true})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_TRUE(text(r).contains("would remove"));
    EXPECT_EQ(tr0->getNumFXSlots(), 4);  // unchanged

    // remove_fx: actually remove slot 2 (reverb)
    r = callTool(8, "remove_fx", R"({"trackId":0,"slotIndex":2})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    ASSERT_EQ(tr0->getNumFXSlots(), 3);
    EXPECT_EQ(tr0->getFXChain().at(2)->getType().toStdString(), std::string("delay"));

    // Model state in sync: FX_CHAIN subtree has 3 children
    {
        auto fxChainTree = engine.getProjectModel().getTrackListTree()
                              .getChild(0).getChildWithName(IDs::FX_CHAIN);
        EXPECT_EQ(fxChainTree.getNumChildren(), 3);
    }

    // add_fx for a non-existent track returns "track not found" with isError=true
    r = callTool(9, "add_fx", R"({"trackId":99,"fxType":"eq"})");
    EXPECT_FALSE(r.value("error").isObject());  // no JSON-RPC error
    EXPECT_TRUE(r.value("result").toObject().value("isError").toBool(false));
    EXPECT_TRUE(text(r).contains("track not found"));

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, NotificationsCancelledSetsFlagAndProducesNoResponse) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

    EXPECT_FALSE(s.isCancelRequested());

    // Pump a notification (no "id" field). Per JSON-RPC 2.0 the server
    // must not produce a response.
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","method":"notifications/cancelled"})"));
    QByteArray out;
    EXPECT_FALSE(tp.waitForOutgoing(50, &out));
    EXPECT_TRUE(tp.drainOutgoing().isEmpty());
    EXPECT_TRUE(s.isCancelRequested());

    // A regular request after the notification must still get a response.
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":42,"method":"ping"})"));
    ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_EQ(r.value("id").toInt(), 42);
    EXPECT_TRUE(r.value("error").isNull() || r.value("error").isUndefined());

    // resetCancelFlag must clear the atomic for the next run.
    s.resetCancelFlag();
    EXPECT_FALSE(s.isCancelRequested());

    s.stop();
    s.setTransport(nullptr);
}

namespace {
// Build a unique temp file path for a single export run. Cleans up any
// pre-existing file so the assertion is meaningful.
// Uses forward slashes so the path is safe to embed directly in JSON
// without escaping (QStandardPaths returns native separators on Windows).
QString makeTempWavPath(const char* tag) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    dir = QDir::fromNativeSeparators(dir);
    QString path = QString("%1/hdaw_export_test_%2_%3.wav")
                       .arg(dir)
                       .arg(tag)
                       .arg(QCoreApplication::applicationPid());
    QFile::remove(path);
    return path;
}

QString textOf(const QJsonObject& r) {
    return r.value("result").toObject()
            .value("content").toArray().at(0).toObject()
            .value("text").toString();
}
} // namespace

TEST(McpServer, ExportAudioDryRunReturnsPlan) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

    QString path = makeTempWavPath("dryrun");

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"export_audio","arguments":{"outputPath":")" + path.toUtf8() +
        R"(","dryRun":true}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_TRUE(textOf(r).contains("would export to"));
    EXPECT_TRUE(QFile::exists(path) == false);  // dry-run must not write

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, ExportAudioSkipsWhenCancelFlagSet) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

    // Arm the cancel flag directly (simulating a notifications/cancelled
    // already dispatched before this tool call).
    s.setCancelFlag(true);
    ASSERT_TRUE(s.isCancelRequested());

    QString path = makeTempWavPath("cancelled");
    QByteArray request = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export_audio","arguments":{"outputPath":")" + path.toUtf8() + R"("}}})";
    tp.pumpIncoming(request);
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_TRUE(r.value("result").toObject().value("isError").toBool(false));
    QString text = textOf(r);
    EXPECT_TRUE(text.contains("cancelled")) << "got: " << text.toStdString();
    EXPECT_FALSE(QFile::exists(path));  // no file written

    // The handler must have cleared the flag so subsequent calls work.
    EXPECT_FALSE(s.isCancelRequested());

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, ExportAudioRendersDefaultProject) {
    AudioEngine engine;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

    QString path = makeTempWavPath("real");

    // A 2-second render of the default project (3 tracks, 2 MIDI clips)
    // produces a small but non-empty WAV. The exact duration is bounded by
    // [start, end]; we use a short range so the test finishes quickly.
    QString args = QString(R"({"outputPath":"%1","format":"wav","start":0.0,"end":2.0,"sampleRate":44100.0,"bitDepth":16})")
                       .arg(path);
    QString req = QString(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export_audio","arguments":%1}})")
                      .arg(args);
    tp.pumpIncoming(req.toUtf8());
    QByteArray out;
    // The export takes a few hundred ms to render 2 seconds at 44.1kHz.
    // Allow up to 30s.
    ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
    // The export tool emits progress notifications through the same
    // transport. Look for the response (object with an "id") specifically.
    auto r = parseResponse(out);
    EXPECT_FALSE(r.value("error").isObject());
    QString text = textOf(r);
    if (r.value("result").toObject().value("isError").toBool(false)) {
        FAIL() << "export failed: " << text.toStdString();
    }
    EXPECT_TRUE(text.contains("exported to")) << "got: [" << text.toStdString() << "]";
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_GT(QFile(path).size(), 0);

    // The MCP cancel flag must be cleared after a successful run.
    EXPECT_FALSE(s.isCancelRequested());

    QFile::remove(path);
    s.stop();
    s.setTransport(nullptr);
}
