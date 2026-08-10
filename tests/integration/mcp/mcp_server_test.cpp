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
#include <QUrl>
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

    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

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
        EXPECT_TRUE(text.contains("exported to")) << "got: [" << text.toStdString() << "]";
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

// ────────────────────────────────────────────────────────────────────────────
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

    mcp::McpServer s; s.setEngine(&engine); mcp::registerAllTools(s);
    mcp::TransportLoopback tp; tp.start(&s); s.setTransport(&tp); s.start();

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
                r.exportOk = expText.contains("exported to");

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
            if (!cv.wait_for(lk, perPluginTimeout, [&]() { return row.done; }))
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
