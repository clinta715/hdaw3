#include <gtest/gtest.h>
#include <QCoreApplication>

#include "common/MessagePumpThread.h"
#include "common/ScopedComInit.h"

int main(int argc, char** argv) {
    // COM init is host-app responsibility for JUCE 8's WASAPI (see
    // common/ScopedComInit.h). Tests construct AudioEngine / touch
    // AudioDeviceManager; without COM the WASAPI scan returns empty.
    static HDAW::ScopedComInit comInit;
    (void) comInit;

    // MUST precede any other JUCE construction: the pump thread wins
    // MessageManager messageThreadId + the hidden message window on first
    // getInstance() (see MessagePumpThread.h), so AudioProcessorGraph render
    // sequences and AsyncUpdaters can bake during tests (headers, exports,
    // live render) without a GUI message loop. Without this, export graphs
    // processBlock falls into the audio.clear() early-out (silence).
    HDAW::MessagePumpThread::start();

    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
