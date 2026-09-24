#pragma once
// fm_synth patch loading — ONE entry point shared by the MCP tool
// fm_synth_load_preset (src/mcp/McpTools_FmSynth.cpp) and the JSON-RPC route
// audio.fmSynthLoadPreset (src/frontend/router/Router_Audio.cpp), per the
// AGENTS.md parity contract: both surfaces must answer the SAME text for the
// SAME arguments, so the shaping lives here and each surface only reads its
// own argument names (trackId on MCP, trackIndex on RPC — historical wire
// names; extraction stays surface-side, loadNordBank precedent).
//
// The loader routes through ProjectCommands::setFmPatch, the tree-first write
// (IDs::fmPatchData on the FX-slot tree + best-effort live load) that
// tree-copy renders and save/load restore — never a live-only write.
// Header-only; JUCE + Qt only.
#include "../engine/AudioEngine.h"
#include "../engine/FmSynthEngine.h"

#include <QString>

#include <string>

namespace HDAW {

// fm_synth_load_preset: load a raw DX7 patch (156 bytes as a 312-char hex
// string) into an FM synth FX slot. Returns the exact tool text ("ok" on
// success, the failure text otherwise); *outOk distinguishes the two
// (outOk=false <=> the MCP tool answered isError=true).
inline QString fmLoadPresetToolText(AudioEngine& e, int trackId, int slotIndex,
                                    const QString& patchDataHex,
                                    bool* outOk = nullptr)
{
    if (outOk) *outOk = false;

    auto fxSlots = e.getReadModel().getFxSlots(trackId);
    if (slotIndex < 0 || slotIndex >= (int)fxSlots.size())
        return QStringLiteral("slot not found");
    if (fxSlots[slotIndex].fxType != "fm_synth")
        return QStringLiteral("slot is not an FM synth");

    if (patchDataHex.isEmpty() || patchDataHex.size() != 312)
        return QStringLiteral("patchData must be 312 hex characters (156 bytes)");

    uint8_t patch[FmSynthEngine::kPatchSize];
    for (int i = 0; i < FmSynthEngine::kPatchSize; i++)
    {
        bool ok = false;
        patch[i] = static_cast<uint8_t>(patchDataHex.mid(i * 2, 2).toInt(&ok, 16));
        if (!ok) return QStringLiteral("invalid hex at offset ") + QString::number(i);
    }

    // Route through the command: writes fmPatchData to the slot tree (so
    // tree-copy renders and save/load hear it) and applies live best-effort.
    // Works without an audio device.
    juce::MemoryBlock block(patch, FmSynthEngine::kPatchSize);
    e.getProjectCommands().setFmPatch(trackId, slotIndex,
                                      block.toBase64Encoding().toStdString());

    if (outOk) *outOk = true;
    return QStringLiteral("ok");
}

} // namespace HDAW
