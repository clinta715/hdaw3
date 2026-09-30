#pragma once
// Watchdog / crash minidump policy (P2-b, 2026-09-30).
//
// Measured 2026-09-30 (ion_rift session): the hang watchdog wrote
// MiniDumpWithFullMemory dumps of **1.5-2 GB each, twice in one session**.
// The watchdog's question is "WHERE is processBlock stuck" — a thread-stack dump
// answers that in kilobytes; the rest of the address space is mostly the
// emulated device's firmware image and adds nothing but disk churn (and a long
// write on a machine already under render load). So the two dump paths differ:
//
//   hang  -> MiniDumpNormal            (thread stacks + module list; small)
//   crash -> MiniDumpWithFullMemory    (an SEH fault needs the faulting context
//                                       and the memory it dereferenced — the
//                                       pre-existing behaviour, kept exactly)
//
// This does NOT change WHEN a dump is written (that is PluginHost's watchdog
// threshold logic); it only changes how big the artifact is. Detection is
// untouched, so a real hang is still captured — with the stack that diagnoses it.
//
// Pure constants + a pure selector, so the policy is unit-testable without
// triggering a hang. Header-only: no CMake registration.

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
