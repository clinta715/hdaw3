#include <gtest/gtest.h>
#include "mcp/McpTransportStdio.h"
using namespace mcp;

TEST(StdioTransport, NotifyPublicSurface) {
    TransportStdio t;
    EXPECT_NO_THROW(t.notify(QByteArray("{}")));
}
