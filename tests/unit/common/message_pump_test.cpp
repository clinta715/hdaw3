// Unit tests for the process-wide JUCE message pump (MessagePumpThread).
// Gate E1: start() is idempotent and the queue actually dispatches.
// Gate E2: AsyncUpdater callbacks from non-message threads fire while
//          running, with no message injection from test code.
#include <gtest/gtest.h>
#include <juce_events/juce_events.h>

#include "common/MessagePumpThread.h"

namespace {

class AsyncFiredCounter : public juce::AsyncUpdater
{
public:
    using juce::AsyncUpdater::AsyncUpdater;

    std::atomic<int> fired { 0 };

private:
    void handleAsyncUpdate() override { ++fired; }
};

} // namespace

TEST(MessagePumpThread, StartIsIdempotent)
{
    // main() (test_main.cpp) may already have started the pump; whichever
    // thread wins, the contract is: at most one live pump, ownership held,
    // and repeated start() calls are harmless no-ops (returning false).
    HDAW::MessagePumpThread::start();
    const bool second = HDAW::MessagePumpThread::start();
    EXPECT_FALSE(second) << "second start() must be a no-op";
    EXPECT_TRUE(HDAW::MessagePumpThread::isOwned())
        << "pump thread must own the JUCE message loop";
}

TEST(MessagePumpThread, AsyncUpdaterFiresWithoutExplicitPumping)
{
    AsyncFiredCounter counter;

    counter.triggerAsyncUpdate();

    bool fired = false;
    for (int i = 0; i < 100 && !fired; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        fired = counter.fired.load() > 0;
    }

    EXPECT_TRUE(fired) << "message pump did not deliver AsyncUpdater callback";
}

// A component that constructs and destroys a juce::ScopedJuceInitialiser_GUI
// must NOT be able to tear the pump's JUCE messaging down.
//
// REGRESSION: ScopedJuceInitialiser_GUI calls shutdownJuce_GUI() when its LAST
// instance dies, and that deletes the MessageManager AND stops JUCE's
// TimerThread (ShutdownDetector -> applicationShuttingDown). The next
// MessageManager::getInstance() then re-creates it on whichever thread asks
// first — in a test process that is the TEST thread — which rebinds JUCE's
// hidden message window (and therefore its whole queue) to that thread. The
// pump's GetMessage loop can then never see another JUCE message, so every
// juce::Timer in the process (e.g. PluginProxySlot's 100 ms staged-param
// flush) stops firing PERMANENTLY. MessagePumpThread pins the initialisation
// for its own lifetime; this test is the pin's tripwire.
TEST(MessagePumpThread, JuceInitialiserScopeDoesNotTearDownThePumpQueue)
{
    HDAW::MessagePumpThread::start();
    auto* messageManagerBefore = juce::MessageManager::getInstance();
    ASSERT_NE(messageManagerBefore, nullptr);

    // A timer on the TEST thread proves the pump can dispatch: it only fires
    // if the pump thread owns the JUCE message queue.
    class TickCounter : public juce::Timer
    {
    public:
        std::atomic<int> ticks { 0 };
        void timerCallback() override { ++ticks; }
    } counter;
    counter.startTimer(50);

    {
        juce::ScopedJuceInitialiser_GUI scopedInit;
        juce::ignoreUnused(scopedInit);
    }

    EXPECT_EQ(juce::MessageManager::getInstanceWithoutCreating(), messageManagerBefore)
        << "a ScopedJuceInitialiser_GUI scope destroyed the pump's MessageManager "
           "(shutdownJuce_GUI ran: the pump no longer owns the JUCE queue)";

    bool fired = false;
    for (int i = 0; i < 200 && !fired; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        fired = counter.ticks.load() > 0;
    }
    counter.stopTimer();

    EXPECT_TRUE(fired) << "the pump stopped dispatching after a "
                          "ScopedJuceInitialiser_GUI scope closed";
}