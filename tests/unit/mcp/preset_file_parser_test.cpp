#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include "mcp/PresetFileParser.h"
#include <vector>

namespace {

juce::MemoryBlock blockFromVector(const std::vector<uint8_t>& bytes)
{
    return juce::MemoryBlock(bytes.data(), bytes.size());
}

void writeBE32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    bytes[offset + 0] = static_cast<uint8_t>((value >> 24) & 0xff);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xff);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xff);
    bytes[offset + 3] = static_cast<uint8_t>(value & 0xff);
}

void writeLE32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    bytes[offset + 0] = static_cast<uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xff);
    bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

std::vector<uint8_t> makeFxp(size_t sizeOffset, size_t dataOffset, const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> bytes(dataOffset + payload.size(), 0);
    bytes[0] = 'C'; bytes[1] = 'c'; bytes[2] = 'n'; bytes[3] = 'K';
    bytes[8] = 'F'; bytes[9] = 'P'; bytes[10] = 'C'; bytes[11] = 'h';
    writeBE32(bytes, sizeOffset, static_cast<uint32_t>(payload.size()));
    std::copy(payload.begin(), payload.end(), bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset));
    return bytes;
}

std::vector<uint8_t> makeSerumPreset(const juce::String& metadataJson, const std::vector<uint8_t>& payload)
{
    const auto metadata = metadataJson.toRawUTF8();
    const auto metadataLen = static_cast<uint32_t>(std::strlen(metadata));
    std::vector<uint8_t> bytes(17 + metadataLen + payload.size(), 0);
    const char magic[] = "XferJson";
    std::copy(magic, magic + 8, bytes.begin());
    writeLE32(bytes, 9, metadataLen);
    std::copy(metadata, metadata + metadataLen, bytes.begin() + 17);
    std::copy(payload.begin(), payload.end(), bytes.begin() + 17 + metadataLen);
    return bytes;
}

std::vector<uint8_t> payloadBytes(const mcp::ParsedPresetFile& parsed)
{
    const auto* bytes = static_cast<const uint8_t*>(parsed.data);
    return { bytes, bytes + parsed.size };
}

} // namespace

TEST(PresetFileParser, ParsesDx7SysExAsWholePayload)
{
    const std::vector<uint8_t> syx { 0xF0, 0x43, 0x00, 0x09, 0x20, 0x00, 0x7f, 0xF7 };
    const auto raw = blockFromVector(syx);

    const auto parsed = mcp::parsePresetFile(raw);

    ASSERT_TRUE(parsed.ok()) << parsed.error;
    EXPECT_EQ(parsed.format, "syx");
    EXPECT_EQ(parsed.size, syx.size());
    EXPECT_EQ(payloadBytes(parsed), syx);
}

TEST(PresetFileParser, ParsesSerum2LayoutFxpChunkAt56DataAt60)
{
    const std::vector<uint8_t> payload { 0x10, 0x20, 0x30, 0x40, 0x50 };
    const auto raw = blockFromVector(makeFxp(56, 60, payload));

    const auto parsed = mcp::parsePresetFile(raw);

    ASSERT_TRUE(parsed.ok()) << parsed.error;
    EXPECT_EQ(parsed.format, "fxp");
    EXPECT_EQ(payloadBytes(parsed), payload);
}

TEST(PresetFileParser, ParsesStandardFxpChunkAt52DataAt56)
{
    const std::vector<uint8_t> payload { 0xaa, 0xbb, 0xcc, 0xdd };
    const auto raw = blockFromVector(makeFxp(52, 56, payload));

    const auto parsed = mcp::parsePresetFile(raw);

    ASSERT_TRUE(parsed.ok()) << parsed.error;
    EXPECT_EQ(parsed.format, "fxp");
    EXPECT_EQ(payloadBytes(parsed), payload);
}

TEST(PresetFileParser, ParsesNativeSerumPresetXferJsonMetadataAndPayload)
{
    const std::vector<uint8_t> payload { 0xde, 0xad, 0xbe, 0xef, 0x01, 0x02 };
    const auto raw = blockFromVector(makeSerumPreset(
        R"({"fileType":"SerumPreset","presetName":"Acid Test"})", payload));

    const auto parsed = mcp::parsePresetFile(raw);

    ASSERT_TRUE(parsed.ok()) << parsed.error;
    EXPECT_EQ(parsed.format, "SerumPreset");
    EXPECT_EQ(parsed.presetName, "Acid Test");
    EXPECT_EQ(payloadBytes(parsed), payload);
}

TEST(PresetFileParser, RejectsMalformedSerumPresetBadJson)
{
    const std::vector<uint8_t> payload { 0x01, 0x02 };
    const auto raw = blockFromVector(makeSerumPreset(R"({"fileType":"SerumPreset")", payload));

    const auto parsed = mcp::parsePresetFile(raw);

    EXPECT_FALSE(parsed.ok());
    EXPECT_TRUE(parsed.error.contains("JSON"));
}

TEST(PresetFileParser, RejectsMalformedSerumPresetWrongFileType)
{
    const std::vector<uint8_t> payload { 0x01, 0x02 };
    const auto raw = blockFromVector(makeSerumPreset(R"({"fileType":"NotSerum"})", payload));

    const auto parsed = mcp::parsePresetFile(raw);

    EXPECT_FALSE(parsed.ok());
    EXPECT_TRUE(parsed.error.contains("fileType"));
}

TEST(PresetFileParser, RejectsMalformedSerumPresetWithoutPayload)
{
    const auto raw = blockFromVector(makeSerumPreset(R"({"fileType":"SerumPreset"})", {}));

    const auto parsed = mcp::parsePresetFile(raw);

    EXPECT_FALSE(parsed.ok());
    EXPECT_TRUE(parsed.error.contains("payload"));
}
