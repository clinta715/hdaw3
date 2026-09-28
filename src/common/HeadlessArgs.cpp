#include "HeadlessArgs.h"

#include <cstring>

namespace HDAW {

namespace {

constexpr const char* kProjectFlag   = "--project";
constexpr const char* kProjectPrefix = "--project=";

// A token that starts with "--" is another flag, not a file value — treat it as
// a missing value rather than trying to open a file called "--mcp-stdio".
bool looksLikeFlag(const std::string& t)
{
    return t.size() >= 2 && t[0] == '-' && t[1] == '-';
}

} // namespace

HeadlessArgs parseHeadlessArgs(const std::vector<std::string>& args)
{
    HeadlessArgs out;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        if (a == kProjectFlag)
        {
            if (i + 1 >= args.size())
            {
                out.ok = false;
                out.error = "missing value for --project (expected --project <file> or --project=<file>)";
                return out;
            }
            const std::string value = args[i + 1];
            if (value.empty() || looksLikeFlag(value))
            {
                out.ok = false;
                out.error = "missing value for --project: '" + value + "' is not a file";
                return out;
            }
            out.hasProject = true;
            out.projectPath = value;
            ++i; // consume the value token
        }
        else if (a.rfind(kProjectPrefix, 0) == 0)
        {
            const std::string value = a.substr(std::strlen(kProjectPrefix));
            if (value.empty())
            {
                out.ok = false;
                out.error = "empty value for --project= (expected --project=<file>)";
                return out;
            }
            out.hasProject = true;
            out.projectPath = value;
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
