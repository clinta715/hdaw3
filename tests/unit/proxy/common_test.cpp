#include <gtest/gtest.h>
#include "proxy/ProxyCommon.h"
#include "proxy/DumpPolicy.h"   // P2-b: hang dumps stay stack-only

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

// P2-b (2026-09-30): the hang watchdog used to write MiniDumpWithFullMemory —
// 1.5-2 GB per dump, twice in one session (the emulated device's firmware image
// dominates the child's address space). The watchdog's question is WHERE
// processBlock is stuck, which thread stacks answer; the crash (SEH) path keeps
// the full-memory dump it always had. This pins the policy, which is the whole
// fix — the WHEN (thresholds) is untouched, so a real hang is still captured.
TEST(DumpPolicy, HangDumpIsStackOnlyWhileTheCrashDumpKeepsFullMemory) {
#if defined(_WIN32)
    EXPECT_EQ(HDAW::minidumpTypeFor(HDAW::DumpKind::Hang), MiniDumpNormal);
    EXPECT_FALSE(HDAW::dumpCapturesFullMemory(HDAW::minidumpTypeFor(HDAW::DumpKind::Hang)))
        << "the hang dump must not carry the whole address space";
    EXPECT_EQ(HDAW::minidumpTypeFor(HDAW::DumpKind::Crash), MiniDumpWithFullMemory);
    EXPECT_TRUE(HDAW::dumpCapturesFullMemory(HDAW::minidumpTypeFor(HDAW::DumpKind::Crash)))
        << "the SEH crash path keeps its pre-existing diagnostic";

    // No indirect-memory walk either: the hang dump must not grow with the
    // plugin's heap (that is the 2 GB class, one flag away).
    const uint32_t hang = static_cast<uint32_t>(HDAW::kHangDumpType);
    EXPECT_EQ(hang & static_cast<uint32_t>(MiniDumpWithIndirectlyReferencedMemory), 0u);
    EXPECT_EQ(hang & static_cast<uint32_t>(MiniDumpWithPrivateReadWriteMemory), 0u);
#else
    // Linux: artifacts are kilobyte-scale text reports (signal + registers +
    // backtrace) written by async-signal-safe handlers. The full-address-space
    // artifact class does not exist on this platform, for EITHER kind — the
    // invariant that matters (no 2 GB dumps on the hang path) holds by
    // construction. The report surface must exist for both kinds.
    EXPECT_FALSE(HDAW::dumpCapturesFullMemory(HDAW::DumpKind::Hang));
    EXPECT_FALSE(HDAW::dumpCapturesFullMemory(HDAW::DumpKind::Crash));
#endif
}
