#pragma once
// P1-a — the DEVICELESS live-slot diagnostic, in one place.
//
// Measured 2026-09-30 (ion_rift session): with no audio output device open,
// processBlock never runs, the live routing graph is never clocked into
// existence, and every live-slot tool that resolves its track through
// AudioEngine::ensureLiveRouting fails. ensureLiveRouting's bounded fallback
// (MainAudioProcessor::rebuildRoutingGraph) no-ops without a RoutingManager, so
// a deviceless session cannot be rescued — yet the error used to say
// "track not found: N", blaming the TRACK for a DEVICE problem, for tracks that
// list_tracks plainly lists.
//
// The fix is a DIAGNOSTIC, not a routing change: when the live-track lookup
// fails AND no output device is open, name the device state and the fix
// (set_audio_output_device). When a device IS open the text stays byte-identical
// to the pre-fix message ("track not found: N") — existing tests and agents
// match on it verbatim.
//
// Pure by construction (a bool + a name; no engine, no hardware, no Qt), so
// BOTH branches are unit-testable on a machine with no audio device at all.
//
// Header-only (all inline), so no CMake source registration is needed.

#include <string>

namespace HDAW {

// `deviceOpen` is the engine's own "a device is clocking the graph" test —
// AudioDeviceManager::getCurrentAudioDevice() != nullptr, the same flag
// captureFxSlotState already derives in AudioEngineCommands_Fx.cpp.
// `outputDeviceName` is AudioDeviceManager::getAudioDeviceSetup().
// outputDeviceName — the CONFIGURED output, which can be non-empty while
// nothing is open; pass "" when there is nothing to name.
inline std::string liveTrackNotFoundError(int trackIndex, bool deviceOpen,
                                          const std::string& outputDeviceName)
{
    if (deviceOpen)
        return "track not found: " + std::to_string(trackIndex);

    std::string msg = "track not found: " + std::to_string(trackIndex)
        + " - no audio output device is open";
    if (!outputDeviceName.empty())
        msg += " (\"" + outputDeviceName + "\" is configured but not open)";
    msg += ": the live graph only runs while a device clocks it, so a live-slot tool"
           " cannot see a track that list_tracks still lists - open one with"
           " set_audio_output_device";
    return msg;
}

} // namespace HDAW
