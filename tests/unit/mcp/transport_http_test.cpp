#include <gtest/gtest.h>
#include "mcp/McpTransportHttp.h"
#include "mcp/McpServer.h"
#include "engine/AudioEngine.h"
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
    // Use a port distinct from the HTTP round-trip integration test
    // (18765) so the two can run in the same test binary without
    // colliding on the bound loopback port.
    TransportHttp t(18760);
    EXPECT_TRUE(t.start(&s));
    EXPECT_TRUE(t.lastError().isEmpty()) << "lastError=" << t.lastError().toStdString();
    t.stop();
}
