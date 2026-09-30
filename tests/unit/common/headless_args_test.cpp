// HeadlessArg parser (slice S5, docs/plans/2026-09-28-agent-mechanization.md §6;
// extended 2026-09-30 to the listen ports): the `--project` one-shot session
// bootstrap plus `--port` / `--mcp-http-port` / `--mcp-http-host`. BOTH spellings
// (`--flag value` and `--flag=value`) are accepted; a flag with no usable value,
// or a port that is not a port, is a HARD error (never a silent skip into an
// empty project or onto the default port).

#include <gtest/gtest.h>
#include "common/HeadlessArgs.h"

#include <string>
#include <vector>

using HDAW::HeadlessArgs;
using HDAW::parseHeadlessArgs;

namespace {
HeadlessArgs parse(std::vector<std::string> args)
{
    return parseHeadlessArgs(args);
}
} // namespace

TEST(HeadlessArgs, SpaceSeparatedProject) {
    const HeadlessArgs r = parse({"--mcp-stdio", "--project", "C:\\tmp\\a.hdaw"});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.hasProject);
    EXPECT_EQ(r.projectPath, "C:\\tmp\\a.hdaw");
}

TEST(HeadlessArgs, EqualsSeparatedProject) {
    const HeadlessArgs r = parse({"--mcp-stdio", "--project=C:\\tmp\\a.hdaw"});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.hasProject);
    EXPECT_EQ(r.projectPath, "C:\\tmp\\a.hdaw");
}

TEST(HeadlessArgs, NoFlagIsNotAnError) {
    const HeadlessArgs r = parse({"--mcp-stdio"});
    EXPECT_TRUE(r.ok);
    EXPECT_FALSE(r.hasProject);
    EXPECT_TRUE(r.projectPath.empty());
}

TEST(HeadlessArgs, MissingValueIsHardError) {
    const HeadlessArgs r = parse({"--mcp-stdio", "--project"});
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.hasProject);
    EXPECT_NE(r.error.find("--project"), std::string::npos);
    EXPECT_NE(r.error.find("missing value"), std::string::npos);
}

TEST(HeadlessArgs, FollowingFlagIsNotAValue) {
    const HeadlessArgs r = parse({"--project", "--mcp-stdio"});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("not a file"), std::string::npos);
}

TEST(HeadlessArgs, EmptyEqualsValueIsHardError) {
    const HeadlessArgs r = parse({"--project="});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("empty value"), std::string::npos);
}

TEST(HeadlessArgs, LastOccurrenceWins) {
    const HeadlessArgs r = parse({"--project", "first.hdaw", "--project=second.hdaw"});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.projectPath, "second.hdaw");
}

TEST(HeadlessArgs, ArgvOverloadSkipsProgramName) {
    char p0[] = "HDAW_headless.exe";
    char p1[] = "--mcp-stdio";
    char p2[] = "--project";
    char p3[] = "x.hdaw";
    char* argv[] = {p0, p1, p2, p3};
    const HeadlessArgs r = parseHeadlessArgs(4, argv);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.hasProject);
    EXPECT_EQ(r.projectPath, "x.hdaw");
}

TEST(HeadlessArgs, ArgvOverloadReportsMissingValue) {
    char p0[] = "HDAW_headless.exe";
    char p1[] = "--project";
    char* argv[] = {p0, p1};
    const HeadlessArgs r = parseHeadlessArgs(2, argv);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("missing value"), std::string::npos);
}

// ─── Listen ports (2026-09-30) ─────────────────────────────────────────────
// Measured trap: `--port 8799` (the SPACE spelling) was silently ignored by the
// old `=`-only parser, so a second engine instance bound the default 8766,
// collided with the live session engine and exited 1 with a bind error that never
// mentioned the argument. Both spellings must work, and a bad port must be
// REFUSED naming the flag and the value.

TEST(HeadlessArgs, SpaceSeparatedPorts) {
    const HeadlessArgs r = parse({"--port", "8799", "--mcp-http", "--mcp-http-port", "18766",
                                  "--mcp-http-host", "127.0.0.1"});
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.hasFrontendPort);
    EXPECT_EQ(r.frontendPort, 8799u);
    EXPECT_TRUE(r.hasMcpHttpPort);
    EXPECT_EQ(r.mcpHttpPort, 18766u);
    EXPECT_TRUE(r.hasMcpHttpHost);
    EXPECT_EQ(r.mcpHttpHost, "127.0.0.1");
}

TEST(HeadlessArgs, EqualsSeparatedPorts) {
    const HeadlessArgs r = parse({"--port=8799", "--mcp-http-port=18766"});
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.frontendPort, 8799u);
    EXPECT_EQ(r.mcpHttpPort, 18766u);
}

TEST(HeadlessArgs, PortZeroIsAValidEphemeralRequest) {
    // 0 means "let the OS choose" (FrontendServer::start(0)); it must NOT be
    // confused with "absent", which is what hasFrontendPort distinguishes.
    const HeadlessArgs r = parse({"--port=0"});
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.hasFrontendPort);
    EXPECT_EQ(r.frontendPort, 0u);
}

TEST(HeadlessArgs, AbsentPortsStayAbsent) {
    const HeadlessArgs r = parse({"--mcp-stdio"});
    EXPECT_TRUE(r.ok);
    EXPECT_FALSE(r.hasFrontendPort);
    EXPECT_FALSE(r.hasMcpHttpPort);
    EXPECT_FALSE(r.hasMcpHttpHost);
}

TEST(HeadlessArgs, NonNumericPortIsAHardErrorNamingFlagAndValue) {
    const HeadlessArgs r = parse({"--port", "eight"});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("--port"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("eight"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("not a port"), std::string::npos) << r.error;
}

TEST(HeadlessArgs, OutOfRangePortIsAHardError) {
    const HeadlessArgs r = parse({"--mcp-http-port=99999"});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("--mcp-http-port"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("99999"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("out of range"), std::string::npos) << r.error;
}

TEST(HeadlessArgs, PortWithNoValueIsAHardError) {
    const HeadlessArgs r = parse({"--port"});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("missing value"), std::string::npos) << r.error;
    EXPECT_NE(r.error.find("--port"), std::string::npos) << r.error;
}

TEST(HeadlessArgs, PortFollowedByAFlagIsNotAValue) {
    const HeadlessArgs r = parse({"--port", "--mcp-http"});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("not a port"), std::string::npos) << r.error;
}

TEST(HeadlessArgs, EmptyEqualsPortIsAHardError) {
    const HeadlessArgs r = parse({"--port="});
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("empty value"), std::string::npos) << r.error;
}

TEST(HeadlessArgs, LastPortOccurrenceWins) {
    const HeadlessArgs r = parse({"--port=8799", "--port", "8899"});
    EXPECT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.frontendPort, 8899u);
}
