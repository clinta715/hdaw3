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

} // namespace mcp
