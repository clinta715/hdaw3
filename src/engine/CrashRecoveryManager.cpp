#include "CrashRecoveryManager.h"
#include "../common/DebugLog.h"
#include <chrono>
#include <vector>

namespace HDAW {

constexpr int64_t CrashRecoveryManager::kBackoffMs[3];

CrashRecoveryManager::CrashRecoveryManager() {
    // Read the storm-breaker budget once (precedent: PluginManager.cpp:34,68 /
    // FrontendTreeWatcher.cpp:25-29 read env flags once at startup).
    const auto budgetStr = juce::SystemStats::getEnvironmentVariable("HDAW_RESPAWN_BUDGET", {});
    if (budgetStr.isNotEmpty()) {
        const int parsed = budgetStr.getIntValue();
        if (parsed > 0) respawnBudget = parsed;
    }
    const auto windowStr = juce::SystemStats::getEnvironmentVariable("HDAW_RESPAWN_WINDOW_MS", {});
    if (windowStr.isNotEmpty()) {
        const int64_t parsed = windowStr.getLargeIntValue();
        if (parsed > 0) respawnWindowMs = parsed;
    }
}

void CrashRecoveryManager::onSlotCrashed(uint32_t slotId, const juce::String& pluginName,
                                         const juce::String& pluginPath) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(slotId);
    const bool brandNew = (it == entries.end());
    auto& entry = brandNew ? entries[slotId] : it->second;
    entry.pluginPath = pluginPath;
    entry.pluginName = pluginName;
    entry.oldSlotId = slotId;
    entry.crashedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    entry.nextRetryMs = entry.crashedAtMs + kGracePeriodMs;
    entry.pendingRespawn.store(true);
    if (brandNew)
        entry.attemptCount = 0;
}

void CrashRecoveryManager::tick() {
    std::vector<std::pair<uint32_t, RecoveryEntry*>> due;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        for (auto& [id, entry] : entries) {
            if (entry.pendingRespawn.load() && nowMs >= entry.nextRetryMs) {
                due.emplace_back(id, &entry);
            }
        }
        breakerTrippedInBatch = false;
    }
    for (auto& [id, entry] : due) {
        attemptRespawn(*entry);
    }
    if (breakerTrippedInBatch) {
        // Once per tick batch, not per entry: a storm flags dozens of slots
        // per sweep and per-entry logging would flood the log.
        HDAW_LOG("CrashRecovery",
            juce::String("respawn breaker tripped: budget ") + juce::String(respawnBudget)
            + juce::String(" respawns per ") + juce::String((int)respawnWindowMs)
            + juce::String(" ms exhausted; remaining entries stay pending (self-heals as timestamps age out)"));
    }
}

void CrashRecoveryManager::requestRespawn(uint32_t slotId, bool immediate) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entries.find(slotId);
    if (it == entries.end()) return;
    if (immediate) {
        it->second.nextRetryMs = 0;
    }
    it->second.pendingRespawn.store(true);
}

void CrashRecoveryManager::cancel(uint32_t slotId) {
    std::lock_guard<std::mutex> lock(mutex);
    entries.erase(slotId);
}

size_t CrashRecoveryManager::numEntries() const {
    std::lock_guard<std::mutex> lock(mutex);
    return entries.size();
}

void CrashRecoveryManager::attemptRespawn(RecoveryEntry& entry) {
    // Suppression gate: during offline export, respawn is disabled so a
    // crashed plugin fails the export instead of respawning into a
    // half-rendered file (and racing the export render thread's
    // processBlock via kill+swap). The entry stays pending and is either
    // retried after export restores the flag or canceled by the proxy
    // destruction callback when export tears down.
    if (!respawnEnabled.load(std::memory_order_relaxed))
        return;

    // Global storm breaker (lesson 21): cap respawns across ALL entries in a
    // sliding window. Runs BEFORE attemptCount++ so a denied entry keeps its
    // place in the per-entry backoff/give-up ladder, and timestamps are only
    // recorded for respawns actually attempted (respawnFn invoked).
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        while (!respawnAttemptTimes.empty()
               && respawnAttemptTimes.front() <= nowMs - respawnWindowMs) {
            respawnAttemptTimes.pop_front();
        }
        if (static_cast<int>(respawnAttemptTimes.size()) >= respawnBudget) {
            // Exhausted: leave the entry pending for a later tick; do NOT
            // count this as an attempt.
            entry.nextRetryMs = nowMs + 2000;
            entry.pendingRespawn.store(true);
            breakerTrippedInBatch = true;
            return;
        }
        respawnAttemptTimes.push_back(nowMs);
    }

    entry.attemptCount++;
    entry.pendingRespawn.store(false);

    if (entry.attemptCount > kMaxAttempts) {
        if (giveUpFn) giveUpFn(entry.oldSlotId, entry.pluginName);
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(entry.oldSlotId);
        return;
    }

    // respawnFn may cancel this entry (PluginManager::respawnIsolatedSlot
    // tears it down when the proxy is gone), which erases — and destroys —
    // the map node `entry` refers to. Capture the ids before the call and
    // re-find under the mutex afterwards; a missing entry is final.
    const auto slotIdCopy = entry.oldSlotId;
    const int attemptCopy = entry.attemptCount;
    const juce::String pluginPathCopy = entry.pluginPath;

    bool ok = false;
    if (respawnFn) ok = respawnFn(slotIdCopy, pluginPathCopy);

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = entries.find(slotIdCopy);
        if (it == entries.end())
            return;
        if (ok) {
            entries.erase(it);
            return;
        }
        it->second.nextRetryMs = nowMs + kBackoffMs[attemptCopy >= 3 ? 2 : (attemptCopy - 1)];
        it->second.pendingRespawn.store(true);
    }
}

} // namespace HDAW
