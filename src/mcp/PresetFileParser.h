#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <cstring>
#include <limits>

namespace mcp {

struct ParsedPresetFile
{
    const void* data = nullptr;
    size_t size = 0;
    juce::String format;
    juce::String presetName;
    juce::String error;

    bool ok() const noexcept { return data != nullptr && size > 0 && error.isEmpty(); }
};

inline uint32_t readPresetBE32(const uint8_t* p) noexcept
{
    return (static_cast<uint32_t>(p[0]) << 24)
        | (static_cast<uint32_t>(p[1]) << 16)
        | (static_cast<uint32_t>(p[2]) << 8)
        | static_cast<uint32_t>(p[3]);
}

inline uint32_t readPresetLE32(const uint8_t* p) noexcept
{
    return static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

inline ParsedPresetFile presetParseError(const juce::String& message)
{
    ParsedPresetFile parsed;
    parsed.error = message;
    return parsed;
}

inline ParsedPresetFile parsePresetFile(const juce::MemoryBlock& raw)
{
    const auto rawSize = raw.getSize();
    if (rawSize < 2)
        return presetParseError("preset file too small");

    const auto* bytes = static_cast<const uint8_t*>(raw.getData());

    // Raw DX7 SysEx: Yamaha manufacturer id follows F0.
    if (bytes[0] == 0xF0 && bytes[1] == 0x43)
    {
        ParsedPresetFile parsed;
        parsed.data = raw.getData();
        parsed.size = rawSize;
        parsed.format = "syx";
        return parsed;
    }

    // Serum 2 native preset container: XferJson\0 + LE metadata length + reserved + JSON + binary payload.
    static constexpr char serumMagic[] = "XferJson";
    if (rawSize >= 8 && std::memcmp(bytes, serumMagic, 8) == 0)
    {
        if (rawSize < 17)
            return presetParseError("SerumPreset file too small for XferJson header");
        if (bytes[8] != 0)
            return presetParseError("invalid SerumPreset XferJson header terminator");

        const uint32_t metadataLen = readPresetLE32(bytes + 9);
        if (metadataLen == 0)
            return presetParseError("SerumPreset metadata is empty");
        if (bytes[13] != 0 || bytes[14] != 0 || bytes[15] != 0 || bytes[16] != 0)
            return presetParseError("invalid SerumPreset reserved header bytes");

        constexpr size_t metadataOffset = 17;
        if (static_cast<size_t>(metadataLen) > rawSize - metadataOffset)
            return presetParseError("SerumPreset metadata length exceeds file size");

        const size_t payloadOffset = metadataOffset + static_cast<size_t>(metadataLen);
        if (payloadOffset >= rawSize)
            return presetParseError("SerumPreset contains no binary payload");

        juce::String metadataJson = juce::String::fromUTF8(
            reinterpret_cast<const char*>(bytes + metadataOffset),
            static_cast<int>(metadataLen));
        auto metadata = juce::JSON::parse(metadataJson);
        if (metadata.isVoid())
            return presetParseError("SerumPreset metadata is not valid JSON");

        auto* object = metadata.getDynamicObject();
        if (object == nullptr)
            return presetParseError("SerumPreset metadata JSON is not an object");

        if (object->hasProperty("fileType")
            && object->getProperty("fileType").toString() != "SerumPreset")
            return presetParseError("XferJson fileType is not SerumPreset");

        ParsedPresetFile parsed;
        parsed.data = bytes + payloadOffset;
        parsed.size = rawSize - payloadOffset;
        parsed.format = "SerumPreset";
        if (object->hasProperty("presetName"))
            parsed.presetName = object->getProperty("presetName").toString();
        return parsed;
    }

    if (rawSize < 12)
        return presetParseError("not a valid FXP, SerumPreset, or SysEx file");

    const uint32_t chunkMagic = readPresetBE32(bytes);
    if (chunkMagic != 0x43636e4b) // CcnK
        return presetParseError("not a valid FXP, SerumPreset, or SysEx file");

    if (rawSize < 60)
        return presetParseError("file too small for FXP header");

    const uint32_t fxMagic = readPresetBE32(bytes + 8);
    if (fxMagic != 0x46504368) // FPCh
        return presetParseError("not a chunk-based FXP (bad FPCh magic)");

    const uint8_t* chunkData = nullptr;
    size_t chunkSize = 0;

    const uint32_t serumChunkSize = readPresetBE32(bytes + 56);
    if (serumChunkSize > 0
        && static_cast<size_t>(serumChunkSize) <= rawSize - 60
        && 60 + static_cast<size_t>(serumChunkSize) == rawSize)
    {
        chunkData = bytes + 60;
        chunkSize = serumChunkSize;
    }

    if (chunkData == nullptr)
    {
        const uint32_t standardChunkSize = readPresetBE32(bytes + 52);
        if (standardChunkSize > 0
            && static_cast<size_t>(standardChunkSize) <= rawSize - 56
            && 56 + static_cast<size_t>(standardChunkSize) == rawSize)
        {
            chunkData = bytes + 56;
            chunkSize = standardChunkSize;
        }
    }

    if (chunkData == nullptr || chunkSize == 0)
        return presetParseError("could not locate chunk data in FXP file");
    if (chunkSize > static_cast<size_t>(std::numeric_limits<int>::max()))
        return presetParseError("preset payload too large");

    ParsedPresetFile parsed;
    parsed.data = chunkData;
    parsed.size = chunkSize;
    parsed.format = "fxp";
    return parsed;
}

// ---------------------------------------------------------------------------
// Clavia Nord Lead 2x (NodalRed2x) bank dumps (docs/plans/2026-09-12-nodal-preset-loading.md)
//
// Wire format (gearmulator 2.2.9 source/nord/n2x/n2xLib/n2xmiditypes.h,
// verified against the D:\pdf\NL2x Banks library):
//   F0 33 <device> 04 <msgType> <msgSpec> | data | F7
//   single dump = 139 B (66 nibble-encoded params), "+name" variant 149 B
//   bank multi  = 1063 B (8 x 66 params) — SMF-wrapped banks (.mid) carry the
//   SAME dumps as SMF F0 events whose length INCLUDES the trailing F7.
// ---------------------------------------------------------------------------

inline constexpr uint8_t kNordIdClavia = 0x33;
inline constexpr uint8_t kNordIdN2x = 0x04;
inline constexpr size_t kNordMaxDumpSize = 32768;   // SHM midiIn sysex margin

inline bool isNordDumpHeader(const uint8_t* bytes, size_t size) noexcept
{
    return size >= 4 && bytes[0] == 0xF0 && bytes[1] == kNordIdClavia
        && bytes[3] == kNordIdN2x;
}

inline juce::String validateNordDump(const uint8_t* bytes, size_t size)
{
    if (size < 8)
        return "dump too small (" + juce::String((int) size) + " bytes)";
    if (bytes[0] != 0xF0 || bytes[1] != kNordIdClavia)
        return "not a Clavia SysEx dump (expected F0 33)";
    if (bytes[3] != kNordIdN2x)
        return "not a Nord Lead 2x dump (expected N2x id 04)";
    if (bytes[size - 1] != 0xF7)
        return "dump is not F7-terminated";
    if (size > kNordMaxDumpSize)
        return "dump too large (" + juce::String((int) size)
             + " bytes, max 32768)";
    return {};
}

/// Split a raw .syx byte run into complete F0..F7 dumps. Bytes before the
/// first F0 are ignored; each dump must pass isNordDumpHeader. Returns the
/// number of dumps appended, or -1 when a dump is not F7-terminated (truncated
/// file) — partial trailing data is never silently queued.
inline int splitNordSyx(const uint8_t* bytes, size_t size,
                        std::vector<std::vector<uint8_t>>& outDumps)
{
    int count = 0;
    size_t i = 0;
    while (i < size)
    {
        if (bytes[i] != 0xF0)
        {
            ++i;
            continue;
        }
        const size_t start = i;
        size_t end = i + 1;
        while (end < size && bytes[end] != 0xF7)
            ++end;
        if (end >= size)
            return -1;              // unterminated dump — reject the file
        outDumps.emplace_back(bytes + start, bytes + end + 1);
        ++count;
        i = end + 1;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Roland JP-8080 (JE8086) patch dumps (D:/pdf/je8086, timbre-lib/je8086_patch.py)
//
// Wire format (gearmulator 2.2.9 source/ronaldo/je8086/jeLib/jemiditypes.h plus
// all 46 files of the real bank library, verified 2026-09-16):
//   F0 41 10 00 06 12 <a0 a1 a2> <data..> <checksum> F7
//     - 0x41 = Roland, device 0x10, model 0x0006 (JP-8080), 0x12 = DT1 data set
//     - the three address bytes are 7-bit and address 256-byte PAGES:
//         pageValue = (a0 << 14) | (a1 << 7) | a2
//     - checksum = (128 - (sum(addr) + sum(data)) % 128) % 128
//   A patch occupies 0x200 B = 2 pages, so a 64-patch bank spans 128 pages:
//     patch area (base 02 00 00 = 2 << 14):
//       rel = page - base;  bank = rel / 128;  slot = (rel % 128) / 2 + 1;
//       pageInPatch = (rel % 128) % 2
//     performance area (base 03 00 00 = 3 << 14), 128 pages per performance:
//       off = (page - base) % 128;  off < 64 -> performance common (page = off);
//       else inner = off - 64;  part = inner / 2 + 1;  pageInPatch = inner % 2
//   (PatchUpper = 0x4000 B = page 64, PatchLower = 0x4200 B = page 66.)
//
// The dump carries ONE leading byte before the documented patch body, so a
// patch name is data[1:17] and Patch.<param> = 0xNN sits at data[1 + 0xNN]
// (validated over 3893 real messages by timbre-lib/je8086_patch.py).
// ---------------------------------------------------------------------------

inline constexpr uint8_t kJp8080ModelHi = 0x00;
inline constexpr uint8_t kJp8080ModelLo = 0x06;
inline constexpr uint8_t kJp8080Dt1 = 0x12;
inline constexpr uint32_t kJp8080PatchAreaBase = 2u << 14;   // address bytes 02 00 00
inline constexpr uint32_t kJp8080PerfAreaBase  = 3u << 14;   // address bytes 03 00 00
inline constexpr uint32_t kJp8080AreaPages = 128;            // pages per bank / performance
inline constexpr uint32_t kJp8080PerfCommonPages = 64;
inline constexpr size_t kJp8080MaxDumpSize = 32768;          // SHM midiIn sysex margin

/// One JP-8080 DT1 message plus its bank/slot coordinates.
struct Jp8080Dump
{
    std::vector<uint8_t> raw;       // complete F0..F7 message
    uint32_t page = 0;
    uint32_t pageInPatch = 0;
    uint8_t area = 0;               // 2 = patch area, 3 = performance area
    int bank = 0;                   // patch area: 0-based bank; perf: 1-based performance
    int slot = 0;                   // patch area: 1..64; perf part 1..8; 0 = performance common
    bool performanceCommon = false;
    bool checksumOk = false;
};

inline uint32_t jp8080PageValue(uint8_t a0, uint8_t a1, uint8_t a2) noexcept
{
    return (static_cast<uint32_t>(a0) << 14) | (static_cast<uint32_t>(a1) << 7) | a2;
}

inline bool isJp8080Dt1Header(const uint8_t* bytes, size_t size) noexcept
{
    return size >= 9 && bytes[0] == 0xF0 && bytes[1] == 0x41
        && bytes[3] == kJp8080ModelHi && bytes[4] == kJp8080ModelLo
        && bytes[5] == kJp8080Dt1;
}

/// Fill in the bank/slot/page coordinates for a DT1 message (in place).
inline void jp8080AssignUnit(Jp8080Dump& d) noexcept
{
    if (d.raw.size() < 9)
        return;
    d.page = jp8080PageValue(d.raw[6], d.raw[7], d.raw[8]);
    if (d.page >= kJp8080PatchAreaBase && d.page < kJp8080PerfAreaBase)
    {
        const uint32_t rel = d.page - kJp8080PatchAreaBase;
        d.area = 2;
        d.bank = static_cast<int>(rel / kJp8080AreaPages);
        const uint32_t within = rel % kJp8080AreaPages;
        d.slot = static_cast<int>(within / 2) + 1;
        d.pageInPatch = within % 2;
        return;
    }
    if (d.page >= kJp8080PerfAreaBase)
    {
        const uint32_t rel = d.page - kJp8080PerfAreaBase;
        d.area = 3;
        d.bank = static_cast<int>(rel / kJp8080AreaPages) + 1;
        const uint32_t off = rel % kJp8080AreaPages;
        if (off < kJp8080PerfCommonPages)
        {
            d.performanceCommon = true;
            d.slot = 0;
            d.pageInPatch = off;
            return;
        }
        const uint32_t inner = off - kJp8080PerfCommonPages;
        d.slot = static_cast<int>(inner / 2) + 1;
        d.pageInPatch = inner % 2;
    }
}

/// Validate one complete JP-8080 DT1 message. Empty string = valid.
inline juce::String validateJp8080Dump(const uint8_t* bytes, size_t size)
{
    if (size < 10)
        return "dump too small (" + juce::String((int) size) + " bytes)";
    if (bytes[0] != 0xF0 || bytes[1] != 0x41)
        return "not a Roland SysEx dump (expected F0 41)";
    if (bytes[3] != kJp8080ModelHi || bytes[4] != kJp8080ModelLo)
        return "not a JP-8080 dump (expected model 00 06)";
    if (bytes[5] != kJp8080Dt1)
        return "not a DT1 data-set message (expected 0x12)";
    if (bytes[size - 1] != 0xF7)
        return "dump is not F7-terminated";
    if (size > kJp8080MaxDumpSize)
        return "dump too large (" + juce::String((int) size) + " bytes, max 32768)";
    uint32_t sum = 0;
    for (size_t i = 6; i + 2 < size; ++i)   // addr + data, excluding the checksum
        sum += bytes[i];
    const auto expected = static_cast<uint8_t>((128 - (sum % 128)) % 128);
    if (bytes[size - 2] != expected)
        return "checksum mismatch (expected " + juce::String((int) expected)
             + ", got " + juce::String((int) bytes[size - 2]) + ")";
    return {};
}

/// Split a raw SysEx byte run into JP-8080 DT1 messages (non-JP8080 SysEx is
/// skipped). Returns the number appended, or -1 when a dump is not
/// F7-terminated (truncated file) - partial trailing data is never queued.
inline int splitJp8080Syx(const uint8_t* bytes, size_t size, std::vector<Jp8080Dump>& out)
{
    int count = 0;
    size_t i = 0;
    while (i < size)
    {
        if (bytes[i] != 0xF0)
        {
            ++i;
            continue;
        }
        const size_t start = i;
        size_t end = i + 1;
        while (end < size && bytes[end] != 0xF7)
            ++end;
        if (end >= size)
            return -1;                      // unterminated dump - reject the file
        if (isJp8080Dt1Header(bytes + start, end + 1 - start))
        {
            Jp8080Dump d;
            d.raw.assign(bytes + start, bytes + end + 1);
            d.checksumOk = validateJp8080Dump(d.raw.data(), d.raw.size()).isEmpty();
            jp8080AssignUnit(d);
            out.push_back(std::move(d));
            ++count;
        }
        i = end + 1;
    }
    return count;
}

/// True when a DT1 message carries a patch NAME: the dump's body starts after
/// the 9-byte header and carries ONE leading byte, so the 16-char name lives at
/// raw[10..25] (same rule timbre-lib/je8086_patch.py uses for the sidecar index).
inline bool jp8080MessageHasName(const Jp8080Dump& d) noexcept
{
    constexpr size_t kNameStart = 10;   // 9-byte header + 1 leading byte
    constexpr size_t kNameLen = 16;
    if (d.raw.size() < kNameStart + kNameLen)
        return false;
    int printable = 0;
    for (size_t i = kNameStart; i < kNameStart + kNameLen; ++i)
    {
        const auto c = d.raw[i];
        if (c == 0x20 || c == 0x00)
            continue;                   // space / NUL are name padding
        if (c < 0x20 || c > 0x7E)
            return false;
        ++printable;
    }
    return printable > 0;
}

/// A patch unit = (area, bank, slot); performances also carry a common block.
struct Jp8080Unit
{
    uint8_t area = 0;
    int bank = 0;
    int slot = 0;
    bool performanceCommon = false;
};

/// Distinct patch units in file order (what a caller counts with presetIndex).
inline std::vector<Jp8080Unit> jp8080UnitsInFileOrder(const std::vector<Jp8080Dump>& dumps)
{
    std::vector<Jp8080Unit> units;
    for (const auto& d : dumps)
    {
        // Only NAME-BEARING page-0 groups are patch units. Page 1 is a
        // continuation of its unit, and unnamed groups (a performance-common
        // block, or the binary tail of a part) are not patches -- the sidecar
        // survey counts units the same way, so preset indexes stay aligned
        // (Kulshan: 128 named parts out of 192 DT1 groups).
        if (d.area == 0 || d.pageInPatch != 0 || !jp8080MessageHasName(d))
            continue;
        bool seen = false;
        for (const auto& u : units)
            if (u.area == d.area && u.bank == d.bank && u.slot == d.slot
                && u.performanceCommon == d.performanceCommon)
            {
                seen = true;
                break;
            }
        if (!seen)
            units.push_back({ d.area, d.bank, d.slot, d.performanceCommon });
    }
    return units;
}

/// Collect the dumps (page 0 and page 1) that belong to one unit.
inline int jp8080SelectUnit(const std::vector<Jp8080Dump>& dumps,
                            const Jp8080Unit& unit,
                            std::vector<const Jp8080Dump*>& out)
{
    int count = 0;
    for (const auto& d : dumps)
        if (d.area == unit.area && d.bank == unit.bank && d.slot == unit.slot
            && d.performanceCommon == unit.performanceCommon)
        {
            out.push_back(&d);
            ++count;
        }
    return count;
}

} // namespace mcp
