#pragma once
namespace mcp { class McpServer; }
namespace mcp { void registerExportTool(McpServer& server); }
namespace mcp { void registerCancelExportTool(McpServer& server); }
// S4 (docs/plans/2026-09-28-agent-mechanization.md §5): the render→measure pair
// that shares ONE implementation with its RPC twin
// (composition.verifyWindow / export.renderAndVerify).
namespace mcp { void registerVerifyWindowTool(McpServer& server); }
namespace mcp { void registerRenderAndVerifyTool(McpServer& server); }
