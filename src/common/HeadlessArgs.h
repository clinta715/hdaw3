#pragma once
// Headless session bootstrap args (docs/plans/2026-09-28-agent-mechanization.md §6,
// extended 2026-09-30 to the listen ports).
//
// `--project` (one-shot session bootstrap) and the listen ports `--port`,
// `--mcp-http-port`, `--mcp-http-host`. The value of this seam is that BOTH
// spellings (`--flag value` and `--flag=value`) are accepted and a missing or
// malformed value is a HARD error instead of a silent skip — a headless engine
// that quietly runs an empty project, or quietly binds the DEFAULT port while you
// asked for another one, is the exact failure mode this slice exists to kill.
//
// Measured 2026-09-30: `--port 8799` (the space spelling) was silently ignored by
// main_headless's old `=`-only parser, so a second engine instance tried to bind
// the default 8766, collided with the live session engine, and exited 1 with a
// "failed to bind" message that never mentioned the ignored argument.

#include <string>
#include <vector>

namespace HDAW {

struct HeadlessArgs
{
    bool ok = true;             // false => the command line is malformed
    bool hasProject = false;    // a --project value was supplied
    std::string projectPath;    // the supplied file ("" when !hasProject)

    // Listen ports. `has*` distinguishes "not supplied" (the caller keeps its own
    // default) from a supplied 0, which is a VALID value meaning "let the OS pick
    // a free port" (FrontendServer::start(0)).
    bool hasFrontendPort = false;
    unsigned frontendPort = 0;      // --port
    bool hasMcpHttpPort = false;
    unsigned mcpHttpPort = 0;       // --mcp-http-port
    bool hasMcpHttpHost = false;
    std::string mcpHttpHost;        // --mcp-http-host

    std::string error;          // human-readable problem when !ok
};

// Scan `argv` (argv[0] = program name) for the bootstrap arguments.
// Accepts BOTH `--flag <value>` (space separated) and `--flag=<value>`. A flag
// with no following token, a value that is another flag (`--...`), an empty
// `--flag=`, a non-numeric port, or a port above 65535 is a HARD error
// (ok=false, `error` names the flag AND the value) — never a silent skip. Every
// other token is ignored. The LAST occurrence of a flag wins when it is repeated.
HeadlessArgs parseHeadlessArgs(int argc, char** argv);

// Testable seam: parse argv[1..] as an explicit token vector.
HeadlessArgs parseHeadlessArgs(const std::vector<std::string>& args);

} // namespace HDAW
