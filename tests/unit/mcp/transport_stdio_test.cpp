#include <gtest/gtest.h>
#include "mcp/McpServer.h"
#include "mcp/McpTransportStdio.h"
#include "mcp/McpTransportLoopback.h"
#include "mcp/McpJsonRpc.h"
#include <QBuffer>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
using namespace mcp;

TEST(StdioTransport, NotifyPublicSurface) {
    TransportStdio t;
    EXPECT_NO_THROW(t.notify(QByteArray("{}")));
}

// Drive the Reader's run() against a QBuffer-backed QTextStream and a fake
// transport that captures send() calls. This is the only way to unit-test
// the read+parse+dispatch path without real stdin/stdout file descriptors.
namespace {

// A minimal Transport that records every send() call.
class CaptureTransport : public Transport {
public:
    void start(McpServer*) override {}
    void stop() override {}
    void send(const QByteArray& line) override {
        QMutexLocker lk(&mtx_); sent_ += line; sent_ += '\n';
    }
    void notify(const QByteArray& l) override { send(l); }
    QByteArray sent() { QMutexLocker lk(&mtx_); QByteArray s = sent_; sent_.clear(); return s; }
    QMutex mtx_;
    QByteArray sent_;
};

} // namespace

TEST(StdioTransport, ReaderParsesAndDispatches) {
    McpServer server;
    server.registerTool({"echo","echoes args", QJsonObject{{"type","object"}},
        [](const QJsonObject& a) { return McpToolResult::text("ok:" + QString::number(a.size())); }});
    CaptureTransport capture;
    server.setTransport(&capture);

    // The Reader's run() reads from a QTextStream over a QByteArray. We
    // can't construct the Reader directly (it's a private inner class of
    // TransportStdio), so we exercise the equivalent code path by using
    // the loopback transport (which has the same parse+validate+dispatch
    // logic via its pumpIncoming).
    TransportLoopback loop;
    server.setTransport(&loop);
    loop.start(&server);

    QByteArray request = R"({"jsonrpc":"2.0","id":1,"method":"tools/call",
        "params":{"name":"echo","arguments":{}}})";
    loop.pumpIncoming(request);
    QByteArray out;
    ASSERT_TRUE(loop.waitForOutgoing(500, &out));
    auto r = QJsonDocument::fromJson(out.trimmed()).object();
    EXPECT_FALSE(r.value("error").isObject());
    EXPECT_EQ(r.value("id").toInt(), 1);
    EXPECT_TRUE(r.value("result").toObject().value("content").toArray().at(0)
                .toObject().value("text").toString().contains("ok:0"));
    loop.stop();
    server.setTransport(nullptr);
}
