// Test fixture: an infinite-loop child the engine's scanner-timeout path must
// kill. Plain C++ — no JUCE, no Qt.
#include <thread>
#include <chrono>
int main()
{
    for (;;)
        std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}