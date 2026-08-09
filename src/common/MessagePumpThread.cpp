#include "MessagePumpThread.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace HDAW {

MessagePumpThread::~MessagePumpThread()
{
    stop();
}

MessagePumpThread& MessagePumpThread::instance()
{
    // Function-local static: constructed on first use; thread-safe.
    static MessagePumpThread pump;
    return pump;
}

bool MessagePumpThread::start()
{
    auto& p = instance();

    if (p.started.exchange(true))
        return false; // idempotent; already running (or already tried)

    p.thread = std::thread(&MessagePumpThread::pumpLoopStatic, &p);

    // Block until the pump thread has acquired the message loop, so callers
    // can rely on JUCE messaging being live the moment start() returns.
    std::unique_lock<std::mutex> lk(p.mtx);
    const bool ok = p.cv.wait_for(lk, std::chrono::seconds(5),
                                  [&p] { return p.acquired || p.stopRequested; });
    if (!ok || p.stopRequested)
    {
        // Rare: failed to acquire in time. Do not leave a half-started pump;
        // join and report failure so entry points can fall back.
        lk.unlock();
        if (p.thread.joinable())
            p.thread.join();
        return false;
    }
    return true;
}

void MessagePumpThread::stop()
{
    auto& p = instance();

    {
        std::lock_guard<std::mutex> lk(p.mtx);
        if (p.stopRequested)
            return;
        p.stopRequested = true;
    }
    p.cv.notify_all();

    if (p.thread.joinable())
        p.thread.join();
}

bool MessagePumpThread::isOwned()
{
    auto& p = instance();
    std::lock_guard<std::mutex> lk(p.mtx);
    return p.acquired;
}

void MessagePumpThread::pumpLoopStatic(MessagePumpThread* self)
{
    self->pumpLoop();
}

void MessagePumpThread::pumpLoop()
{
    // CRITICAL ORDER: constructing MessageManager::getInstance() here FIRST
    // (on this thread) fixes messageThreadId AND InternalMessageQueue
    // ownership (hidden window + per-thread Win32 queue) to this thread.
    // Any AsyncUpdater / graph topology update posted from ANY thread lands
    // in the queue this thread drains.
    juce::MessageManager::getInstance();

    {
        std::lock_guard<std::mutex> lk(mtx);
        acquired = true;
    }
    cv.notify_all();

    while (true)
    {
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (stopRequested)
                break;
        }

        // Bounded pump: dispatch whatever is pending, then yield. The child
        // host uses the identical pattern (PluginHost.cpp:499-506) to keep
        // CLAP on_main_thread callbacks alive during export.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(0);
        juce::Thread::sleep(1);
    }
}

} // namespace HDAW