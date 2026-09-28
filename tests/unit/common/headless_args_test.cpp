// HeadlessArg parser (slice S5, docs/plans/2026-09-28-agent-mechanization.md §6):
// the `--project` one-shot session bootstrap. Both `--project <file>` and
// `--project=<file>` are accepted; a --project with no usable value is a HARD
// error (never a silent skip into an empty project).

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
