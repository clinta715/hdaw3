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