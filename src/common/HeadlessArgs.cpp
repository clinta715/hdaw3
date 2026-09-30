#include "HeadlessArgs.h"

#include <cstring>

namespace HDAW {

namespace {

// A token that starts with "--" is another flag, not a value — treat it as a
// missing value rather than trying to open a file called "--mcp-stdio" or
// parsing a port called "--mcp-http".
bool looksLikeFlag(const std::string& t)
{
    return t.size() >= 2 && t[0] == '-' && t[1] == '-';
}

// 0..65535, digits only. Non-numeric and out-of-range values are REFUSED with
// the flag AND the value named (lesson 38: a coerced or silently-dropped argument
// is how you end up binding a port nobody asked for).
bool parsePort(const std::string& value, unsigned& out, std::string& error, const char* flag)
{
    if (value.empty())
    {
        error = std::string("empty value for ") + flag + "= (expected " + flag
              + "=<port> or " + flag + " <port>)";
        return false;
    }
    unsigned v = 0;
    for (const char c : value)
    {
        if (c < '0' || c > '9')
        {
            error = std::string(flag) + " value '" + value
                  + "' is not a port (expected 0..65535)";
            return false;
        }
        v = v * 10u + static_cast<unsigned>(c - '0');
        if (v > 65535u)
        {
            error = std::string(flag) + " value '" + value
                  + "' is out of range (expected 0..65535)";
            return false;
        }
    }
    out = v;
    return true;
}

} // namespace

HeadlessArgs parseHeadlessArgs(const std::vector<std::string>& args)
{
    HeadlessArgs out;

    // Read the value of `flag` at args[i], in either spelling. Returns true when
    // this token WAS the flag (advancing i past a space-separated value); on a
    // malformed value it sets out.ok=false + out.error and still returns true, so
    // the caller stops with the SAME message the pre-existing --project path used.
    const auto take = [&](std::size_t& i, const char* flag, const char* noun,
                          std::string& value) -> bool
    {
        const std::string f(flag);
        const std::string prefix = f + "=";
        const std::string& a = args[i];

        if (a == f)
        {
            if (i + 1 >= args.size())
            {
                out.ok = false;
                out.error = "missing value for " + f + " (expected " + f + " <" + noun
                          + "> or " + f + "=<" + noun + ">)";
                return true;
            }
            const std::string next = args[i + 1];
            if (next.empty() || looksLikeFlag(next))
            {
                out.ok = false;
                out.error = "missing value for " + f + ": '" + next + "' is not a " + noun;
                return true;
            }
            value = next;
            ++i; // consume the value token
            return true;
        }
        if (a.rfind(prefix, 0) == 0)
        {
            const std::string v = a.substr(prefix.size());
            if (v.empty())
            {
                out.ok = false;
                out.error = "empty value for " + prefix + " (expected " + f + "=<" + noun + ">)";
                return true;
            }
            value = v;
            return true;
        }
        return false;
    };

    for (std::size_t i = 0; i < args.size(); ++i)
    {
        std::string value;

        if (take(i, "--project", "file", value))
        {
            if (!out.ok) return out;
            out.hasProject = true;
            out.projectPath = value;
            continue;
        }
        if (take(i, "--port", "port", value))
        {
            if (!out.ok) return out;
            if (!parsePort(value, out.frontendPort, out.error, "--port")) { out.ok = false; return out; }
            out.hasFrontendPort = true;
            continue;
        }
        if (take(i, "--mcp-http-port", "port", value))
        {
            if (!out.ok) return out;
            if (!parsePort(value, out.mcpHttpPort, out.error, "--mcp-http-port")) { out.ok = false; return out; }
            out.hasMcpHttpPort = true;
            continue;
        }
        if (take(i, "--mcp-http-host", "host", value))
        {
            if (!out.ok) return out;
            out.hasMcpHttpHost = true;
            out.mcpHttpHost = value;
            continue;
        }
    }
    return out;
}

HeadlessArgs parseHeadlessArgs(int argc, char** argv)
{
    std::vector<std::string> args;
    args.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i] != nullptr ? argv[i] : "");
    return parseHeadlessArgs(args);
}

} // namespace HDAW
