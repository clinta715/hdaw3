#pragma once
// Deferred plugin-state capture receipt for one FX slot — the SINGLE read behind
// the MCP tool get_fx_capture_status and the RPC method audio.getFxCaptureStatus
// (AGENTS.md "Feature parity: MCP + RPC"; docs/plans/2026-09-21-rpc-parity-retrofit.md
// slice 3). Both surfaces read the same slot properties here, so the receipt cannot
// disagree between them.
//
// The receipt is written by the capture path (ProjectCommands::captureFxSlotState —
// the send_fx_midi trigger): "pending" while a deferred capture is in flight, "ok"
// when the injected preset landed in IDs::pluginState, "unchanged" when the state
// came back byte-identical (e.g. a no-op edit-buffer dump), or "failed: <reason>".
// Absent receipt => "none".
//
// READ-ONLY: four property reads, no mutation.

#include <QString>

#include <juce_data_structures/juce_data_structures.h>

namespace HDAW {

struct FxCaptureStatus
{
    QString status = "none";   // pending | ok | unchanged | failed: ... | none
    int stateBytes = 0;        // captured IDs::pluginState size
    qint64 capturedAtMs = 0;   // stamp of the receipt
    bool hasPluginState = false;
};

// Pass the FX_SLOT ValueTree (or an invalid one for an all-default receipt).
FxCaptureStatus readFxCaptureStatus(const juce::ValueTree& slotTree);

} // namespace HDAW
