#pragma once
// fm_synth slot state read — ONE shaping shared by the MCP tool
// fm_synth_get_state (src/mcp/McpTools_FmSynth.cpp) and the JSON-RPC route
// read.getFmSynthState, per the AGENTS.md parity contract.
//
// Deliberately NOT read.getFmAnalysis: that route reads the ANALYSIS voice
// count + the engine algorithm for the track's FIRST non-bypassed fm_synth
// slot; this read reports the live processor's activeVoiceCount for the
// caller-chosen (trackId, slotIndex) + the slot TREE's param_0 (the algorithm
// the rebuild restores) — different sources, documented in the ledger note.
// Header-only; JUCE + Qt only.
#include "../engine/AudioEngine.h"
#include "../engine/MainAudioProcessor.h"
#include "../engine/Track.h"
#include "../engine/TrackFXSlot.h"
#include "../model/ProjectModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace HDAW {

// fm_synth_get_state: live activeVoices (best-effort; absent when no live
// processor/track/slot/engine) + the tree's param_0 for the chosen slot.
// Returns the exact tool text; outOk=false <=> the tool answered isError=true
// ("slot not found" / "slot is not an FM synth").
inline QString fmSynthStateToolText(AudioEngine& e, int trackId, int slotIndex,
                                    bool* outOk = nullptr)
{
    if (outOk) *outOk = false;

    auto fxSlots = e.getReadModel().getFxSlots(trackId);
    if (slotIndex < 0 || slotIndex >= (int)fxSlots.size())
        return "slot not found";
    if (fxSlots[slotIndex].fxType != "fm_synth")
        return "slot is not an FM synth";

    QJsonObject state;
    auto* proc = e.getMainProcessor();
    if (proc)
    {
        auto* track = proc->getTrack(trackId);
        if (track)
        {
            auto& chain = track->getFXChain();
            if (slotIndex < (int)chain.size() && chain[slotIndex])
            {
                auto* engine = chain[slotIndex]->fmSynthEngine();
                if (engine)
                    state["activeVoices"] = engine->activeVoiceCount();
            }
        }
    }
    auto& model = e.getProjectModel();
    auto slotTree = model.getTrackListTree().getChild(trackId)
        .getChildWithName(IDs::FX_CHAIN).getChild(slotIndex);
    state["algorithm"] = slotTree.isValid()
        ? static_cast<int>(slotTree.getProperty("param_0", 0)) : 0;

    if (outOk) *outOk = true;
    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

} // namespace HDAW
