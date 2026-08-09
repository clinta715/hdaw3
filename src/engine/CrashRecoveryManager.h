#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <mutex>

namespace HDAW {

class CrashRecoveryManager {
public:
    struct RecoveryEntry {
        juce::String pluginPath;
        juce::String pluginName;
        uint32_t oldSlotId = 0;
        std::atomic<bool> pendingRespawn{false};
        int attemptCount{0};
        int64_t crashedAtMs{0};
        int64_t nextRetryMs{0};
    };

    using PluginRespawnFn = std::function<bool(uint32_t oldSlotId, const juce::String& pluginPath)>;
    using GiveUpFn = std::function<void(uint32_t slotId, const juce::String& pluginName)>;

    explicit CrashRecoveryManager() = default;

    void onSlotCrashed(uint32_t slotId, const juce::String& pluginName,
                       const juce::String& pluginPath);
    void tick();
    void requestRespawn(uint32_t slotId, bool immediate);

    // Cancels a pending recovery entry for a slot. Called from the proxy
    // destruction callback when a slot is torn down (e.g. at export teardown)
    // so a stale respawn can't dereference a freed proxy.
    void cancel(uint32_t slotId);

    // Global suppression gate for offline export. When false, attemptRespawn
    // returns early and leaves the entry pending so it can be retried (or
    // canceled by the destruction callback) once export finishes. Set false
    // by ExportManager for the entire export duration (including teardown).
    std::atomic<bool> respawnEnabled{ true };

    void setRespawnFn(PluginRespawnFn fn) { respawnFn = std::move(fn); }
    void setGiveUpFn(GiveUpFn fn) { giveUpFn = std::move(fn); }

private:
    std::mutex mutex;
    std::unordered_map<uint32_t, RecoveryEntry> entries;
    PluginRespawnFn respawnFn;
    GiveUpFn giveUpFn;

    static constexpr int kMaxAttempts = 3;
    static constexpr int64_t kGracePeriodMs = 500;
    static constexpr int64_t kBackoffMs[3] = {1000, 2000, 4000};

    void attemptRespawn(RecoveryEntry& entry);
};

} // namespace HDAW
