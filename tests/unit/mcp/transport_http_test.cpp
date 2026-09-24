#include <gtest/gtest.h>
#include "mcp/McpTransportHttp.h"
#include "mcp/McpServer.h"
#include "engine/AudioEngine.h"
#include <QEventLoop>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
using namespace mcp;

TEST(HttpTransport, ConstructsAndExposesPort) {
    TransportHttp t(8765);
    EXPECT_EQ(t.port(), 8765);
}

TEST(HttpTransport, ConstructsWithAlternatePort) {
    TransportHttp t(9001);
    EXPECT_EQ(t.port(), 9001);
}

TEST(HttpTransport, StartStopLifecycle) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    // Use an OS-assigned ephemeral port (0) so this test and the HTTP
    // round-trip integration test can never collide on a fixed bound
    // loopback port (the old fixed 18760/18765 pairing failed whenever a
    // live engine held 18765 — handoff finding 2026-09-23).
    TransportHttp t(0);
    EXPECT_TRUE(t.start(&s));
    EXPECT_TRUE(t.lastError().isEmpty()) << "lastError=" << t.lastError().toStdString();
    // Bound-port contract: after a successful start() on port 0, port()
    // must expose the OS-assigned port (the bound-port URL of a real
    // round-trip is covered by McpServer.HttpRoundTrip).
    EXPECT_NE(t.port(), 0) << "start() must expose the OS-assigned bound port";
    t.stop();
}

// Pins the keep-alive config that prevents rebuild-commands-drop-response:
// with Qt's default 15 s keep-alive, the heartbeat abort()s the connection
// when idle time since the request READ (lastActiveTimer restarts only on
// read) exceeds the timeout, discarding a still-buffered response after a
// long synchronous rebuild — the race itself is statically proven against
// Qt sources (qhttpserverhttp1protocolhandler.cpp checkKeepAliveTimeout,
// qhttpserverconfiguration.cpp default 15 s) and is NOT reproduced here.
// Qt advertises the configured value in the Keep-Alive response header
// (addConnectionAndKeepAliveHeaders, qhttpserverhttp1protocolhandler.cpp),
// so an advertised timeout >= 900 pins the config that makes the drop
// impossible (heartbeat interval = timeout/2 = 450 s never fires inside
// the completion-to-flush window).
TEST(HttpTransport, AdvertisesKeepAliveTimeoutAtLeast900) {
    AudioEngine engine;
    mcp::McpServer s;
    s.setEngine(&engine);
    TransportHttp t(0);
    ASSERT_TRUE(t.start(&s));
    ASSERT_TRUE(t.lastError().isEmpty()) << "lastError=" << t.lastError().toStdString();

    // Raw HTTP/1.1 over QTcpSocket so the Keep-Alive header is read
    // unmodified off the wire (no client-side header normalization). No
    // Connection header → HTTP/1.1 default keep-alive → Qt emits
    // Keep-Alive: timeout=N.
    const QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
    QByteArray post = "POST /mcp HTTP/1.1\r\n"
                      "Host: 127.0.0.1\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: ";
    post += QByteArray::number(body.size());
    post += "\r\n\r\n";
    post += body;

    QTcpSocket sock;
    sock.connectToHost(QHostAddress::LocalHost, t.port());
    sock.write(post);

    // Nested event loop (the McpServer.HttpRoundTrip pattern): the server
    // lives on this test thread, so only an event loop dispatches its
    // socket notifiers — blocking waitForReadyRead would starve it.
    QByteArray resp;
    QEventLoop loop;
    QObject::connect(&sock, &QTcpSocket::readyRead, &sock, [&] {
        resp += sock.readAll();
        if (resp.contains("\r\n\r\n"))
            loop.quit();
    });
    QObject::connect(&sock, &QTcpSocket::disconnected, &sock, [&] {
        resp += sock.readAll();
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    t.stop();

    ASSERT_FALSE(resp.isEmpty()) << "no HTTP response received within 5 s";
    const QRegularExpression re(
        QStringLiteral(R"((?i)\r\nKeep-Alive:\s*timeout=(\d+))"));
    const auto m = re.match(QString::fromLatin1(resp));
    ASSERT_TRUE(m.hasMatch())
        << "response must advertise Keep-Alive: timeout=N, got: "
        << resp.constData();
    EXPECT_GE(m.captured(1).toLongLong(), 900)
        << "Keep-Alive timeout must be >= 900 s (defect class: "
        << "rebuild-commands-drop-response)";
}
