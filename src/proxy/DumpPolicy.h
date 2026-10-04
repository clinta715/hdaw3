#pragma once
// Watchdog / crash dump policy (P2-b, 2026-09-30).
//
// Measured 2026-09-30 (ion_rift session): the hang watchdog wrote
// MiniDumpWithFullMemory dumps of **1.5-2 GB each, twice in one session**.
// The watchdog's question is "WHERE is processBlock stuck" — a thread-stack dump
// answers that in kilobytes; the rest of the address space is mostly the
// emulated device's firmware image and adds nothing but disk churn (and a long
// write on a machine already under render load). So the two dump paths differ:
//
//   hang  -> MiniDumpNormal            (thread stacks + module list; small)
//   crash -> MiniDumpWithFullMemory    (a fault needs the faulting context
//                                       and the memory it dereferenced — the
//                                       pre-existing behaviour, kept exactly)
//
// This does NOT change WHEN a dump is written (that is PluginHost's watchdog
// threshold logic); it only changes how big the artifact is. Detection is
// untouched, so a real hang is still captured — with the stack that diagnoses it.
//
// Linux branch: there is no dbghelp, so a crash is captured as a TEXT report
// (signal number, fault address, saved registers from ucontext, and
// backtrace() frames written with the async-signal-safe
// backtrace_symbols_fd form) to the same capture-directory convention, then
// the signal is re-raised for the default disposition / core. Only
// async-signal-safe calls are used inside the handler.

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <dbghelp.h>   // MINIDUMP_TYPE / MiniDump* flags

#include <cstdint>

namespace HDAW {

enum class DumpKind
{
    Hang,    // watchdog: processBlock held the audio loop past its threshold
    Crash    // SEH filter: a fault, with EXCEPTION_POINTERS available
};

// The watchdog dump must stay SMALL: no full memory, no indirectly referenced
// memory walk. MiniDumpNormal is exactly "thread stacks + modules".
inline constexpr MINIDUMP_TYPE kHangDumpType = MiniDumpNormal;

// The crash dump keeps the pre-P2-b behaviour byte for byte.
inline constexpr MINIDUMP_TYPE kCrashDumpType = MiniDumpWithFullMemory;

inline constexpr MINIDUMP_TYPE minidumpTypeFor(DumpKind kind)
{
    return kind == DumpKind::Crash ? kCrashDumpType : kHangDumpType;
}

// True when the given dump flags would capture the whole address space (i.e.
// the 1.5-2 GB class). The unit test asserts the HANG type is false here and
// the CRASH type is true — that IS the P2-b contract.
inline constexpr bool dumpCapturesFullMemory(MINIDUMP_TYPE type)
{
    return (static_cast<uint32_t>(type) & static_cast<uint32_t>(MiniDumpWithFullMemory)) != 0u;
}

} // namespace HDAW

#else // !defined(_WIN32) — Linux: signal-handler crash reports

#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include "ProxyCommon.h"

#if defined(__GLIBC__)
#include <execinfo.h>
#define HDAW_HAVE_BACKTRACE 1
#endif

namespace HDAW {

enum class DumpKind
{
    Hang,    // watchdog: processBlock held the audio loop past its threshold
    Crash    // fatal signal in the child process
};

// True when an artifact captures the whole address space — on Linux nothing
// ever does (reports are kilobyte-scale text; the MinidumpWithFullMemory class
// of artifact does not exist here).
inline constexpr bool dumpCapturesFullMemory(DumpKind /*kind*/)
{
    return false;
}

namespace detail {

// The capture directory follows the same convention as the Windows branch's
// GetTempPath: the redirectable per-process temp location. TMP/TEMP are the
// Windows names (the engine's test harness and crash tooling redirect the
// child through them), TMPDIR/XDG_RUNTIME_DIR the Linux ones, /tmp the final
// fallback.
inline std::string captureDir() {
    for (const char* var : { "TMP", "TEMP", "TMPDIR", "XDG_RUNTIME_DIR" }) {
        if (const char* v = std::getenv(var); v && v[0] != '\0') return v;
    }
    return "/tmp";
}

// Stack-append helper: printf is NOT async-signal-safe, so the handler writes
// through raw write() into a stack buffer.
struct FdAppender
{
    int fd = -1;
    char buf[512];
    size_t len = 0;

    void flushRaw() {
        if (len) { ssize_t ignored = ::write(fd, buf, len); (void) ignored; len = 0; }
    }
    void append(const char* s, size_t n) {
        while (n > 0) {
            const size_t space = sizeof(buf) - len;
            const size_t take = n < space ? n : space;
            std::memcpy(buf + len, s, take);
            len += take; s += take; n -= take;
            if (len == sizeof(buf)) flushRaw();
        }
    }
    void appendStr(const char* s) { append(s, std::strlen(s)); }
    void appendU64(unsigned long long v) {
        char tmp[24];
        int i = (int) sizeof(tmp);
        if (v == 0) { append("0", 1); return; }
        while (v && i > 0) { tmp[--i] = char('0' + (v % 10)); v /= 10; }
        append(tmp + i, sizeof(tmp) - (size_t) i);
    }
    void appendHex(unsigned long long v) {
        static const char d[] = "0123456789abcdef";
        char tmp[20];
        int i = (int) sizeof(tmp);
        if (v == 0) { appendStr("0x0"); return; }
        while (v && i > 1) { tmp[--i] = d[v & 0xF]; v >>= 4; }
        append("0x", 2);
        append(tmp + i, sizeof(tmp) - (size_t) i);
    }
    ~FdAppender() { flushRaw(); }
};

inline void writeReport(int fd, const char* reason, int sig, ucontext_t* uc) {
    FdAppender a; a.fd = fd;
    a.appendStr("HDAW plugin_host crash report\nreason: ");
    a.appendStr(reason);
    a.appendStr("\nsignal: "); a.appendU64((unsigned long long) sig);
    a.appendStr("\ntime: ");
    struct timespec ts{}; clock_gettime(CLOCK_REALTIME, &ts); // async-signal-safe on Linux
    a.appendU64((unsigned long long) ts.tv_sec);
    a.appendStr("."); a.appendU64((unsigned long long) ts.tv_nsec);
    a.appendStr("\npid: "); a.appendU64((unsigned long long) getpid());
    a.appendStr("\n");

#if defined(__GLIBC__) && defined(__x86_64__)
    if (uc) {
        const greg_t* g = uc->uc_mcontext.gregs;
        static const char* names[23] = {
            "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15",
            "RDI", "RSI", "RBP", "RBX", "RDX", "RAX", "RCX", "RSP",
            "RIP", "EFL", "CSGSFS", "ERR", "TRAPNO", "OLDMASK", "CR2"
        };
        a.appendStr("registers:\n");
        for (int i = 0; i < 23; ++i) {
            a.appendStr("  "); a.appendStr(names[i]); a.appendStr("="); a.appendHex((unsigned long long) g[i]);
            a.appendStr("\n");
        }
    }
#else
    (void) uc;
#endif

#if defined(HDAW_HAVE_BACKTRACE)
    // execinfo backtrace(): fills the address array safely (stops at the
    // outermost valid frame — no fixed depth guessing) and the emission uses
    // backtrace_symbols_fd's async-signal-safe fd form.
    void* frames[64];
    const int n = ::backtrace(frames, (int) (sizeof(frames) / sizeof(frames[0])));
    a.appendStr("frames: "); a.appendU64((unsigned long long) (n > 0 ? n : 0)); a.appendStr("\n");
    if (n > 0) backtrace_symbols_fd(frames, n, fd); // async-signal-safe fd form
#endif
}

// Async-signal-safe handler body: path is precomputed at install time.
struct HandlerState {
    char path[256];
};
inline HandlerState g_handlerState{};

inline void fatalHandler(int sig, siginfo_t* info, void* ctx) {
    int fd = ::open(g_handlerState.path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) {
        writeReport(fd, "fatal signal", sig, static_cast<ucontext_t*>(ctx));
        ::close(fd);
    }
    // Re-raise for the default disposition / core. Restore SIGDFL first so the
    // re-raise is not caught by this handler again.
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    ::sigaction(sig, &sa, nullptr);
    ::raise(sig);
}

} // namespace detail

// Write a text crash/stack report to the capture directory
// ($XDG_RUNTIME_DIR or /tmp), named "hdaw_plugin_host_<reason>.crash.txt".
// NOT async-signal-safe: call it OFF a signal handler (the watchdog hang path
// and the post-crash logging path). Returns the artifact path in a static
// buffer (empty string on failure) — single-threaded call sites only.
inline const char* writeCrashReport(DumpKind kind, const char* reason) {
    static char pathBuf[300];
    std::string base = detail::captureDir() + "/hdaw_plugin_host_";
    std::string safe;
    for (const char* p = reason; p && *p; ++p)
        safe.push_back((*p == '/' || *p == ' ') ? '_' : *p);
    std::string path = base + safe + ".crash.txt";
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) { pathBuf[0] = '\0'; return pathBuf; }
    detail::writeReport(fd, reason, /*sig=*/0, nullptr);
    ::close(fd);
    std::snprintf(pathBuf, sizeof(pathBuf), "%s (%s)",
                  path.c_str(), kind == DumpKind::Crash ? "crash" : "hang");
    return pathBuf;
}

// Install SIGSEGV/SIGBUS/SIGFPE/SIGABRT handlers that write a text report
// (signal, ucontext registers, backtrace frames — async-signal-safe forms
// only) and then re-raise the signal for the default disposition / core.
// Returns true if every handler was installed.
inline bool installCrashSignalHandlers() {
    std::string dir = detail::captureDir();
    std::snprintf(detail::g_handlerState.path, sizeof(detail::g_handlerState.path),
                  "%s/hdaw_plugin_host_signal.crash.txt", dir.c_str());

    bool all = true;
    const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGABRT };
    for (int sig : sigs) {
        struct sigaction sa{};
        sa.sa_sigaction = detail::fatalHandler;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset(&sa.sa_mask);
        if (::sigaction(sig, &sa, nullptr) != 0)
            all = false;
    }
    return all;
}

} // namespace HDAW

#endif // _WIN32
