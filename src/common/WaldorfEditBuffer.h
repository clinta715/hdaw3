#pragma once
// ONE implementation of the Waldorf microQ/Vavra edit-buffer retarget.
//
// Why this exists (2026-09-30): the retarget added 2026-09-18 for the documented
// "NOT APPLYING" failure lived ONLY in the file-loader path
// (src/common/PresetApply.h, waldorfSysexFileToolText). Real microQ bank dumps
// carry their ORIGINAL buffer/location bytes — 0x30 = multi-edit, 0x40+ = a
// bank slot — and the single-mode OS does not play a dump aimed at those
// buffers, so an injection into them is a SILENT no-op. The
// `apply_matrix_preset` device-native dump route pushed the sheet's `sysex`
// bytes verbatim, so every vavra matrix preset / morph step (measured: byte5 =
// 0x30 on all 40 vavra.json presets and all 20 vavra_morphs.json steps) did
// nothing.
//
// House rule (AGENTS.md "identical by construction, not by discipline"): the
// transform is THIS function, called by both routes — never re-derived at a
// call site. Header-only (juce_core only) so no CMake registration is needed,
// mirroring src/common/TrackJson.h.
//
// The transform mirrors the microQ editor's own send path
// (mqController::sendSingle): retarget to the single-mode edit buffer and
// recompute the Waldorf checksum (sum of bytes [4 .. size-2) & 0x7F).
// microq_patch.py documents that the emulator only checks the size, but real
// hardware validates the checksum, so a correct file is written either way.
//
// Guard (unchanged from the original implementation, and deliberately narrow):
//   machine == microQ  &&  size == 392
// Everything else — a non-microQ dump (Xenia/Microwave XT), or a microQ dump
// that is not 392 bytes — passes through BYTE-IDENTICAL.

#include "../mcp/PresetFileParser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace HDAW {

/// Waldorf machine byte for a plugin id ('Vavra.clap') OR a matrix-sheet engine
/// id ('vavra'). One implementation of the identity -> machine rule, shared by
/// both call sites so they cannot disagree about which engine is the microQ.
inline uint8_t waldorfMachineForName(const std::string& name)
{
    return juce::String(name).containsIgnoreCase(juce::String("vavra"))
               ? mcp::kWaldorfMachineMicroQ
               : mcp::kWaldorfMachineMw2;
}

/// Retarget ONE dump to the microQ single-mode edit buffer. Returns true iff the
/// dump was rewritten (false => the bytes are untouched).
inline bool retargetWaldorfDumpForSingleEditBuffer(uint8_t* d, std::size_t size,
                                                   uint8_t machine)
{
    if (d == nullptr)
        return false;
    if (machine != mcp::kWaldorfMachineMicroQ)
        return false;
    if (size != 392)
        return false;

    d[5] = 0x20;   // MidiBufferNum::SingleEditBufferSingleMode
    d[6] = 0x00;   // MidiSoundLocation::EditBufferCurrentSingle
    uint8_t cs = 0;
    for (std::size_t i = 4; i + 2 < size; ++i)
        cs += d[i];
    d[size - 2] = cs & 0x7f;
    return true;
}

/// std::vector spelling (the shape both call sites carry).
inline bool retargetWaldorfDumpForSingleEditBuffer(std::vector<uint8_t>& dump,
                                                   uint8_t machine)
{
    return retargetWaldorfDumpForSingleEditBuffer(dump.data(), dump.size(), machine);
}

/// Batch spelling — the file-loader path carries N split dumps from one file.
/// Returns true iff ANY dump was rewritten.
inline bool retargetWaldorfDumpsForSingleEditBuffer(
    std::vector<std::vector<uint8_t>>& dumps, uint8_t machine)
{
    bool changed = false;
    for (auto& d : dumps)
    {
        const bool one = retargetWaldorfDumpForSingleEditBuffer(d, machine);
        changed = changed || one;
    }
    return changed;
}

} // namespace HDAW
