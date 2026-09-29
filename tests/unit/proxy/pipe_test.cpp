#include <gtest/gtest.h>
#include "proxy/ProxyPipe.h"
#include <thread>

using namespace proxy;

TEST(Pipe, ServerClientRoundTrip) {
    PipeServer server("\\\\.\\pipe\\hdaw_test_pipe_1");
    ASSERT_TRUE(server.start());

    std::thread clientThread([]() {
        PipeClient client("\\\\.\\pipe\\hdaw_test_pipe_1");
        ASSERT_TRUE(client.connect());

        ProxyMessage msg{};
        msg.type = MessageType::READY;
        msg.requestId = 11;
        msg.slotId = 42;
        ASSERT_TRUE(client.send(msg));

        ProxyResponse resp{};
        ASSERT_TRUE(client.receive(resp));
        EXPECT_EQ(resp.type, MessageType::READY);
        EXPECT_EQ(resp.result, 1u);
    });

    ProxyMessage received{};
    ASSERT_TRUE(server.receive(received));
    EXPECT_EQ(received.type, MessageType::READY);
    EXPECT_EQ(received.slotId, 42u);
    EXPECT_EQ(received.requestId, 11u);

    ProxyResponse resp{};
    resp.type = MessageType::READY;
    resp.requestId = 11;
    resp.result = 1;
    ASSERT_TRUE(server.send(resp));

    clientThread.join();
    server.stop();
}

TEST(Pipe, SendReceiveLargePayload) {
    PipeServer server("\\\\.\\pipe\\hdaw_test_pipe_2");
    ASSERT_TRUE(server.start());

    std::thread clientThread([]() {
        PipeClient client("\\\\.\\pipe\\hdaw_test_pipe_2");
        ASSERT_TRUE(client.connect());

        ProxyMessage msg{};
        msg.type = MessageType::SET_STATE;
        msg.dataSize = 1024;
        // kMaxPayload (240) is the fixed payload capacity — a 244 here would
        // overrun the message into adjacent memory.
        for (uint32_t i = 0; i < kMaxPayload; ++i)
            msg.data[i] = static_cast<uint8_t>(i & 0xFF);
        ASSERT_TRUE(client.send(msg));
    });

    ProxyMessage received{};
    ASSERT_TRUE(server.receive(received));
    EXPECT_EQ(received.type, MessageType::SET_STATE);

    clientThread.join();
    server.stop();
}
