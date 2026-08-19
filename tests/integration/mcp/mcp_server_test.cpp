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
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QVector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include "common/DebugLog.h"

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

// Poll the loopback until a notifications/exportComplete line appears in the
// accumulated outgoing buffer (timeout in ms). Returns {} on timeout.
// processEvents is required: exportComplete is delivered to the main thread
// via a queued invokeMethod (notifyFromBackground), and test_main runs no
// event loop. The budget is real elapsed time — waitForOutgoing returns
// immediately while the buffer is non-empty (response + progress lines).
QJsonObject waitExportComplete(mcp::TransportLoopback& tp, int msec) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(msec);
    while (std::chrono::steady_clock::now() < deadline) {
        int remaining = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count());
        QByteArray out;
        if (tp.waitForOutgoing(std::min(250, remaining), &out)) {
            int start = 0;
            while (start < out.size()) {
                int nl = out.indexOf('\n', start);
                QByteArray line = (nl >= 0) ? out.mid(start, nl - start) : out.mid(start);
                start = (nl >= 0) ? nl + 1 : out.size();
                QByteArray trimmed = line.trimmed();
                if (trimmed.contains("notifications/exportComplete")) {
                    QJsonObject obj = QJsonDocument::fromJson(trimmed).object();
                    return obj.value("params").toObject();
                }
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(10);  // avoid hot spin; buffer accumulates
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
// NOTE: in every test below the transport object must be declared BEFORE the
// McpServer so it outlives it: ~McpServer() calls transport_->stop(), and a
// destroyed transport's QMutex hangs the teardown forever.
TEST(McpServer, HttpRoundTrip) {
    AudioEngine engine;
    mcp::TransportHttp t(18765);
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
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
    mcp::TransportLoopback tp;
    mcp::McpServer s;
    s.setEngine(&engine);
    mcp::registerAllTools(s);
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
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();
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
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

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
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();
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

    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

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

TEST(McpServer, SetFaderAuthoritativeDisablesVolumeAutomation) {
    AudioEngine engine;
    engine.initialize();

    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

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

    // The default project's track 0 already has a "Volume" (paramID 1) lane but
    // it starts DISABLED; enable it first so the tool's disable is observable.
    engine.getProjectCommands().setAutomationEnabled(0, "Volume", true);

    auto r = callTool(1, "set_fader_authoritative", R"({"trackId":0,"authoritative":true})");
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_EQ(text(r).toStdString(), std::string("ok"));

    // Read model: Volume lane disabled; non-volume lanes untouched.
    auto lanes = engine.getReadModel().getAutomationLanes(0);
    bool foundVolume = false;
    for (const auto& l : lanes)
    {
        if (l.name == "Volume")
        {
            EXPECT_FALSE(l.enabled);
            foundVolume = true;
        }
    }
    EXPECT_TRUE(foundVolume);

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, NotificationsCancelledSetsFlagAndProducesNoResponse) {
    AudioEngine engine;
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

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

QString writeSineWav(const char* tag) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    dir = QDir::fromNativeSeparators(dir);
    QString path = QString("%1/hdaw_fx_input_%2_%3.wav")
                       .arg(dir)
                       .arg(tag)
                       .arg(QCoreApplication::applicationPid());
    QFile::remove(path);

    constexpr int sampleRate = 44100;
    constexpr int numChannels = 2;
    constexpr int numSeconds = 8;
    constexpr double amplitude = 0.5;
    constexpr double freqHz = 440.0;

    juce::AudioBuffer<float> buf(numChannels, sampleRate * numSeconds);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < buf.getNumSamples(); ++s)
            buf.setSample(ch, s, static_cast<float>(
                amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * s / sampleRate)));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(juce::File(path.toStdString())),
                            sampleRate, numChannels, 16, {}, 0));
    if (writer == nullptr) return {};
    writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    writer->flush();
    return path;
}

// 12 s stereo 440 Hz sine (0.5 amplitude) — longer than the 8 s streaming
// promotion threshold (StreamingClipSource::kPromoteToWholeFileMs), for the
// streamed-clip export test (Subsystem D gate G2).
QString writeLongSineWav() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) dir = QDir::tempPath();
    dir = QDir::fromNativeSeparators(dir);
    QString path = QString("%1/hdaw_stream_long_%2.wav")
                       .arg(dir)
                       .arg(QCoreApplication::applicationPid());
    QFile::remove(path);

    constexpr int sampleRate = 44100;
    constexpr int numChannels = 2;
    constexpr int numSeconds = 12;
    constexpr double amplitude = 0.5;
    constexpr double freqHz = 440.0;

    juce::AudioBuffer<float> buf(numChannels, sampleRate * numSeconds);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < buf.getNumSamples(); ++s)
            buf.setSample(ch, s, static_cast<float>(
                amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * freqHz * s / sampleRate)));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(juce::File(path.toStdString())),
                            sampleRate, numChannels, 16, {}, 0));
    if (writer == nullptr) return {};
    writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    writer->flush();
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
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

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

TEST(McpServer, ExportAudioConsumesStaleCancelFlag) {
    AudioEngine engine;
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    // Arm the cancel flag directly (simulating a notifications/cancelled the
    // MCP client dispatched before this tool call). The export handler must
    // consume the stale flag and still start the render.
    s.setCancelFlag(true);
    ASSERT_TRUE(s.isCancelRequested());

    QString path = makeTempWavPath("stale-cancel");
    QString args = QString(R"({"outputPath":"%1","format":"wav","start":0.0,"end":2.0,"sampleRate":44100.0,"bitDepth":16})")
                       .arg(path);
    QString req = QString(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export_audio","arguments":%1}})")
                      .arg(args);
    tp.pumpIncoming(req.toUtf8());
    QByteArray out;
    ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
    auto r = parseResponse(out);
    EXPECT_FALSE(r.value("error").isObject());
    QString text = textOf(r);
    if (r.value("result").toObject().value("isError").toBool(false)) {
        FAIL() << "export failed: " << text.toStdString();
    }
    EXPECT_TRUE(text.contains("export started")) << "got: [" << text.toStdString() << "]";

    // The handler must have consumed the stale flag.
    EXPECT_FALSE(s.isCancelRequested());

    auto complete = waitExportComplete(tp, 30000);
    ASSERT_FALSE(complete.isEmpty());
    EXPECT_TRUE(complete.value("success").toBool());

    EXPECT_TRUE(QFile::exists(path));
    EXPECT_GT(QFile(path).size(), 0);

    QFile::remove(path);
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, SetTempo) {
    AudioEngine engine;
    engine.initialize();  // getProjectCommands() requires initialize()
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"set_tempo","arguments":{"bpm":132}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_EQ(textOf(r).toStdString(), "ok");

    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":2,"method":"tools/call",
        "params":{"name":"get_project_summary","arguments":{}}})"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    QString summary = textOf(parseOne(out));
    EXPECT_TRUE(summary.contains("tempo=132")) << "got: [" << summary.toStdString() << "]";

    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":3,"method":"tools/call",
        "params":{"name":"undo","arguments":{"count":1}}})"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    EXPECT_FALSE(parseOne(out).value("error").isObject());

    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":4,"method":"tools/call",
        "params":{"name":"get_project_summary","arguments":{}}})"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    QString back = textOf(parseOne(out));
    // JUCE 8's set-based UndoManager undoes the whole transaction: the
    // default-project root properties (name/tempo) are recorded WITH the undo
    // manager and share the set with the tempo change, so one undo reverts the
    // tempo property entirely rather than restoring it to 120. Assert the
    // change is gone (not 132) instead of a specific restore value.
    EXPECT_FALSE(back.contains("tempo=132")) << "got: [" << back.toStdString() << "]";

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, SetTimeSignature) {
    AudioEngine engine;
    engine.initialize();  // getProjectCommands() requires initialize()
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"set_time_signature","arguments":{"numerator":3,"denominator":4}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_EQ(textOf(r).toStdString(), "ok");

    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":2,"method":"tools/call",
        "params":{"name":"get_transport","arguments":{}}})"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    QString txt = textOf(parseOne(out));
    auto obj = QJsonDocument::fromJson(txt.toUtf8()).object();
    EXPECT_EQ(obj.value("timeSigNumerator").toInt(), 3) << "got: [" << txt.toStdString() << "]";
    EXPECT_EQ(obj.value("timeSigDenominator").toInt(), 4) << "got: [" << txt.toStdString() << "]";

    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":3,"method":"tools/call",
        "params":{"name":"undo","arguments":{"count":1}}})"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    EXPECT_FALSE(parseOne(out).value("error").isObject());

    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":4,"method":"tools/call",
        "params":{"name":"get_transport","arguments":{}}})"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    QString backTxt = textOf(parseOne(out));
    auto back = QJsonDocument::fromJson(backTxt.toUtf8()).object();
    EXPECT_EQ(back.value("timeSigNumerator").toInt(), 4) << "got: [" << backTxt.toStdString() << "]";
    EXPECT_EQ(back.value("timeSigDenominator").toInt(), 4) << "got: [" << backTxt.toStdString() << "]";

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, GenerateRhythmPattern) {
    AudioEngine engine;
    engine.initialize();  // default project: 3 tracks, track 0 exists
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    // Default params: two euclidean pulses (4-over-3, rotation 1) collide
    // once at step 0 -> 4 + 3 - 1 = 6 notes in a new MIDI clip.
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"generate_rhythm_pattern","arguments":{"trackId":0}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_TRUE(textOf(r).contains("notes=6")) << "got: [" << textOf(r).toStdString() << "]";

    // Pulses disabled; pure DSL voice: E(3,8) = 3 euclidean hits.
    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"rp({"jsonrpc":"2.0","id":2,"method":"tools/call",
        "params":{"name":"generate_rhythm_pattern","arguments":{"trackId":0,"pulseA":0,"pulseB":0,"dsl":"E(3,8)"}}})rp"));
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(500, &out));
    auto r2 = parseOne(out);
    EXPECT_FALSE(r2.value("error").isObject());
    EXPECT_FALSE(r2.value("result").toObject().value("isError").toBool(true));
    EXPECT_TRUE(textOf(r2).contains("notes=3")) << "got: [" << textOf(r2).toStdString() << "]";

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, AddInstrumentPartTool) {
    AudioEngine engine;
    engine.initialize();  // default project: 3 tracks
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    const int before = engine.getReadModel().getTrackCount();

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"add_instrument_part","arguments":{"trackName":"MCP Lead","style":"Lead","lengthBeats":4,"placement":"region","count":1}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(10000, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_TRUE(textOf(r).contains("trackIndex=")) << "got: [" << textOf(r).toStdString() << "]";
    EXPECT_TRUE(textOf(r).contains("notes=")) << "got: [" << textOf(r).toStdString() << "]";

    // The composite added exactly one track.
    EXPECT_EQ(engine.getReadModel().getTrackCount(), before + 1);

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, AutoGainToTargetTool) {
    AudioEngine engine;
    engine.initialize();
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    const QString srcPath = writeSineWav("gainstage");
    ASSERT_FALSE(srcPath.isEmpty());
    // Default project track 0 (audio) — add the sine as an audio clip (2 s
    // of content at 120 BPM; the 1.0 s window is fully covered by signal).
    const int clipId = engine.getProjectCommands().addAudioClip(
        0, 0.0, 4.0, srcPath.toStdString(), "sine");
    ASSERT_GE(clipId, 0);

    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"auto_gain_to_target","arguments":{"trackId":0,"targetRms":0.05,"windowSeconds":1.0}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    QString txt = textOf(r);
    EXPECT_TRUE(txt.contains("ok=1")) << "got: [" << txt.toStdString() << "]";
    EXPECT_TRUE(txt.contains("fader=")) << "got: [" << txt.toStdString() << "]";

    QFile::remove(srcPath);
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, AuditionPluginTool) {
    AudioEngine engine;
    engine.initialize();
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    const int baseline = engine.getReadModel().getTrackCount();

    // A fake plugin id: the probe completes, renders silence (audible=false),
    // and is reverted — the tool must not crash and the project must be left
    // untouched (temp-probe cleanup contract).
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"audition_plugin","arguments":{"pluginId":"test.plugin.id","windowSeconds":1.0}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    QString txt = textOf(r);
    EXPECT_TRUE(txt.contains("ok=1")) << "got: [" << txt.toStdString() << "]";
    EXPECT_TRUE(txt.contains("audible=0")) << "got: [" << txt.toStdString() << "]";

    // The probe track was cleaned up.
    EXPECT_EQ(engine.getReadModel().getTrackCount(), baseline);

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, VerifyPartTool) {
    AudioEngine engine;
    engine.initialize();
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    // Compose a gain-staged part first (internal fm_synth), then verify it.
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"add_instrument_part","arguments":{"trackName":"MCP Verify","style":"Standard","lengthBeats":4,"seed":7,"targetRms":0.15,"windowSeconds":4.0}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_FALSE(r.value("result").toObject().value("isError").toBool(true));
    QString txt = textOf(r);
    ASSERT_TRUE(txt.contains("trackIndex=")) << "got: [" << txt.toStdString() << "]";
    QString tiStr = txt.mid(txt.indexOf("trackIndex=") + 11);
    tiStr = tiStr.left(tiStr.indexOf(' '));
    const int trackIndex = tiStr.toInt();
    ASSERT_GE(trackIndex, 0);

    tp.drainOutgoing();
    const QString req = QString(R"({"jsonrpc":"2.0","id":2,"method":"tools/call",)"
                                R"("params":{"name":"verify_part","arguments":{"trackIndex":%1,"windowSeconds":4.0}}})")
                            .arg(trackIndex);
    tp.pumpIncoming(req.toUtf8());
    QByteArray out2; ASSERT_TRUE(tp.waitForOutgoing(30000, &out2));
    auto r2 = parseOne(out2);
    EXPECT_FALSE(r2.value("error").isObject());
    EXPECT_FALSE(r2.value("result").toObject().value("isError").toBool(true));
    QString txt2 = textOf(r2);
    EXPECT_TRUE(txt2.contains("ok=1")) << "got: [" << txt2.toStdString() << "]";
    EXPECT_TRUE(txt2.contains("nonClipping=1")) << "got: [" << txt2.toStdString() << "]";

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, AddInstrumentPartProgramIndex) {
    AudioEngine engine;
    engine.initialize();  // default project: 3 tracks
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    const int before = engine.getReadModel().getTrackCount();

    // programIndex is passed through the tool into the engine command. Per the
    // plan's G2 contract, programIndex >= 0 without a pluginId is rejected at
    // the command boundary — the composite is NOT built, so the project is left
    // untouched.
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"add_instrument_part","arguments":{"trackName":"MCP P","style":"Lead","lengthBeats":4,"placement":"region","count":1,"programIndex":0}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(10000, &out));
    auto r = parseOne(out);
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_TRUE(r.value("result").toObject().value("isError").toBool(true));
    EXPECT_TRUE(textOf(r).contains("programIndex requires a pluginId"))
        << "got: [" << textOf(r).toStdString() << "]";

    // Rejected before any tree mutation — track count unchanged.
    EXPECT_EQ(engine.getReadModel().getTrackCount(), before);

    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, ExportAudioRendersDefaultProject) {
    AudioEngine engine;
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

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
    EXPECT_TRUE(text.contains("export started")) << "got: [" << text.toStdString() << "]";

    auto complete = waitExportComplete(tp, 30000);
    ASSERT_FALSE(complete.isEmpty());
    EXPECT_TRUE(complete.value("success").toBool());

    // Exactly one completion notification per export.
    QByteArray buf;
    if (tp.waitForOutgoing(100, &buf)) {
        int count = 0, pos = 0;
        while ((pos = buf.indexOf("notifications/exportComplete", pos)) >= 0) {
            ++count;
            pos += 1;
        }
        EXPECT_EQ(count, 1);
    }

    EXPECT_TRUE(QFile::exists(path));
    EXPECT_GT(QFile(path).size(), 0);

    // The MCP cancel flag must be cleared after a successful run.
    EXPECT_FALSE(s.isCancelRequested());

    QFile::remove(path);
    s.stop();
    s.setTransport(nullptr);
}

// Subsystem D gate G2: a clip whose source file is long enough to STREAM
// (> 8 s) must export via the non-realtime synchronous-refill path with no
// dropouts. Before 76bc275 the realtime reader starved ~74 blocks at every
// ~4 s window boundary; this asserts every 100 ms slice of the render
// carries signal (a 100 ms slice straddling a boundary would dip below the
// threshold if whole blocks rendered silence).
TEST(McpServer, ExportAudioStreamsLongClipWithoutDropouts) {
    AudioEngine engine;
    engine.initialize();  // default project: 3 tracks, track 0 exists
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    const QString srcPath = writeLongSineWav();
    ASSERT_FALSE(srcPath.isEmpty());

    // 120 BPM so 24 beats == exactly 12 s (add_audio_clip takes beats).
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"set_tempo","arguments":{"bpm":120}}})"));
    QByteArray out; ASSERT_TRUE(tp.waitForOutgoing(5000, &out));
    EXPECT_FALSE(parseOne(out).value("error").isObject());

    tp.drainOutgoing();
    QString addArgs = QString(R"({"trackId":0,"start":0.0,"length":24.0,"sourceFile":"%1"})")
                          .arg(srcPath);
    QString addReq = QString(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add_audio_clip","arguments":%1}})")
                         .arg(addArgs);
    tp.pumpIncoming(addReq.toUtf8());
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(5000, &out));
    auto addR = parseOne(out);
    EXPECT_FALSE(addR.value("error").isObject());
    EXPECT_TRUE(textOf(addR).contains("clipId="))
        << "got: [" << textOf(addR).toStdString() << "]";

    QString path = makeTempWavPath("stream-long");
    tp.drainOutgoing();
    QString args = QString(R"({"outputPath":"%1","format":"wav","start":0.0,"end":12.0,"sampleRate":44100.0,"bitDepth":16})")
                       .arg(path);
    QString req = QString(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"export_audio","arguments":%1}})")
                      .arg(args);
    tp.pumpIncoming(req.toUtf8());
    out.clear(); ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
    auto r = parseResponse(out);
    EXPECT_FALSE(r.value("error").isObject());
    if (r.value("result").toObject().value("isError").toBool(false)) {
        FAIL() << "export failed: " << textOf(r).toStdString();
    }

    auto complete = waitExportComplete(tp, 30000);
    ASSERT_FALSE(complete.isEmpty());
    EXPECT_TRUE(complete.value("success").toBool());
    ASSERT_TRUE(QFile::exists(path));

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(
        fm.createReaderFor(juce::File(path.toStdString())));
    ASSERT_NE(reader, nullptr);
    ASSERT_GT(reader->lengthInSamples, 44100 * 11); // ~12 s rendered

    const int slice = 4410; // 100 ms at 44.1 kHz
    juce::AudioBuffer<float> buf(static_cast<int>(reader->numChannels), slice);
    for (int64_t pos = 0; pos + slice <= reader->lengthInSamples; pos += slice) {
        buf.clear();
        float* ptrs[2] = { buf.getWritePointer(0),
                           buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr };
        reader->read(ptrs, buf.getNumChannels(), pos, slice);
        double rms = 0.0;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < slice; ++i)
                rms += buf.getSample(ch, i) * buf.getSample(ch, i);
        rms = std::sqrt(rms / (slice * buf.getNumChannels()));
        EXPECT_GT(rms, 0.1) << "silent slice at sample " << pos
                            << " (t=" << (pos / 44100.0) << " s)";
    }

    QFile::remove(path);
    QFile::remove(srcPath);
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, ExportAudioCancelsMidRender) {
    AudioEngine engine;
    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    QString path = makeTempWavPath("cancel-mid");

    // An 8-second render is long enough that the explicit cancel_export tool
    // call right after the (immediate) response is picked up mid-render.
    QString args = QString(R"({"outputPath":"%1","format":"wav","start":0.0,"end":8.0,"sampleRate":44100.0,"bitDepth":16})")
                       .arg(path);
    QString req = QString(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export_audio","arguments":%1}})")
                      .arg(args);
    tp.pumpIncoming(req.toUtf8());
    QByteArray out;
    ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
    auto r = parseResponse(out);
    EXPECT_FALSE(r.value("error").isObject());
    QString text = textOf(r);
    EXPECT_TRUE(text.contains("export started")) << "got: [" << text.toStdString() << "]";

    // Cancellation is explicit via the cancel_export tool: it flips
    // ExportManager's cancel flag, the render loop aborts at the next block,
    // and the partial file is deleted.
    tp.drainOutgoing();
    tp.pumpIncoming(QByteArray(R"({"jsonrpc":"2.0","id":2,"method":"tools/call",
        "params":{"name":"cancel_export","arguments":{}}})"));
    out.clear();
    ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
    auto c = parseResponse(out);
    EXPECT_FALSE(c.value("error").isObject());
    EXPECT_TRUE(textOf(c).contains("cancel requested"))
        << "got: [" << textOf(c).toStdString() << "]";

    auto complete = waitExportComplete(tp, 30000);
    ASSERT_FALSE(complete.isEmpty());
    EXPECT_FALSE(complete.value("success").toBool());
    EXPECT_TRUE(complete.value("message").toString()
                    .contains("cancel", Qt::CaseInsensitive))
        << "got: [" << complete.value("message").toString().toStdString() << "]";
    EXPECT_FALSE(QFile::exists(path));  // partial file deleted on cancel
    EXPECT_FALSE(s.isCancelRequested());

    QFile::remove(path);
    s.stop();
    s.setTransport(nullptr);
}

TEST(McpServer, ExportAudioWithClapPluginDoesNotHang) {
    // Regression test: before the fix, exporting with a CLAP plugin would
    // hang forever because on_main_thread callbacks never fired during the
    // tight render loop. The fix adds a message pump between render blocks.

    AudioEngine engine;
    engine.initialize();

    // Find a CLAP instrument plugin from the cache (loaded by initialize()).
    // We don't call scanAll() here — it spawns external scanner processes
    // and can take many minutes, blocking the test thread.
    QString clapPluginId;
    for (const auto& pd : engine.getPluginManager().getPlugins()) {
        if (pd.pluginFormatName == "CLAP") {
            clapPluginId = QString::fromStdString(pd.createIdentifierString().toStdString());
            break;
        }
    }

    if (clapPluginId.isEmpty()) {
        GTEST_SKIP() << "No CLAP plugins found in cache — skipping export hang test";
    }

    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    // Add a track with the CLAP plugin
    int trackId = -1;
    {
        QString req = QString(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"add_track_with_fx","arguments":{"name":"CLAP Track","pluginId":"%1"}}})")
                          .arg(clapPluginId);
        tp.pumpIncoming(req.toUtf8());
        QByteArray out;
        ASSERT_TRUE(tp.waitForOutgoing(10000, &out));
        auto r = parseResponse(out);
        QString text = textOf(r);
        ASSERT_TRUE(text.contains("trackId")) << "Failed to add track: " << text.toStdString();
        QRegularExpression re("trackId=(\\d+)");
        auto match = re.match(text);
        ASSERT_TRUE(match.hasMatch()) << "Could not parse trackId from: " << text.toStdString();
        trackId = match.captured(1).toInt();
    }

    // Generate a short phrase (4 bars = 16 beats)
    {
        QString req = QString(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"generate_phrase","arguments":{"trackId":%1,"style":"Lead","length":16,"density":20}}})")
                          .arg(trackId);
        tp.drainOutgoing();
        tp.pumpIncoming(req.toUtf8());
        QByteArray out;
        ASSERT_TRUE(tp.waitForOutgoing(10000, &out));
    }

    // Export to WAV — this must complete within 30 seconds.
    // Before the fix, this would hang forever.
    QString path = makeTempWavPath("clap");
    {
        QString args = QString(R"({"outputPath":"%1","format":"wav","start":0.0,"end":8.0,"sampleRate":44100.0,"bitDepth":16})")
                           .arg(path);
        QString req = QString(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"export_audio","arguments":%1}})")
                          .arg(args);
        tp.drainOutgoing();
        tp.pumpIncoming(req.toUtf8());
        QByteArray out;
        // 30-second timeout — if the fix doesn't work, this will fail
        // instead of hanging the test suite forever.
        ASSERT_TRUE(tp.waitForOutgoing(30000, &out))
            << "Export with CLAP plugin timed out (the on_main_thread fix is not working)";
        auto r = parseResponse(out);
        EXPECT_FALSE(r.value("error").isObject());
        QString text = textOf(r);
        if (r.value("result").toObject().value("isError").toBool(false)) {
            FAIL() << "Export failed: " << text.toStdString();
        }
        EXPECT_TRUE(text.contains("export started")) << "got: [" << text.toStdString() << "]";
        auto complete = waitExportComplete(tp, 30000);
        ASSERT_FALSE(complete.isEmpty());
        EXPECT_TRUE(complete.value("success").toBool());
    }

    EXPECT_TRUE(QFile::exists(path));
    EXPECT_GT(QFile(path).size(), 0);

    // Assert the render is non-silent — the silent-export regression (CLAP
    // identifier string instead of file path reaching the isolated child)
    // produced a present-but-zero WAV. Block that class of regression here.
    {
        juce::AudioFormatManager fmtMgr;
        fmtMgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(
            fmtMgr.createReaderFor(juce::File(path.toStdString())));
        ASSERT_NE(reader, nullptr) << "Could not open exported WAV for peak analysis";
        juce::AudioBuffer<float> buf(
            static_cast<int>(reader->numChannels),
            static_cast<int>(reader->lengthInSamples));
        reader->read(&buf, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
        float peakAbs = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int s = 0; s < buf.getNumSamples(); ++s)
                peakAbs = std::max(peakAbs, std::abs(buf.getSample(ch, s)));
        EXPECT_GT(peakAbs, 0.01f) << "Exported WAV is silent (peak=" << peakAbs
                                  << "). The isolated CLAP spawn likely passed an "
                                     "identifier string instead of a real file path.";
    }

    QFile::remove(path);
    s.stop();
    s.setTransport(nullptr);
}

// Isolation-of-the-export regression: an export whose render graph hosts
// MULTIPLE isolated plugin instances must complete and produce a non-empty,
// non-silent WAV while the LIVE graph keeps its own isolated instances
// untouched. Before the isolation-export-wedge fix, the export render graph
// shared the live PluginManager/ProxyProcessManager (slot counter, children
// map, crash callbacks), so live FX-chain rebuilds during export could kill
// the export's own children via defensive killPluginHost(slotId) — the render
// never settled and the export hung forever. The fix gives the export its own
// plugin domain. This test drives the old failure shape (several isolated
// instances inside one export, several isolated instances on the live graph)
// through the real MCP export_audio path and requires completion within the
// wait budget; a hang regression trips the waitExportComplete timeout.
TEST(McpServer, ExportAudioWithMultipleIsolatedInstances) {
    AudioEngine engine;
    engine.initialize();

    // Find a CLAP instrument plugin from the cache (loaded by initialize()).
    QString clapPluginId;
    for (const auto& pd : engine.getPluginManager().getPlugins()) {
        if (pd.pluginFormatName == "CLAP") {
            clapPluginId = QString::fromStdString(pd.createIdentifierString().toStdString());
            break;
        }
    }

    if (clapPluginId.isEmpty()) {
        GTEST_SKIP() << "No CLAP plugins found in cache — skipping multi-instance export test";
    }

    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    // Three live tracks, each with its own isolated CLAP instance (the live
    // graph keeps these alive across the export below).
    QVector<int> trackIds;
    for (int i = 0; i < 3; ++i) {
        QString req = QString(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"add_track_with_fx","arguments":{"name":"MultiInst Track %1","pluginId":"%2"}}})")
                          .arg(i).arg(clapPluginId);
        tp.pumpIncoming(req.toUtf8());
        QByteArray out;
        ASSERT_TRUE(tp.waitForOutgoing(10000, &out));
        auto r = parseResponse(out);
        QString text = textOf(r);
        ASSERT_TRUE(text.contains("trackId")) << "Failed to add track: " << text.toStdString();
        QRegularExpression re("trackId=(\\d+)");
        auto match = re.match(text);
        ASSERT_TRUE(match.hasMatch()) << "Could not parse trackId from: " << text.toStdString();
        trackIds.append(match.captured(1).toInt());
    }

    // Give each track a MIDI phrase so the isolated instruments render audio.
    for (int i = 0; i < trackIds.size(); ++i) {
        QString req = QString(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"generate_phrase","arguments":{"trackId":%1,"style":"Lead","length":16,"density":20}}})")
                          .arg(trackIds[i]);
        tp.drainOutgoing();
        tp.pumpIncoming(req.toUtf8());
        QByteArray out;
        ASSERT_TRUE(tp.waitForOutgoing(10000, &out));
    }

    // Export 8 seconds with the three isolated instances on the render graph.
    // Must complete within the wait budget — a wedge regression hangs here.
    QString path1 = makeTempWavPath("multiinst1");
    {
        QString args = QString(R"({"outputPath":"%1","format":"wav","start":0.0,"end":8.0,"sampleRate":44100.0,"bitDepth":16})")
                           .arg(path1);
        QString req = QString(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"export_audio","arguments":%1}})")
                          .arg(args);
        tp.drainOutgoing();
        tp.pumpIncoming(req.toUtf8());
        QByteArray out;
        ASSERT_TRUE(tp.waitForOutgoing(30000, &out))
            << "Multi-instance export did not start within 30s";
        auto r = parseResponse(out);
        EXPECT_FALSE(r.value("error").isObject());
        QString text = textOf(r);
        if (r.value("result").toObject().value("isError").toBool(false)) {
            FAIL() << "Export failed: " << text.toStdString();
        }
        EXPECT_TRUE(text.contains("export started")) << "got: [" << text.toStdString() << "]";
        auto complete = waitExportComplete(tp, 60000);
        ASSERT_FALSE(complete.isEmpty()) << "Export never completed (render-graph wedge?)";
        EXPECT_TRUE(complete.value("success").toBool())
            << "message: " << complete.value("message").toString().toStdString();
    }

    EXPECT_TRUE(QFile::exists(path1));
    EXPECT_GT(QFile(path1).size(), 0) << "Exported WAV is empty";

    // The render must be non-silent: each of the three isolated instruments
    // must have produced audio (peak assertion below).
    {
        juce::AudioFormatManager fmtMgr;
        fmtMgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(
            fmtMgr.createReaderFor(juce::File(path1.toStdString())));
        ASSERT_NE(reader, nullptr) << "Could not open exported WAV for peak analysis";
        juce::AudioBuffer<float> buf(
            static_cast<int>(reader->numChannels),
            static_cast<int>(reader->lengthInSamples));
        reader->read(&buf, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
        float peakAbs = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int s = 0; s < buf.getNumSamples(); ++s)
                peakAbs = std::max(peakAbs, std::abs(buf.getSample(ch, s)));
        EXPECT_GT(peakAbs, 0.01f) << "Exported WAV is silent (peak=" << peakAbs << ")";
    }
    QFile::remove(path1);

    // A second export must work back-to-back: the dedicated export domain is
    // created and destroyed per export, and the live graph (with its three
    // still-loaded instances) must not have been disturbed by the first one.
    QString path2 = makeTempWavPath("multiinst2");
    {
        QString args = QString(R"({"outputPath":"%1","format":"wav","start":0.0,"end":4.0,"sampleRate":44100.0,"bitDepth":16})")
                           .arg(path2);
        QString req = QString(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"export_audio","arguments":%1}})")
                          .arg(args);
        tp.drainOutgoing();
        tp.pumpIncoming(req.toUtf8());
        QByteArray out;
        ASSERT_TRUE(tp.waitForOutgoing(30000, &out));
        auto r = parseResponse(out);
        QString text = textOf(r);
        if (r.value("result").toObject().value("isError").toBool(false)) {
            FAIL() << "Second export failed: " << text.toStdString();
        }
        auto complete = waitExportComplete(tp, 60000);
        ASSERT_FALSE(complete.isEmpty()) << "Second export never completed";
        EXPECT_TRUE(complete.value("success").toBool());
    }
    EXPECT_TRUE(QFile::exists(path2));
    EXPECT_GT(QFile(path2).size(), 0);
    QFile::remove(path2);

    s.stop();
    s.setTransport(nullptr);
}
// Diagnostic matrix: exports every installed target CLAP instrument and
// asserts each produces non-silent audio. Exercises the full export pipeline
// (AudioProcessorGraph + isolated CLAP children) end-to-end.
//
// The export-path proxy-lifetime UAF that previously crashed this test IS
// CLOSED: PluginProxySlot::~PluginProxySlot now fires a destruction callback
// that erases the slot from PluginManager::liveProxySlots and cancels its
// pending CrashRecovery entry, so a respawn scheduled during export can never
// dereference the freed proxy after teardown. Additionally, crash-recovery
// respawn is suppressed for the entire export duration (including teardown)
// via CrashRecoveryManager::respawnEnabled, so a crashed plugin fails the
// export instead of racing the render thread with a kill+swap.
//
// KNOWN LIMITATION: several CLAP instruments render silence on isolated
// export (peak=0) even though the transport playhead is now forwarded to the
// child over the shm header (docs/plans/2026-08-09-forward-transport-playhead-to-isolated-children.md).
// Root cause (investigated, evidence in the plan): these instruments ship
// silent factory-default patches (e.g. the NodalRed2x gearmulator port drops
// program-change messages and has no presets — n2xdevice.cpp:91) and the
// matrix feeds no plugin state/preset, so they legitimately output zeros
// (outPeak=0 with healthy process status). The transport forward is correct
// host behavior and stays; the silent set is documented-skipped below rather
// than weakening the per-plugin EXPECT_GT. Plugins that DO produce audio at
// their default patch (Vital, Dexed, JC303, Identity, Altitude) remain fully
// asserted. TODO (out of scope): preset-load sweep to test these instruments
// with a real patch loaded.
TEST(McpServer, DiagnosticClapExportMatrix) {
    AudioEngine engine;
    engine.initialize();

    // Explicit target substrings. Missing plugins are reported and skipped.
    static const char* kTargets[] = {
        "Vital", "Dexed", "JC303", "Odin2", "ShinRonin",
        "Identity", "Gneiss", "Retrospect", "NodalRed2x", "Altitude"
    };

    // Plugins known to render silence on isolated export. Skip with a clear
    // log rather than failing the suite. Do NOT weaken the EXPECT_GT for
    // plugins that are NOT in this set.
    //
    // 2026-08-10 (fx-audio-input sweep): EMPTY. The last three entries
    // (ShinRonin, Gneiss, Retrospect) were misdiagnosed: they are EFFECT
    // plugins, and the matrix fed them a MIDI phrase with no audio input, so
    // they rendered silence by design. The sweep now classifies per plugin
    // (cached numInputChannels is 0 for all CLAPs, so the explicit name
    // fallback marks the three known FX) and feeds effect plugins a 440 Hz
    // sine clip (add_audio_clip, 16 beats @ 120 bpm = 8 s) on isolated
    // export; all three now render and are fully asserted below:
    //   ShinRonin peak=0.298889  Gneiss peak>0.01 (varies per run,
    //   observed 0.07-0.25)  Retrospect peak=0.300385
    // See docs/plans/2026-08-10-fx-audio-input-sweep-kknownsilent.md. Keep
    // this mechanism for future documented skips.
    static const char* kKnownSilent[] = { nullptr };
    auto isKnownSilent = [](const juce::String& name) {
        for (const char* s : kKnownSilent)
        {
            if (s == nullptr) break;
            if (name.containsIgnoreCase(juce::String(s)))
                return true;
        }
        return false;
    };

    std::vector<juce::PluginDescription> selected;
    const auto& allPlugins = engine.getPluginManager().getPlugins();
    for (const char* t : kTargets)
    {
        bool found = false;
        for (const auto& pd : allPlugins)
        {
            if (pd.pluginFormatName != "CLAP") continue;
            if (!pd.name.containsIgnoreCase(juce::String(t))) continue;
            bool dup = false;
            for (const auto& s : selected)
                if (s.name == pd.name) { dup = true; break; }
            if (!dup) selected.push_back(pd);
            found = true;
            break;
        }
        if (!found)
            HDAW_LOG("DiagMatrix", "target-not-installed '" + juce::String(t) + "'");
    }

    if (selected.empty())
        GTEST_SKIP() << "No target CLAP plugins found in cache";

    mcp::TransportLoopback tp;
    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    tp.start(&s); s.setTransport(&tp); s.start();

    HDAW_LOG("DiagMatrix",
             juce::String("matrix-start selected=") + juce::String((int)selected.size()));

    // Per-plugin result rows.
    struct Row {
        std::string name;
        std::string id;
        std::string file;
        bool done = false;
        bool addOk = false;
        bool exportOk = false;
        float peak = 0.0f;
        bool hung = false;
        std::string phaseNote;
    };
    std::vector<Row> rows;
    rows.resize(selected.size());
    for (size_t pi = 0; pi < selected.size(); ++pi)
    {
        const auto& pd = selected[pi];
        rows[pi].name = pd.name.toStdString();
        rows[pi].id = pd.createIdentifierString().toStdString();
        rows[pi].file = pd.fileOrIdentifier.toStdString();
    }

    std::mutex cvMutex;
    std::condition_variable cv;
    const std::chrono::seconds perPluginTimeout(180);

    for (size_t pi = 0; pi < rows.size(); ++pi)
    {
        Row& row = rows[pi];
        const auto& pd = selected[pi];
        const int baseId = static_cast<int>(pi * 100);

        // Worker thread runs ONLY this plugin's pipeline so a hang is not a
        // whole-suite hang. One worker at a time (sequential).
        std::thread worker([&, pi, baseId]() {
            Row& r = rows[pi];
            auto run = [&](int id, const char* tool, const QString& args) -> QString {
                QString req = QString(R"({"jsonrpc":"2.0","id":%1,"method":"tools/call",)"
                                      R"("params":{"name":"%2","arguments":%3}})")
                                  .arg(id).arg(tool).arg(args);
                tp.drainOutgoing();
                tp.pumpIncoming(req.toUtf8());
                QByteArray out;
                if (!tp.waitForOutgoing(60000, &out))
                    return QString("TIMEOUT");
                auto resp = parseResponse(out);
                return textOf(resp);
            };

            // 1) Reset project to a clean 3-track default.
            auto newProjText = run(baseId + 1, "new_project", "{}");
            r.phaseNote += "new_project=" + newProjText.toStdString() + "; ";

            // 2) Add track with the CLAP plugin.
            QString plId = QString::fromStdString(pd.createIdentifierString().toStdString());
            QString addArgs = QString(R"({"name":"Diag %1","pluginId":"%2"})")
                                  .arg(QString::fromStdString(r.name), plId);
            QString addText = run(baseId + 2, "add_track_with_fx", addArgs);
            r.phaseNote += "add=" + addText.toStdString() + "; ";
            QRegularExpression re("trackId=(\\d+)");
            auto m = re.match(addText);
            if (m.hasMatch())
            {
                r.addOk = true;
                int trackId = m.captured(1).toInt();

                // 3) Effect plugins (numInputChannels > 0) get a 440 Hz sine
                // clip as audio input; instruments get the MIDI phrase.
                bool isEffect = pd.numInputChannels > 0
                    || pd.name.containsIgnoreCase("ShinRonin")
                    || pd.name.containsIgnoreCase("Gneiss")
                    || pd.name.containsIgnoreCase("Retrospect");
                HDAW_LOG("DiagMatrix",
                         juce::String("classify plugin=") + juce::String(r.name)
                         + " inCh=" + juce::String(static_cast<int>(pd.numInputChannels))
                         + " outCh=" + juce::String(static_cast<int>(pd.numOutputChannels))
                         + " isEffect=" + juce::String(isEffect ? "true" : "false"));

                QString fxWavPath;
                if (isEffect)
                {
                    fxWavPath = writeSineWav(("fx" + std::to_string(pi)).c_str());
                    if (fxWavPath.isEmpty())
                    {
                        r.phaseNote += "sine-wav-write-failed; ";
                    }
                    else
                    {
                        QString clipArgs = QString(R"({"trackId":%1,"start":0,"length":16,"sourceFile":"%2"})")
                                              .arg(trackId)
                                              .arg(fxWavPath);
                        QString clipText = run(baseId + 3, "add_audio_clip", clipArgs);
                        r.phaseNote += "clip=" + clipText.toStdString() + "; ";
                    }
                }
                else
                {
                    // Generate a Lead phrase, 4 bars (length=16 beats), on it.
                    QString genArgs = QString(R"({"trackId":%1,"style":"Lead","length":16,"density":20})")
                                          .arg(trackId);
                    QString genText = run(baseId + 3, "generate_phrase", genArgs);
                    r.phaseNote += "gen=" + genText.toStdString() + "; ";
                }

                // 4) Export to a unique temp WAV (same params as the existing test).
                QString path = makeTempWavPath(("dx" + std::to_string(pi)).c_str());
                QString expArgs = QString(R"({"outputPath":"%1","format":"wav","start":0.0,"end":8.0,"sampleRate":44100.0,"bitDepth":16})")
                                      .arg(path);
                QString expText = run(baseId + 4, "export_audio", expArgs);
                r.phaseNote += "export=" + expText.toStdString() + "; ";
                r.exportOk = expText.contains("export started");
                auto complete = waitExportComplete(tp, 60000);
                r.exportOk = r.exportOk && !complete.isEmpty() && complete.value("success").toBool();
                r.phaseNote += "complete=" + (complete.isEmpty() ? std::string("TIMEOUT")
                                    : (complete.value("success").toBool() ? std::string("ok")
                                      : complete.value("message").toString().toStdString())) + "; ";

                // 5) Decode and peak-scan the WAV.
                if (QFile::exists(path))
                {
                    juce::AudioFormatManager fmtMgr;
                    fmtMgr.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> reader(
                        fmtMgr.createReaderFor(juce::File(path.toStdString())));
                    if (reader != nullptr)
                    {
                        juce::AudioBuffer<float> buf(
                            static_cast<int>(reader->numChannels),
                            static_cast<int>(reader->lengthInSamples));
                        reader->read(&buf, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
                        float pk = 0.0f;
                        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                            for (int smp = 0; smp < buf.getNumSamples(); ++smp)
                                pk = std::max(pk, std::abs(buf.getSample(ch, smp)));
                        r.peak = pk;
                    }
                    else
                    {
                        r.phaseNote += "wav-decode-failed; ";
                    }
                    QFile::remove(path);
                }
                else
                {
                    r.phaseNote += "wav-missing; ";
                }
                if (!fxWavPath.isEmpty())
                    QFile::remove(fxWavPath);
            }
            else
            {
                r.phaseNote += "no-trackId-parsed; ";
            }

            HDAW_LOG("DiagMatrix",
                     juce::String("RESULT plugin=") + juce::String(r.name)
                     + " id=" + juce::String(r.id)
                     + " file=" + juce::String(r.file)
                     + " add=" + juce::String(r.addOk ? "ok" : "FAIL")
                     + " export=" + juce::String(r.exportOk ? "ok" : "FAIL")
                     + " peak=" + juce::String(r.peak, 6)
                     + " note=" + juce::String(r.phaseNote));

            {
                std::lock_guard<std::mutex> lk(cvMutex);
                r.done = true;
            }
            cv.notify_all();
        });

        {
            std::unique_lock<std::mutex> lk(cvMutex);
            // Pump Qt events on the main thread while the worker runs: the
            // exportComplete notification reaches the loopback via a queued
            // invokeMethod (notifyFromBackground) that only executes when the
            // main thread processes events (test_main runs no event loop).
            const auto deadline = std::chrono::steady_clock::now() + perPluginTimeout;
            bool done = false;
            while (!done && std::chrono::steady_clock::now() < deadline) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                done = cv.wait_for(lk, std::chrono::milliseconds(100),
                                   [&]() { return row.done; });
            }
            if (!done)
            {
                row.hung = true;
                row.phaseNote += "HUNG-after-timeout; ";
                HDAW_LOG("DiagMatrix",
                         juce::String("HUNG plugin=") + juce::String(row.name)
                         + " id=" + juce::String(row.id));
                // Can't join (stuck inside a render wait) — detach and let the
                // process teardown reclaim it. Keep processing subsequent plugins.
                worker.detach();
                continue;
            }
        }
        if (worker.joinable()) worker.join();
    }

    // Report + per-plugin assertion so failures identify the plugin.
    for (size_t pi = 0; pi < rows.size(); ++pi)
    {
        Row& r = rows[pi];
        std::cout << "[DiagMatrix] plugin=" << r.name
                  << " installedFile=" << r.file
                  << " addOk=" << r.addOk
                  << " exportOk=" << r.exportOk
                  << " hung=" << r.hung
                  << " peak=" << r.peak
                  << " note=" << r.phaseNote << std::endl;
        if (!r.addOk)
        {
            EXPECT_TRUE(false) << "plugin=" << r.name << " could not add track: "
                               << r.phaseNote;
            continue;
        }
        // Skip plugins known to render silence on isolated export (separate
        // investigation). Do NOT weaken the EXPECT_GT for other plugins.
        if (isKnownSilent(juce::String(r.name)))
        {
            std::cout << "[DiagMatrix] SKIP-SILENT plugin=" << r.name
                      << " peak=" << r.peak
                      << " (known isolated-export silence, TODO investigate)"
                      << std::endl;
            // Cannot use GTEST_SKIP() here — it would skip assertions for
            // plugins later in the list. Just log + continue.
            continue;
        }
        EXPECT_GT(r.peak, 0.01f) << "plugin=" << r.name
                                 << " hung=" << r.hung
                                 << " peak=" << r.peak
                                 << " phase=" << r.phaseNote;
    }

    s.stop();
    s.setTransport(nullptr);
}
