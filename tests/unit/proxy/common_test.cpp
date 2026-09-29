#include <gtest/gtest.h>
#include "proxy/ProxyCommon.h"

using namespace proxy;

TEST(RingBuffer, SizeSanity) {
    // The control pipe framing is a FIXED 256-byte message on both processes.
    // Pinning it (rather than >=) is what catches a payload drift that would
    // change the message size on one side only.
    EXPECT_EQ(sizeof(ProxyMessage), 256u);
    EXPECT_EQ(sizeof(ProxyResponse), 256u);
    EXPECT_EQ(sizeof(ProxyMessage::data), kMaxPayload);
    EXPECT_EQ(sizeof(ProxyResponse::data), kMaxPayload);
}

TEST(ProxyProtocol, VersionGateRejectsStaleChild) {
    // The READY handshake version guard: a child built against the
    // pre-correlation-id framing answers READY with result=1 (the implicit v1)
    // and must be refused — driving it with v2 framing would mis-parse
    // requestId as slotId.
    EXPECT_TRUE(protocolVersionAccepted(kProtocolVersion));
    EXPECT_FALSE(protocolVersionAccepted(1u))
        << "a stale child (implicit v1 READY result) must be rejected";
    EXPECT_FALSE(protocolVersionAccepted(0u));
    EXPECT_FALSE(protocolVersionAccepted(kProtocolVersion + 1));
}

TEST(RingBuffer, ComputeShmSize) {
    uint32_t size2ch = computeShmSize(2, 512);
    uint32_t size1ch = computeShmSize(1, 256);
    EXPECT_GT(size2ch, sizeof(ShmHeader));
    EXPECT_GT(size1ch, sizeof(ShmHeader));
    EXPECT_GT(size2ch, size1ch);
}
