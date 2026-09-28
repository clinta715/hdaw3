#pragma once
#include <QJsonObject>
namespace mcp { class McpServer; }
class AudioEngine;
namespace mcp {
void registerEngineInfoTool(McpServer& server);
void registerEngineRestartTool(McpServer& server);
void registerWhoamiTool(McpServer& server);
void registerToolHelpTool(McpServer& server);

// The ONE refusal text for tool_help's unknown tool name (MCP_ONLY: it describes
// the MCP tool registry, which the RPC surface does not have — the
// engine_info/whoami precedent). Shared so the tool and its test assert the
// same bytes.
inline QString unknownToolHelpText(const QString& name) { return "unknown tool " + name; }

// The ONE engine-introspection field builder shared by `engine_info` and
// `whoami`, so the two payloads cannot drift: `whoami` reports every key this
// returns with an equal value (ToolRegistry/EngineTools test enforces it).
// `e` may be null (the export flag then reports false). `args` accepts the same
// optional buildBinaryPath / expectedVersion strings as engine_info.
QJsonObject buildEngineInfoPayload(McpServer& server, AudioEngine* e,
                                   const QJsonObject& args);
} // namespace mcp
