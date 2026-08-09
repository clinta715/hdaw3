#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace HDAW {

// Process-wide JUCE message-pump thread.
//
// JUCE's AudioProcessorGraph bakes its render sequence on the JUCE message
// thread (juce_AudioProcessorGraph.cpp:1857-1866) - i.e. the thread that FIRST
// called MessageManager::getInstance() owns the hidden message window and the
// message queue (juce_Messaging_windows.cpp:91-121). In GUI processes the UI
// thread pumps that queue; in headless/test processes NOTHING does, so every
// topology change (graph rebuild, addConnection, prepareToPlay) sits in the
// queue forever and Pimpl::processBlock falls into the audio.clear()
// early-out - rendering silence.
//
// This class must therefore be started at the VERY TOP of each process entry
// point (before any other JUCE object construction), so its thread wins
// MessageManager messageThreadId + InternalMessageQueue ownership, and then
// keeps the queue permanently drained. start() is idempotent and thread-safe;
// it BLOCKS until the pump thread has acquired the message loop (deterministic
// order, no cold-start race).
class MessagePumpThread
{
public:
    // Starts the pump if not running. Returns false if already started or
    // if the pump failed to acquire the message loop within 5s.
    static bool start();
    static void stop();

    // True once this process' pump thread owns the JUCE message loop.
    static bool isOwned();

private:
    static MessagePumpThread& instance();

    MessagePumpThread() = default;
    ~MessagePumpThread();

    void pumpLoop();
    static void pumpLoopStatic(MessagePumpThread* self);

    std::thread thread;
    std::mutex mtx;
    std::condition_variable cv;
    bool acquired = false;
    bool stopRequested = false;
    std::atomic<bool> started { false };
};

} // namespace HDAW