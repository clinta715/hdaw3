#pragma once
#include <cstdint>
#include <array>
#include <optional>
#include <vector>
#include <string>

namespace HDAW {

struct Dx7Voice {
    std::array<uint8_t, 156> patchData{};   // Engine VCED layout (OP6 at [0])
    std::string voiceName;                   // 10-char ASCII name
    int algorithm = 0;
    int feedback = 0;
};

// Parse a single-voice SysEx dump (F0 43 00 00 01 1B ... F7, 163 bytes total).
// Returns nullopt on: wrong header, bad checksum, wrong size.
std::optional<Dx7Voice> parseSingleVoiceSysex(const uint8_t* data, size_t size);

// Parse a 32-voice cartridge SysEx dump (F0 43 00 09 20 00 ... F7, 4104 bytes total).
// Returns a vector of up to 32 voices, or empty on error.
std::vector<Dx7Voice> parseCartridgeSysex(const uint8_t* data, size_t size);

// Unpack one 128-byte VMEM voice to 156-byte engine layout.
// Called internally by parseCartridgeSysex; exposed for testing.
void unpackVmemVoice(const uint8_t packed[128], uint8_t unpacked[156]);

} // namespace HDAW
