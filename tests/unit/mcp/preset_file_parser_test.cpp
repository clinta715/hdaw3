#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
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

// ---------------------------------------------------------------------------
// Nord Lead 2x bank dumps (load_nord_bank) — wire format verified against
// gearmulator n2xmiditypes.h + the real D:\pdf\NL2x Banks library.
// ---------------------------------------------------------------------------

namespace {

std::vector<uint8_t> makeNordSingle(uint8_t msgType, uint8_t msgSpec,
                                    const std::vector<uint8_t>& paramBytes)
{
    // F0 33 <dev> 04 <type> <spec> | 132 nibble data | F7  (139 bytes total)
    std::vector<uint8_t> dump { 0xF0, mcp::kNordIdClavia, 0x0F, mcp::kNordIdN2x,
                                msgType, msgSpec };
    for (uint8_t v : paramBytes)
    {
        dump.push_back(static_cast<uint8_t>(v & 0x0F));
        dump.push_back(static_cast<uint8_t>((v >> 4) & 0x0F));
    }
    // Pad to the real single-dump data size (132 data bytes = 66 params).
    while (dump.size() < 139 - 1)
        dump.push_back(0x00);
    dump.push_back(0xF7);
    return dump;
}

} // namespace

TEST(PresetFileParser, SplitsConcatenatedNordDumps)
{
    std::vector<uint8_t> file;
    const auto a = makeNordSingle(0, 0, std::vector<uint8_t>(66, 0x40));
    const auto b = makeNordSingle(1, 7, std::vector<uint8_t>(66, 0x20));
    std::vector<uint8_t> raw;
    raw.insert(raw.end(), a.begin(), a.end());
    raw.insert(raw.end(), b.begin(), b.end());

    std::vector<std::vector<uint8_t>> dumps;
    EXPECT_EQ(mcp::splitNordSyx(raw.data(), raw.size(), dumps), 2);
    ASSERT_EQ(dumps.size(), 2u);
    EXPECT_EQ(dumps[0].size(), a.size());
    EXPECT_EQ(dumps[1].size(), b.size());
    EXPECT_TRUE(mcp::isNordDumpHeader(dumps[0].data(), dumps[0].size()));
    EXPECT_TRUE(mcp::validateNordDump(dumps[0].data(), dumps[0].size()).isEmpty());
    EXPECT_TRUE(mcp::validateNordDump(dumps[1].data(), dumps[1].size()).isEmpty());
}

TEST(PresetFileParser, NordValidationRejectsNonClaviaHeader)
{
    const std::vector<uint8_t> yamaha { 0xF0, 0x43, 0x00, 0x04, 0x00, 0x00,
                                        0x01, 0xF7 };
    EXPECT_FALSE(mcp::validateNordDump(yamaha.data(), yamaha.size()).isEmpty());

    const std::vector<uint8_t> wrongId { 0xF0, 0x33, 0x0F, 0x07, 0x00, 0x00,
                                         0x01, 0xF7 };
    EXPECT_TRUE(mcp::validateNordDump(wrongId.data(), wrongId.size())
                    .contains("Nord Lead 2x"));
}

TEST(PresetFileParser, NordValidationRejectsUnterminatedAndOversized)
{
    // Full-size dump with the F7 stripped (>=8 bytes so the F7 check is hit).
    std::vector<uint8_t> unterminated { 0xF0, 0x33, 0x0F, 0x04, 0x00, 0x00 };
    for (int i = 0; i < 132; ++i)
        unterminated.push_back(0x00);
    EXPECT_TRUE(mcp::validateNordDump(unterminated.data(), unterminated.size())
                    .contains("F7"));

    std::vector<uint8_t> oversized { 0xF0, 0x33, 0x0F, 0x04, 0x00, 0x00 };
    oversized.resize(mcp::kNordMaxDumpSize + 1, 0x00);
    oversized.back() = 0xF7;
    EXPECT_TRUE(mcp::validateNordDump(oversized.data(), oversized.size())
                    .contains("too large"));
}

TEST(PresetFileParser, NordSyxSplitRejectsTruncatedFile)
{
    const std::vector<uint8_t> truncated { 0xF0, 0x33, 0x0F, 0x04, 0x00, 0x00,
                                           0x01, 0x02 };
    std::vector<std::vector<uint8_t>> dumps;
    EXPECT_EQ(mcp::splitNordSyx(truncated.data(), truncated.size(), dumps), -1);
}

TEST(PresetFileParser, NordMidUnwrapJoinsF0AndF7)
{
    // SMF F0 events carry the payload WITHOUT the leading F0 but WITH the
    // trailing F7 (varlen includes it). JUCE re-joins F0..F7 into one
    // MidiMessage — the exact normalization load_nord_bank relies on.
    const auto dump = makeNordSingle(0, 0, std::vector<uint8_t>(66, 0x40));

    juce::MidiFile mf;
    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage(dump.data(), static_cast<int>(dump.size())));
    mf.addTrack(seq);

    juce::MemoryOutputStream out;
    mf.writeTo(out);
    const auto midBytes = out.getMemoryBlock();

    // Re-read the SMF the same way the MCP tool does.
    juce::MemoryInputStream in(midBytes, false);
    juce::MidiFile round;
    ASSERT_TRUE(round.readFrom(in));
    int sysexCount = 0;
    std::vector<uint8_t> recovered;
    for (int t = 0; t < round.getNumTracks(); ++t)
    {
        const auto* s = round.getTrack(t);
        for (int e = 0; e < s->getNumEvents(); ++e)
        {
            const auto meta = s->getEventPointer(e);
            if (!meta->message.isSysEx())
                continue;
            ++sysexCount;
            const auto* raw = meta->message.getRawData();
            recovered.assign(raw, raw + meta->message.getRawDataSize());
        }
    }
    ASSERT_EQ(sysexCount, 1);
    EXPECT_EQ(recovered.size(), dump.size());
    EXPECT_TRUE(std::equal(dump.begin(), dump.end(), recovered.begin()));
    EXPECT_TRUE(mcp::validateNordDump(recovered.data(), recovered.size()).isEmpty());
}
