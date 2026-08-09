#include <gtest/gtest.h>
#include <QCoreApplication>

#include "common/MessagePumpThread.h"

int main(int argc, char** argv) {
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
