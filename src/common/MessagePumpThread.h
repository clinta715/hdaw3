#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace juce { class ScopedJuceInitialiser_GUI; }

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
//
// OWNERSHIP PIN: the pump also PINS the JUCE GUI initialisation for its own
// lifetime (juceInitPin below). Merely winning the race for the FIRST
// getInstance() is not enough: `juce::ScopedJuceInitialiser_GUI` calls
// `shutdownJuce_GUI()` when its LAST instance is destroyed, and that does
// `DeletedAtShutdown::deleteAll()` + `MessageManager::deleteInstance()` -
// deleting the JUCE message manager AND stopping JUCE's TimerThread
// (juce_Timer.cpp:38-80 ShutdownDetector -> applicationShuttingDown). The next
// `MessageManager::getInstance()` then re-creates it on whichever thread asks
// first, and the hidden window (hence the whole message queue) is rebound to
// THAT thread (juce_Messaging_windows.cpp InternalMessageQueue ctor). If that
// thread is not the pump, the pump's GetMessage loop can never see another
// JUCE message, so every juce::Timer - e.g. PluginProxySlot's 100 ms
// staged-param flush, the C2b transport-stopped param path - and every
// AsyncUpdater in the process stops firing FOR GOOD. A component that scopes a
// ScopedJuceInitialiser_GUI (several tests do) would otherwise be able to
// tear messaging down under the pump, while the pump's pin keeps the
// initialisation count above zero so no teardown can happen at all. The pin is
// intentionally never destroyed: see the comment at its construction.
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

    // JUCE GUI-initialisation pin (see the OWNERSHIP PIN note above).
    // Constructed ON the pump thread, immediately after the MessageManager
    // exists, and DELIBERATELY NEVER DELETED - the JUCE initialisation
    // reference count therefore stays >= 1 for the whole process, so no
    // component's ScopedJuceInitialiser_GUI scope can tear messaging down under
    // the pump (and no late shutdownJuce_GUI() can corrupt static destruction).
    juce::ScopedJuceInitialiser_GUI* juceInitPin = nullptr;
};

} // namespace HDAW
