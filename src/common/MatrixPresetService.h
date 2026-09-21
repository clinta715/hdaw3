#pragma once
// Matrix-preset engine surface — the SINGLE implementation behind both the MCP
// tools (list_matrix_presets / apply_matrix_preset) and the RPC methods
// (matrix.listPresets / matrix.applyPreset).
//
// Parity by construction: the payload objects, the dispatch routes and the error
// text are built HERE, so the two surfaces cannot drift (the same pattern as
// src/common/DeviceParamMap.cpp and src/common/PsyFmModMatrixView.cpp). Before
// 2026-09-21 this logic lived only in src/mcp/McpTools_Matrix.cpp — which is why
// the whole Matrix domain had no RPC route at all.
//
// Engine surface ONLY: bounded file reads + JSON shaping + dispatch onto EXISTING
// engine entry points (PluginParamService::setParam, ProjectCommands::sendFxMidi /
// captureFxSlotState, the pure Nord .syx parser). No DSP, no audio-thread code, no
// ValueTree schema changes, no plugin instantiation.
//
// Sheets (timbre-lib/matrix_presets/):
//   <engine>.json                 schema hdaw.matrix.preset.v1
//   <engine>_morphs.json          schema hdaw.matrix.preset.morph.v1 (xenia:
//                                 xenia_morphs_injectable.json preferred — the
//                                 variant whose steps carry injectable SysEx)
//   <engine>_param_index_map.json decoder name -> live plugin param index
// Location: env HDAW_MATRIX_PRESETS_DIR first, else <cwd>/timbre-lib/matrix_presets,
// <exeDir>/../timbre-lib/matrix_presets, <exeDir>/timbre-lib/matrix_presets. The
// resolved dir is cached (keyed on the env value so an env change re-resolves).
//
// Plans: docs/plans/2026-09-16-matrix-presets.md,
//        docs/plans/2026-09-21-rpc-parity-retrofit.md (slice 2).

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <string>

class AudioEngine;

namespace HDAW {

// Outcome of a matrix operation. `error` is surface-neutral text: MCP renders it as
// an in-band tool error, the RPC as a JSON-RPC error object.
struct MatrixOpResult
{
    bool ok = false;
    std::string error;
    // JSON-RPC code hint so the RPC surface classifies failures exactly like the MCP
    // surface does. The MCP surface only needs the text.
    //   -32602 invalid params : invalid engine id, no sheet for a NAMED engine
    //                           (Router_Device precedent), missing/unknown preset or
    //                           morph-step id, unknown track/slot, non-plugin slot,
    //                           appliesVia with no parameter-level path, malformed
    //                           preset SysEx, program out of range.
    //   -32603 environment    : presets dir missing, sheet unreadable / invalid JSON /
    //                           unsupported schema, index-map unreadable, missing or
    //                           invalid .syx step file, engine-command failure.
    int errorCode = -32602;
    QJsonObject payload;   // identical for every surface on success
};

// list_matrix_presets / matrix.listPresets — {engine, sheet, presets[], morphs[]}.
MatrixOpResult listMatrixPresets(const QString& engine);

// apply_matrix_preset / matrix.applyPreset — dispatches on the preset or morph-step
// id, in this order: device-native SysEx dump -> parameter level -> injectable SysEx
// morph step -> loose .syx morph step file. Route payloads:
//   device_dump : {queued, route, bytes, baseSyx, captureDeferred}
//   params      : {applied, skipped, unmapped, captureToTree, [paramOverrides], [capture]}
//   sysex step  : {queued, route, bytes, captureDeferred}
//   file step   : {queued, route, bytes, program, capturedToTree, captureDeferred}
MatrixOpResult applyMatrixPreset(AudioEngine& engine, const QString& engineId,
                                 const QString& presetId, int trackIndex, int slotIndex,
                                 bool captureToTree);

// Shared argument/env helpers (both surfaces use them for validation + messages).
bool validMatrixEngineId(const QString& engine);
QString matrixPresetsDir(QString* error = nullptr);   // resolved + cached
QStringList matrixPresetEngines();                    // for "available: ..." hints

} // namespace HDAW
