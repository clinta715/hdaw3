#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>

namespace HDAW {

// Debug-only thread-identity tripwires. Mirrors the technique CLAPHost uses
// for thread-check predicates (lesson 19): record real thread ids, compare
// against them — never "not X" complements.
class RealtimeGuard
{
public:
    // Call once from prepareToPlay / the audio device callback to record the
    // real audio thread id.
    static void recordAudioThreadId(void* threadId) noexcept
    {
        audioThreadId_.store(threadId, std::memory_order_release);
    }

    static bool isAudioThread() noexcept
    {
        return isAudioThreadFor(juce::Thread::getCurrentThreadId());
    }

    static bool isAudioThreadFor(void* threadId) noexcept
    {
        return audioThreadId_.load(std::memory_order_acquire) == threadId;
    }

    // True if the current thread is the JUCE message thread. Used by the
    // rebuild/restore paths (lesson 10/12/13) to assert they run message-side.
    static bool isMessageThread() noexcept
    {
        return juce::MessageManager::getInstance()->isThisTheMessageThread();
    }

    // Audio-thread-side helper for lock-block detection. Callers that would
    // otherwise use a blocking ScopedLockType call this instead; it flags a
    // pending problem atomically and returns the passed-in result (false = the
    // lock could not be acquired without blocking).
    static bool tryEnterLock(bool acquired) noexcept
    {
        if (!acquired)
            audioBlocked_.store(true, std::memory_order_release);
        return acquired;
    }

    static bool lastBlockWasAudioThreadLock() noexcept
    {
        return audioBlocked_.exchange(false, std::memory_order_acq_rel);
    }

    static void resetForTest() noexcept
    {
        audioThreadId_.store(nullptr, std::memory_order_release);
        audioBlocked_.store(false, std::memory_order_release);
    }

private:
    inline static std::atomic<void*> audioThreadId_{ nullptr };
    inline static std::atomic<bool> audioBlocked_{ false };
};

} // namespace HDAW