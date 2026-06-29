#include <gtest/gtest.h>
#include "mcp/McpServer.h"
using namespace mcp;

namespace { struct Counter { int n = 0; }; }

TEST(DryRun, DestructiveToolRespectsFlag) {
    Counter c;
    McpServer s;
    QJsonObject argsSchema{{"type","object"},
        {"properties", QJsonObject{{"dryRun", QJsonObject{{"type","boolean"}}}}}};
    s.registerTool({"destroy","mutates something", argsSchema,
        [&c](const QJsonObject& a) {
            bool d = a.value("dryRun").toBool(false);
            if (d) return McpToolResult::text("would mutate");
            ++c.n; return McpToolResult::text("mutated");
        }});
    auto r1 = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","destroy"},{"arguments",QJsonObject{{"dryRun", true}}}});
    EXPECT_FALSE(r1.toObject().value("isError").toBool());
    EXPECT_EQ(c.n, 0);
    EXPECT_TRUE(r1.toObject().value("content").toArray().at(0).toObject()
                .value("text").toString().contains("would mutate"));
    auto r2 = s.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name","destroy"},{"arguments",QJsonObject{}}});
    EXPECT_FALSE(r2.toObject().value("isError").toBool());
    EXPECT_EQ(c.n, 1);
}
