#include "CrashRecoveryManager.h"
#include <chrono>
#include <vector>

namespace HDAW {

constexpr int64_t CrashRecoveryManager::kBackoffMs[3];

void CrashRecoveryManager::onSlotCrashed(uint32_t slotId, const juce::String& pluginName,
                                         const juce::String& pluginPath) {
    std::lock_guard<std::mutex> lock(mutex);
    auto& entry = entries[slotId];
    entry.pluginPath = pluginPath;
    entry.pluginName = pluginName;
    entry.oldSlotId = slotId;
    entry.crashedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    entry.nextRetryMs = entry.crashedAtMs + kGracePeriodMs;
    entry.pendingRespawn.store(true);
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
    }
    for (auto& [id, entry] : due) {
        attemptRespawn(*entry);
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

void CrashRecoveryManager::attemptRespawn(RecoveryEntry& entry) {
    entry.attemptCount++;
    entry.pendingRespawn.store(false);

    if (entry.attemptCount > kMaxAttempts) {
        if (giveUpFn) giveUpFn(entry.oldSlotId, entry.pluginName);
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(entry.oldSlotId);
        return;
    }

    bool ok = false;
    if (respawnFn) ok = respawnFn(entry.oldSlotId, entry.pluginPath);

    if (ok) {
        std::lock_guard<std::mutex> lock(mutex);
        entries.erase(entry.oldSlotId);
        return;
    }

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    entry.nextRetryMs = nowMs + kBackoffMs[entry.attemptCount >= 3 ? 2 : (entry.attemptCount - 1)];
    entry.pendingRespawn.store(true);
}

} // namespace HDAW
