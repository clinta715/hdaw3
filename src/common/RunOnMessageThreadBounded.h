#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>

// Runs fn on the JUCE message thread and waits (bounded). Runs inline when
// already on the message thread or no MessageManager exists. Returns false on
// timeout (fn may still run later — the shared state outlives the wait).
// Rethrows any exception thrown by fn. Same shape as
// PluginHost::runLifecycleOnMessageThread (lesson 16).
inline bool runOnMessageThreadBounded(const std::function<void()>& fn, int timeoutMs)
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || mm->isThisTheMessageThread())
    {
        fn();
        return true;
    }

    struct State
    {
        std::function<void()> fn;
        std::atomic<bool> done{ false };
        std::exception_ptr ep;
    };
    auto st = std::make_shared<State>();
    st->fn = fn;

    juce::MessageManager::callAsync([st]() {
        try { st->fn(); }
        catch (...) { st->ep = std::current_exception(); }
        st->done.store(true, std::memory_order_release);
    });

    const auto deadline = juce::Time::getMillisecondCounter()
        + static_cast<uint32_t>(timeoutMs);
    while (!st->done.load(std::memory_order_acquire))
    {
        if (juce::Time::getMillisecondCounter() >= deadline)
            return false;
        juce::Thread::sleep(1);
    }
    if (st->ep)
        std::rethrow_exception(st->ep);
    return true;
}
