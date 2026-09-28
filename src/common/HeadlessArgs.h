#pragma once
// Headless one-shot session bootstrap args (docs/plans/2026-09-28-agent-mechanization.md §6).
//
// ONLY `--project` lives here: main_headless's pre-existing parseFlag/parseValue
// helpers stay untouched (minimal blast radius). The value of this seam is that
// BOTH spellings are accepted and a missing value is a HARD error instead of a
// silent skip — a headless engine that quietly runs an empty project is the
// exact failure mode this slice exists to kill.

#include <string>
#include <vector>

namespace HDAW {

struct HeadlessArgs
{
    bool ok = true;             // false => the command line is malformed
    bool hasProject = false;    // a --project value was supplied
    std::string projectPath;    // the supplied file ("" when !hasProject)
    std::string error;          // human-readable problem when !ok
};

// Scan `argv` (argv[0] = program name) for the `--project` bootstrap argument.
// Accepts BOTH `--project <file>` (space separated) and `--project=<file>`.
// A `--project` with no following token, a value that is another flag (`--...`),
// or an empty `--project=` is a HARD error (ok=false, `error` names the problem)
// — never a silent skip. Every other token is ignored. The LAST occurrence wins
// when the flag is repeated.
HeadlessArgs parseHeadlessArgs(int argc, char** argv);

// Testable seam: parse argv[1..] as an explicit token vector.
HeadlessArgs parseHeadlessArgs(const std::vector<std::string>& args);

} // namespace HDAW
