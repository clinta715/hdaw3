#pragma once
#include <cstdint>
#include <atomic>
#include <string>
#include <cstdlib>

// ---------------------------------------------------------------------------
// PLATFORM SHIM (Linux branch). The proxy public API is DWORD/HANDLE-stable by
// design (mechanical API stability for PluginProxySlot.cpp etc.), so on Linux
// DWORD and HANDLE keep their names: HANDLE is an fd-compatible int alias.
// Windows includes <windows.h>; Linux gets the minimal equivalents here so the
// per-file `#if defined(_WIN32) #include <windows.h>` branches elsewhere stay
// self-contained.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#include <windows.h>
#else
namespace proxy {
using DWORD = uint32_t;
// On Linux a "handle" is a file descriptor (pipe socket fd, shm fd) or a pid.
using HANDLE = int;
} // namespace proxy
// Matches every `x == INVALID_HANDLE_VALUE` pattern in the Win32 branch.
#define INVALID_HANDLE_VALUE (-1)
#endif

namespace proxy {

#if defined(_WIN32)
inline constexpr uint32_t GRACEFUL_EXIT_CODE = 0xC0DE0001;
#else
// Linux waitpid reports an 8-bit WEXITSTATUS, so a graceful shutdown cannot
// carry the Windows 32-bit code. The child _Exit()s with this constant and the
// parent maps WIFEXITED(status) && WEXITSTATUS(status) == GRACEFUL_EXIT_CODE
// to a graceful stop; WIFSIGNALED is a crash. BOTH sides include this header,
// so the two processes cannot drift.
inline constexpr uint32_t GRACEFUL_EXIT_CODE = 0xC1;
#endif

#if !defined(_WIN32)
// Directory for the per-instance AF_UNIX socket paths and shm objects.
// $XDG_RUNTIME_DIR when set (user-owned, tmpfs), else /tmp.
inline std::string proxyRuntimeDir() {
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && xdg[0] != '\0')
        return xdg;
    return "/tmp";
}

// Map a Win32-style pipe name (`\\.\pipe\NAME`, or the bare logical NAME the
// manager generates) onto an abstract-namespace-free socket path. Callers
// already guarantee uniqueness per slot (pid-hex + instance counter prefix).
inline std::string socketPathForPipeName(std::string name) {
    const std::string prefix = "\\\\.\\pipe\\";
    if (name.rfind(prefix, 0) == 0) name.erase(0, prefix.size());
    std::string sanitized;
    sanitized.reserve(name.size());
    for (char c : name)
        sanitized.push_back((c == '/' || c == '\\') ? '_' : c);
    return proxyRuntimeDir() + "/hdaw-" + sanitized + ".sock";
}

// shm_open requires a leading '/' and no further slashes.
inline std::string shmObjectForName(std::string name) {
    std::string sanitized;
    sanitized.reserve(name.size());
    for (char c : name)
        sanitized.push_back((c == '/' || c == '\\') ? '_' : c);
    return "/" + sanitized;
}
#endif

constexpr uint32_t SHM_MAGIC = 0x4844415D; // bumped 2026-09-20 for the stateSet ring (parent->child plugin state over shm; was 0x4844415C for the output-resync handshake)

constexpr uint32_t PARAM_RING_SIZE = 256;

// Parent->child plugin-state byte ring. State travels as a length-prefixed
// record ([uint32 size][bytes]) through shared memory instead of the control
// pipe: a pipe SET_STATE blocks while the child's control thread is not reading
// (the 12 s Virus-family OS warmup runs there, and a CPU-bound offline render
// starves it), so the parent's bounded send timed out and the restored state
// never reached the plugin (finding F-A). 1 MiB covers the largest states
// measured (OsTIrus ~177 KB arrangement + ~76 KB params).
constexpr uint32_t STATE_RING_SIZE = 1u << 20;

constexpr uint32_t SYSEX_BUFFER_SIZE = 128 * 1024;

enum class MessageType : uint32_t {
    READY = 0,
    PREPARE,
    PREPARE_RESULT,
    SHUTDOWN,

    PROCESS_BLOCK,

    SET_STATE,
    GET_STATE,
    GET_STATE_RESULT,

    SET_PARAM,
    GET_PARAM,
    GET_PARAM_RESULT,
    GET_PARAM_COUNT,
    GET_PARAM_COUNT_RESULT,
    GET_PARAM_INFO,
    GET_PARAM_INFO_RESULT,

    SHOW_EDITOR,
    CLOSE_EDITOR,
    EDITOR_CLOSED,
    PARAM_CHANGED,

    HEARTBEAT,
    STATE_CHUNK,

    GET_PROGRAM_COUNT,
    GET_PROGRAM_COUNT_RESULT,
    GET_PROGRAM_NAME,
    GET_PROGRAM_NAME_RESULT,
    SET_PROGRAM,
    SET_PROGRAM_RESULT,
    GET_CURRENT_PROGRAM,
    GET_CURRENT_PROGRAM_RESULT,
};

// Control-pipe framing version, reported by the child in its READY response
// (ProxyResponse::result) and checked by the parent at spawn (Gate 4/15: a
// stale child exe must be DETECTED, not mis-parsed). v2 is the
// correlation-id framing below; v1 is the pre-correlation-id framing that had
// no requestId field.
//
// SCOPE — what this gate can and cannot catch. It is a version check on a
// reply the v2 parent already understood, so it catches a child built against
// THIS framing that advertises a DIFFERENT version (e.g. a future v3 child
// driven by an older v2 engine). It can NOT catch a genuine v1 binary: v1's
// READY is a different layout, so the v2 parent reads garbage out of it — see
// isLegacyV1ReadyReply() below, which is what detects that case.
//
// Keep in sync with ProxyCommon.h on BOTH processes: this is a two-process
// protocol change, so HDAW_headless.exe AND hdaw_plugin_host.exe must be
// rebuilt together.
inline constexpr uint32_t kProtocolVersion = 2;

// Payload capacity of one fixed 256-byte control message. The struct layout is
// size-fixed (a 256-byte message on a message-mode pipe); ALL payload math
// derives from this constant / sizeof(member.data), never a literal.
inline constexpr uint32_t kMaxPayload = 240;

// requestId sentinel meaning "no request / unsolicited": the child's
// EDITOR_CLOSED notification is emitted with no request behind it, and the
// parent's READY await sends nothing. A correlation id is otherwise always
// >= 1 (see PipeServer's monotonic allocator).
inline constexpr uint32_t kUnsolicitedRequestId = 0;

// Correlation id. Every request carries a non-zero id allocated by
// PipeServer; the child echoes it into EVERY response (single replies AND each
// multi-chunk continuation of that request), so the parent can tell a stale
// reply (an id it is no longer waiting for) from a fresh one. This supersedes
// the old expected-type-only heuristic, which could not distinguish a late
// reply of the SAME type.
struct alignas(256) ProxyMessage {
    MessageType type;
    uint32_t requestId;
    uint32_t slotId;
    uint32_t dataSize;
    uint8_t  data[kMaxPayload];
};

struct alignas(256) ProxyResponse {
    MessageType type;
    uint32_t requestId;
    uint32_t result;
    uint32_t dataSize;
    uint8_t  data[kMaxPayload];
};

// The framing is a fixed 256-byte message (4 x uint32 header + 240 payload).
// A drift here silently changes the pipe's message size on one side only.
static_assert(sizeof(ProxyMessage) == 256, "ProxyMessage must stay 256 bytes");
static_assert(sizeof(ProxyResponse) == 256, "ProxyResponse must stay 256 bytes");

// READY handshake version gate for a child that SPEAKS the current framing but
// reports a different version. Used by ProxyProcessManager::spawnPluginHost.
inline bool protocolVersionAccepted(uint32_t childReportedVersion) {
    return childReportedVersion == kProtocolVersion;
}

// ---------------------------------------------------------------------------
// LEGACY (v1) LAYOUT DETECTION at the READY handshake.
//
// v1's ProxyResponse was {type, result, dataSize, data[244]} — no requestId.
// A v1 child answers READY with result=1, dataSize=0, so the v2 parent decodes
// that same 256-byte frame as:
//
//   v1 bytes:  [type=READY][result=1][dataSize=0][data=0...]
//   v2 decode:  type=READY   requestId=1  result=0    dataSize=0
//
// i.e. a non-zero requestId that no requestless handshake owns, which
// correlation-id matching would DISCARD — leaving the spawn to fail by TIMEOUT
// with no hint of the real cause. isLegacyV1ReadyReply() names that exact
// signature so the handshake can report the mismatch immediately.
//
// A v2 child can never match: its READY is UNSOLICITED (requestId ==
// kUnsolicitedRequestId) and reports kProtocolVersion in `result`.
inline bool isLegacyV1ReadyReply(const ProxyResponse& r) {
    return r.type == MessageType::READY
        && r.requestId == 1u      // v1 `result` (the v1 child always sent 1)
        && r.result == 0u         // v1 `dataSize`
        && r.dataSize == 0u;      // v1 `data[0..3]` (the v1 struct is zero-init)
}

// The parent-side diagnosis for a detected v1 child. Verbatim: the operator
// needs the exact stale-binary check ("did I rebuild BOTH processes?").
inline constexpr const char* kLegacyV1Diagnosis =
    "PROTOCOL VERSION MISMATCH: stale v1 hdaw_plugin_host.exe (rebuild with dsh-build-fast.bat all)";

struct ShmHeader {
    uint32_t magic;
    uint32_t numChannels;
    uint32_t blockSize;
    uint32_t sampleRate;
    uint32_t capacity;

    std::atomic<uint32_t> inputWritePos{0};
    std::atomic<uint32_t> inputReadPos{0};

    std::atomic<uint32_t> outputWritePos{0};
    std::atomic<uint32_t> outputReadPos{0};

    std::atomic<uint32_t> midiInWritePos{0};
    std::atomic<uint32_t> midiInReadPos{0};

    std::atomic<uint32_t> midiOutWritePos{0};
    std::atomic<uint32_t> midiOutReadPos{0};

    std::atomic<uint32_t> childAlive{0};
    std::atomic<uint32_t> dawAlive{0};

    // Child-side watchdog: incremented once per processed audio block.
    // Parent compares against a saved snapshot; a stall for >staleThresholdMs
    // is treated as a hang only when input is pending (inputWritePos !=
    // inputReadPos) — an idle child with no input is healthy, not hung.
    std::atomic<uint64_t> audioFramesProduced{0};
    std::atomic<uint64_t> audioBlocksProcessed{0};

    // One in-flight SysEx per direction. Writer sets after publishing the
    // event, reader clears after copying the bytes out.
    std::atomic<uint32_t> sysexInBusy{0};
    std::atomic<uint32_t> sysexOutBusy{0};

    // Parent->child param set ring (parent audio thread = single writer).
    // Child->parent param notify ring (child AudioProcessorListener = single
    // writer). Both ring bodies are arrays of std::atomic<uint64_t> laid out
    // after the SysEx buffers in the shm region; only the position atomics
    // live here. Each entry is packed (uint32_t(paramIndex) << 32) | bits-of-float.
    std::atomic<uint32_t> paramSetWritePos{0};
    std::atomic<uint32_t> paramSetReadPos{0};
    std::atomic<uint32_t> paramNotifyWritePos{0};
    std::atomic<uint32_t> paramNotifyReadPos{0};

    // Parent->child plugin-state ring positions (parent = single writer, child =
    // single reader). Monotonic byte counters; the ring body is a
    // STATE_RING_SIZE byte array laid out after the param rings.
    std::atomic<uint32_t> stateSetWritePos{0};
    std::atomic<uint32_t> stateSetReadPos{0};

    // ── Transport clock snapshot (playhead forward). ────────────────────────
    // The parent (PluginProxySlot::processBlock, live audio thread AND export)
    // reads its AudioPlayHead each block and packs the transport state below;
    // the child (hdaw_plugin_host) snapshots it into its own AudioPlayHead.
    // transportRevision is the "new data" signal: the parent release-stores an
    // incremented revision AFTER writing every field, the child acquire-loads
    // it and only copies the fields when the value changed. An unchanged
    // revision means "no new info" — the child keeps its last snapshot (which
    // starts out as a stopped-transport default). Wraps naturally at 2^32;
    // unused wrap is fine over a session. Parent is the single writer, child
    // the single reader — no locks, plain bit-pattern values (no pointers).
    std::atomic<uint32_t> transportRevision{0};
    uint32_t transportPlaying;     // 1 = transport running, 0 = stopped
    uint32_t transportTempoBits;   // IEEE 754 single-precision bits (BPM)
    uint64_t transportSecondsBits; // IEEE 754 double bits (time in seconds)
    uint64_t transportPpqBits;     // IEEE 754 double bits (PPQ position)
    uint32_t transportTsigNum;     // time-signature numerator (default 4)
    uint32_t transportTsigDenom;   // time-signature denominator (default 4)

    // The hosted plugin's summed channel layout, written ONCE by the child
    // right after load (0 = not yet known). The parent proxy uses these to
    // size PREPARE and its reported bus width, so multi-port plugins (e.g.
    // the 4-out Nord-2x port) get their full channel count in the child.
    uint32_t pluginNumInputChannels;
    uint32_t pluginNumOutputChannels;

    // Output-alignment handshake. The child release-stores lastConsumedInputPos
    // AFTER processing a block AND writing its output; the parent acquire-loads
    // it and only reads output that is current for the input it just wrote —
    // stale (misaligned) output is drained and silence delivered instead.
    std::atomic<uint64_t> lastConsumedInputPos{0};

    // Parent writes 1 while the slot belongs to an export render graph (see
    // PluginProxySlot::prepareToPlay); the child reads it per loop iteration
    // to decide whether to Sleep-pace its audio loop (render mode only — live
    // playback is paced by the device cadence).
    std::atomic<uint32_t> renderMode{0};
};

struct MidiEvent {
    uint32_t sampleOffset;
    uint8_t  data[3];
    // Bit 7 set: SysEx reference (sysexLen valid, bytes live in the
    // direction's SysEx buffer). Else low bits = inline byte count (1-3).
    uint8_t  flags;
    uint32_t sysexLen;
};

// True when the child has consumed input up to (or past) the position the
// parent wrote this call — only then is the output in the ring current for
// that input. A lagging child (Sleep-granularity pacing, hung processBlock)
// leaves stale output in the ring; the parent must never deliver it.
inline bool proxyOutputIsCurrent(uint64_t lastConsumedInputPos, uint64_t inputPosWrittenThisCall) {
    return lastConsumedInputPos >= inputPosWrittenThisCall;
}

inline uint32_t computeShmSize(uint32_t numChannels, uint32_t blockSize) {
    uint32_t cap = 1;
    while (cap < blockSize * numChannels) cap <<= 1;

    uint32_t headerSize = static_cast<uint32_t>(sizeof(ShmHeader));
    uint32_t inputRing  = cap * sizeof(float);
    uint32_t outputRing = cap * sizeof(float);
    uint32_t midiInRing  = 256 * sizeof(MidiEvent);
    uint32_t midiOutRing = 256 * sizeof(MidiEvent);

    return headerSize + inputRing + outputRing + midiInRing + midiOutRing
         + 2 * SYSEX_BUFFER_SIZE
         + 2 * PARAM_RING_SIZE * sizeof(std::atomic<uint64_t>)
         + STATE_RING_SIZE;
}

// The shared-memory mapping is created ONCE at spawn for the worst-case
// audio config (multi-channel plugins like the 4-out Nord-2x port × the
// largest device block size), so both parent and child can safely let
// hdr->capacity float up to this at PREPARE. Indexing past the mapping
// would be a cross-process OOB write.
constexpr uint32_t kMaxShmChannels = 8;
constexpr uint32_t kMaxShmBlockSize = 4096;
constexpr uint32_t kMaxShmCapacitySamples = 32768; // pow2 >= 8 * 4096

} // namespace proxy
