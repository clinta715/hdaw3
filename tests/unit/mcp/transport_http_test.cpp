#include <gtest/gtest.h>
#include "mcp/McpTransportHttp.h"
#include "mcp/McpServer.h"
using namespace mcp;

TEST(HttpTransport, ConstructsAndExposesPort) {
    TransportHttp t(8765);
    EXPECT_EQ(t.port(), 8765);
}

TEST(HttpTransport, ConstructsWithAlternatePort) {
    TransportHttp t(9001);
    EXPECT_EQ(t.port(), 9001);
}
