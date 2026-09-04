#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace HDAW {

// A decoded Access Virus patch mapped onto the HDAW `sub_synth` internal FX
// (params 0..23, TrackFXSlot::getParamDefsForType("sub_synth")). `mapped`
// holds REAL-UNIT values (cutoff in Hz, env times in seconds, waves/filter
// type as 0..3 ints, ...) matching the param def ranges; only indices present
// in `mapped` are written by the loader. `unmapped` lists Virus features the
// sub_synth has no equivalent for (FM, ring mod, LFOs, keytrack, ...) — never
// silently dropped.
struct VirusPatch {
    std::string name;                  // trimmed ASCII (may be empty; may carry a leading '~')
    bool isValid = false;
    int bank = 0, program = 0;         // from the sysex header when available
    std::array<std::optional<float>, 24> mapped;
    std::vector<std::string> unmapped;
};

// Parse a 267-byte B/C single program dump:
//   F0 00 20 33 01 <dev> 10 <bank> <prog> | 256-byte page A+B | cs | F7
// Checksum = (dev + 0x10 + bank + prog + sum(page)) & 0x7F at byte 265.
// Returns nullopt on: wrong size, missing manufacturer header, not a single
// dump (cmd != 0x10), missing F7, or bad checksum. On success the returned
// patch carries name/bank/program plus the mapped + unmapped lists.
std::optional<VirusPatch> parseBcSingle(const uint8_t* data, size_t size);

// Parse a TI bank: any positive multiple of 524-byte self-contained sysex
// blocks (a full bank is 128 x 524 = 67072 bytes). Each block:
//   F0 00 20 33 01 <dev> 10 <bank> <prog> | 512-byte payload | cs | F7
// checksum at 522 = (dev + 0x10 + bank + prog + sum(payload)) & 0x7F.
// Returns a vector of patches (one per block), or empty on any structural or
// checksum failure (a corrupt bank is rejected wholesale — the loader must
// never partially apply).
std::vector<VirusPatch> parseTiBank(const uint8_t* data, size_t size);

// Map a page A+B payload (256 bytes for a B/C single, the first 256 bytes of
// a TI block payload) onto the sub_synth params 0..23 in real units, plus the
// fixed unmapped-feature list. `page` offsets mirror virus_patch.py's
// PARAM_OFFSETS (page A unless noted); `name` is stored verbatim. Offsets are
// bounds-checked against `len` (never read out of range).
VirusPatch mapToSubSynth(const uint8_t* page, size_t len, const std::string& name);

} // namespace HDAW