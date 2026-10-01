#pragma once
// ProjectPathCheck — the shared save/load filePath gate (fix 2026-09-30,
// lesson-34/38 class: `save_project {"filePath":"compositions/x.hdaw"}` returned
// ok and wrote NOTHING the caller can find — juce::File resolves a relative path
// against the ENGINE process CWD (a Temp dir on the MCP surface), so the ok only
// reflects a CWD-anchored stream write, never the caller's filesystem).
//
// BOTH surfaces call this so the refusal text stays byte-identical
// (MCP-RPC parity, asserted by tests/unit/frontend/add_fx_parity_test.cpp):
//   - MCP `save_project` / `load_project` — src/mcp/McpTools_ProjectSaveLoad.cpp
//   - RPC `project.saveProject` / `project.loadProject` — src/frontend/router/Router_Project.cpp
//
// Returns "" when filePath is acceptable (absolute), otherwise the exact error
// both surfaces must report (MCP isError text / RPC -32602 message).
//
// A relative path is REFUSED, never resolved against the CWD — silently
// anchoring the save root to the engine process' working directory is the bug.

#include <QString>
#include <QDir>

namespace HDAW {

inline QString projectPathError(const QString& filePath)
{
    if (QDir::isAbsolutePath(filePath))
        return {};
    return QString("filePath must be absolute (got \"%1\") — relative paths "
                   "are not resolved against the engine process CWD")
        .arg(filePath);
}

} // namespace HDAW
