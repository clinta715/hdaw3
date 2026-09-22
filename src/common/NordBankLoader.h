#pragma once
// The ONE Clavia Nord bank loader — shared by the MCP tools `load_nord_bank` and
// `apply_preset`'s NordBank route (both via mcp::runNordBankFile) and by the matrix tool's
// loose `.syx` step-file route (via MatrixPresetService).
//
// WHY (retrofit backlog item 4, docs/plans/2026-09-21-rpc-parity-retrofit.md): before this the same
// wire-format dance existed TWICE — mcp::runNordBankFile (PresetRoute.h, prose output) and
// MatrixPresetService's applyNordSyxFile (JSON payload) — with the parse/validate/queue sequence
// and every error string maintained by hand in both. Slice 2 needed a structured payload for the
// RPC surface and had to re-implement it; this is that duplication removed.
//
// The wire-format authority stays the PURE parser (src/mcp/PresetFileParser.h — juce_core only, no
// MCP server/tool types): splitNordSyx normalizes .syx/.mid payloads to complete F0..F7 dumps and
// validateNordDump checks the Clavia header. Each caller formats its own output from the result
// below.
//
// SEQUENCE (unchanged): validate EVERY dump before queueing anything (no partial bank loads), then
// queue the dumps + an optional trailing program change, plus a harmless CC125 at the END so the
// deferred state capture sees inert trailing state (capture-race protocol).

#include <QString>

class AudioEngine;   // engine/AudioEngine.h

namespace HDAW {

struct NordBankLoadResult
{
    bool ok = false;
    // Error class for the RPC surface: true => artifact/environment problem (JSON-RPC -32603:
    // missing/unreadable/undecodable file, invalid dump, engine failure); false => invalid params
    // (-32602: unsupported container, program out of range). The MCP surface only needs the text.
    bool environmentFailure = true;
    QString error;        // exact messages preserved from the pre-dedupe implementations
    int queued = 0;       // FxMidi events queued (dumps + optional PC + the trailing CC125)
    int totalBytes = 0;   // summed SysEx dump bytes
    int program = -1;     // echoed (-1 = none requested)
    bool capturedToTree = false;
};

// Loads `path` (.syx or .mid) into the plugin slot at (trackIndex, slotIndex).
NordBankLoadResult loadNordBankFile(AudioEngine& engine, int trackIndex, int slotIndex,
                                    const QString& path, int program, bool captureToTree);

} // namespace HDAW
