#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "RoutingManager.h"
#include "TransportManager.h"
#include "../model/ProjectModel.h"
#include <atomic>
#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace HDAW {

class ExportManager
{
public:
    ExportManager();
    ~ExportManager();

    enum Format { WAV, AIFF, FLAC };

    bool startExport(const juce::ValueTree& projectTree, juce::AudioFormatManager& formatManager,
                     PluginManager* pluginManager, const juce::File& outputPath,
                     double sampleRate, double startTime, double duration,
                     Format format = WAV, int bitDepth = 24);

    void cancel();
    bool isExporting() const { return active.load(); }
    bool usesDedicatedDomain() const { return dedicatedDomainActive.load(std::memory_order_acquire); }

    // Last result message produced by the most recent render thread
    // (exact: "Export complete." on success; descriptive failure text
    // otherwise). Mutex-guarded; never touched by the audio thread. Waiters
    // should read it only after isExporting() went false.
    juce::String getLastExportMessage() const
    {
        std::lock_guard<std::mutex> lock(lastMsgMutex);
        return lastMessage;
    }

    // Cancel any in-flight render and JOIN the render thread so nothing it
    // touches outlives this call (handoff B1: an orphaned windowed render
    // racing a routing-graph rebuild killed the engine). Bounded drain poll,
    // then an unbounded join — a wedged render is a broken engine; correctness
    // over liveness, but log loudly before blocking.
    void cancelAndJoin(uint32_t drainTimeoutMs = 10000);

    // Poll active with 10ms sleep until idle or timeout/cancel. Returns true
    // if idle before deadline, false on timeout or cancellation. Used by
    // queue=true export paths (MCP + frontend) to wait for a prior export
    // without TOCTOU: caller waits then re-attempts startExport CAS.
    bool waitForIdle(uint32_t timeoutMs);

    std::function<void(float)> onProgress;
    std::function<void(bool success, const juce::String& message)> onComplete;

    static double calculateProjectDuration(ProjectModel& model);

    // Returns the render-sequence bake wait budget (ms) for a project. Large
    // graphs legitimately take >15s to bake on the JUCE message thread
    // (measured ~17-21s for a 771-clip project), so the default scales with
    // clip count: floor 15s, 50ms per clip, cap 120s. The env override
    // HDAW_EXPORT_BAKE_TIMEOUT_MS takes precedence at the call site.
    static uint32_t computeBakeWaitMs(const juce::ValueTree& projectTree);

    // Offline param-override replay (matrix-preset apply ledger,
    // IDs::appliedParamOverrides — McpTools_Matrix.cpp writes it, the render
    // thread replays it). The pluginState capture is a dead end for plugins
    // whose getStateInformation does not serialize param-driven state
    // (JE8086, measured 2026-09-16), so the RESOLVED {liveParamIndex,
    // normalizedValue} pairs travel on the FX_SLOT tree instead.
    struct ParamReplayStats
    {
        int slotsWithOverrides = 0;
        int applied = 0;
        int skippedBeyondCache = 0;
    };

    // Parses ONE FX_SLOT's ledger into (liveParamIndex, normalizedValue)
    // pairs. Absent/empty/malformed property -> empty. Test seam for the
    // replay dispatch: the pairs feed setAutomationParam verbatim.
    static std::vector<std::pair<int, float>>
    parseAppliedParamOverrides(const juce::ValueTree& slotTree);

    // Replays the ledger for every OFFLINE FX slot that carries one: seeds
    // the slot's atomic param cache (TrackFXSlot::setAutomationParam) so the
    // applyAutomation dirty-flag push delivers the overrides starting with
    // the FIRST rendered block. Must run after the render-sequence bake wait
    // (isolated children boot + publish their param lists there) and before
    // the block loop. Slots without the ledger are untouched (zero behavior
    // change); indexes beyond a slot's param cache are counted in
    // skippedBeyondCache. Realtime-safe: runs on the render thread BEFORE any
    // processBlock; setAutomationParam is a relaxed atomic store. Logs under
    // "ParamReplay".
    static ParamReplayStats replayAppliedParamOverrides(const juce::ValueTree& projectTree,
                                                        RoutingManager& routing);

private:
    void renderThreadFunc(juce::ValueTree projectTree, juce::AudioFormatManager* formatManager,
                          PluginManager* pluginManager, juce::File outputPath,
                          double sampleRate, double startTime, double duration,
                          Format format, int bitDepth);

    std::atomic<bool> active{ false };
    std::atomic<bool> cancelFlag{ false };
    std::atomic<bool> dedicatedDomainActive{ false };
    std::thread renderThread;
    mutable std::mutex lastMsgMutex;
    juce::String lastMessage;
};

} // namespace HDAW
