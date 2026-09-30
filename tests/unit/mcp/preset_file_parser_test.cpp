#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "mcp/PresetFileParser.h"
#include "common/WaldorfEditBuffer.h"   // microQ edit-buffer retarget (Phase 5)
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
// Waldorf Microwave XT / microQ dumps (apply_preset Xenia/Vavra route).
// ---------------------------------------------------------------------------

TEST(PresetFileParser, SplitsConcatenatedWaldorfXeniaDumps)
{
    const std::vector<uint8_t> a { 0xF0, mcp::kWaldorfId, mcp::kWaldorfMachineMw2,
                                   0x7F, 0x10, 0x00, 0xF7 };
    const std::vector<uint8_t> b { 0xF0, mcp::kWaldorfId, mcp::kWaldorfMachineMw2,
                                   0x7F, 0x20, 0x01, 0x02, 0xF7 };
    std::vector<uint8_t> raw;
    raw.insert(raw.end(), a.begin(), a.end());
    raw.insert(raw.end(), b.begin(), b.end());

    std::vector<std::vector<uint8_t>> dumps;
    EXPECT_EQ(mcp::splitWaldorfSyx(raw.data(), raw.size(),
        mcp::kWaldorfMachineMw2, dumps), 2);
    ASSERT_EQ(dumps.size(), 2u);
    EXPECT_EQ(dumps[0], a);
    EXPECT_EQ(dumps[1], b);
    EXPECT_TRUE(mcp::isWaldorfDumpHeader(dumps[0].data(), dumps[0].size(),
        mcp::kWaldorfMachineMw2));
    EXPECT_TRUE(mcp::validateWaldorfDump(dumps[0].data(), dumps[0].size(),
        mcp::kWaldorfMachineMw2, "Microwave XT/Xenia").isEmpty());
}

TEST(PresetFileParser, WaldorfValidationRejectsWrongMachineAndTruncatedDump)
{
    const std::vector<uint8_t> microQ { 0xF0, mcp::kWaldorfId,
        mcp::kWaldorfMachineMicroQ, 0x7F, 0x10, 0x00, 0xF7 };
    EXPECT_TRUE(mcp::validateWaldorfDump(microQ.data(), microQ.size(),
        mcp::kWaldorfMachineMw2, "Microwave XT/Xenia").contains("Microwave XT/Xenia"));

    std::vector<std::vector<uint8_t>> dumps;
    const std::vector<uint8_t> truncated { 0xF0, mcp::kWaldorfId,
        mcp::kWaldorfMachineMw2, 0x7F, 0x10, 0x00 };
    EXPECT_EQ(mcp::splitWaldorfSyx(truncated.data(), truncated.size(),
        mcp::kWaldorfMachineMw2, dumps), -1);

    EXPECT_EQ(mcp::splitWaldorfSyx(microQ.data(), microQ.size(),
        mcp::kWaldorfMachineMw2, dumps), -2);
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

// ---------------------------------------------------------------------------
// Roland JP-8080 (JE8086) DT1 dumps
// ---------------------------------------------------------------------------
namespace {

uint8_t jpChecksum(const std::vector<uint8_t>& addrData)
{
    uint32_t sum = 0;
    for (auto b : addrData)
        sum += b;
    return static_cast<uint8_t>((128 - (sum % 128)) % 128);
}

std::vector<uint8_t> jpDt1(uint8_t a0, uint8_t a1, uint8_t a2, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> bytes { 0xF0, 0x41, 0x10, 0x00, 0x06, 0x12, a0, a1, a2 };
    bytes.insert(bytes.end(), data.begin(), data.end());
    std::vector<uint8_t> forCk { a0, a1, a2 };
    forCk.insert(forCk.end(), data.begin(), data.end());
    bytes.push_back(jpChecksum(forCk));
    bytes.push_back(0xF7);
    return bytes;
}

// 256-byte patch page whose name sits at data[1:17] (the dump's leading byte)
std::vector<uint8_t> jpPage(const juce::String& name)
{
    std::vector<uint8_t> page(256, 0);
    const auto text = name.paddedRight(' ', 16).substring(0, 16).toStdString();
    for (size_t i = 0; i < text.size(); ++i)
        page[1 + i] = static_cast<uint8_t>(text[i]);
    return page;
}

std::vector<uint8_t> jpBankStream(int patches)
{
    std::vector<uint8_t> out;
    for (int p = 0; p < patches; ++p)
    {
        const uint32_t rel = static_cast<uint32_t>(p) * 2;
        const auto lo = static_cast<uint8_t>(rel % 128);
        const auto hi = static_cast<uint8_t>((rel / 128) % 128);
        const auto page0 = jpPage("PATCH " + juce::String(p + 1));
        std::vector<uint8_t> page1(16, 0);
        page1[0] = 0x2A;
        for (const auto& m : { jpDt1(2, hi, lo, page0), jpDt1(2, hi, static_cast<uint8_t>(lo + 1), page1) })
            out.insert(out.end(), m.begin(), m.end());
    }
    return out;
}

} // namespace

TEST(PresetFileParser, Jp8080ValidatesHeaderChecksumAndSize)
{
    auto good = jpDt1(2, 0, 0, jpPage("BASS"));
    ASSERT_EQ(mcp::validateJp8080Dump(good.data(), good.size()), juce::String());
    EXPECT_TRUE(mcp::isJp8080Dt1Header(good.data(), good.size()));

    auto badChecksum = good;
    badChecksum[20] ^= 0x01;
    EXPECT_TRUE(mcp::validateJp8080Dump(badChecksum.data(), badChecksum.size())
                    .containsIgnoreCase("checksum"));

    auto wrongModel = good;
    wrongModel[4] = 0x05;
    EXPECT_TRUE(mcp::validateJp8080Dump(wrongModel.data(), wrongModel.size())
                    .containsIgnoreCase("not a JP-8080"));

    auto unterminated = good;
    unterminated.back() = 0x00;
    EXPECT_TRUE(mcp::validateJp8080Dump(unterminated.data(), unterminated.size())
                    .containsIgnoreCase("F7"));
}

TEST(PresetFileParser, Jp8080SplitAssignsBankAndSlot)
{
    const auto stream = jpBankStream(2);
    std::vector<mcp::Jp8080Dump> dumps;
    EXPECT_EQ(mcp::splitJp8080Syx(stream.data(), stream.size(), dumps), 4);
    ASSERT_EQ(dumps.size(), 4u);
    EXPECT_TRUE(dumps[0].checksumOk);
    EXPECT_EQ(static_cast<int>(dumps[0].area), 2);
    EXPECT_EQ(dumps[0].bank, 0);
    EXPECT_EQ(dumps[0].slot, 1);
    EXPECT_EQ(static_cast<int>(dumps[0].pageInPatch), 0);
    EXPECT_EQ(dumps[1].slot, 1);
    EXPECT_EQ(static_cast<int>(dumps[1].pageInPatch), 1);
    EXPECT_EQ(dumps[2].slot, 2);
    EXPECT_EQ(static_cast<int>(dumps[2].pageInPatch), 0);

    const auto units = mcp::jp8080UnitsInFileOrder(dumps);
    ASSERT_EQ(units.size(), 2u);
    EXPECT_EQ(units[0].slot, 1);
    EXPECT_EQ(units[1].slot, 2);

    std::vector<const mcp::Jp8080Dump*> selected;
    EXPECT_EQ(mcp::jp8080SelectUnit(dumps, units[1], selected), 2);
    ASSERT_EQ(selected.size(), 2u);
    EXPECT_EQ(selected[0]->slot, 2);
    EXPECT_EQ(selected[1]->slot, 2);
}

TEST(PresetFileParser, Jp8080PerformanceUnitsAndCommonBlock)
{
    // performance 1: common block at pages 0..1, parts at pages 64 and 66
    std::vector<uint8_t> stream;
    for (const auto& m : { jpDt1(3, 0, 0, jpPage("PERF NAME")),
                           jpDt1(3, 0, 64, jpPage("PART ONE")),
                           jpDt1(3, 0, 65, std::vector<uint8_t>(16, 0)),
                           jpDt1(3, 0, 66, jpPage("PART TWO")) })
        stream.insert(stream.end(), m.begin(), m.end());

    std::vector<mcp::Jp8080Dump> dumps;
    ASSERT_EQ(mcp::splitJp8080Syx(stream.data(), stream.size(), dumps), 4);
    const auto units = mcp::jp8080UnitsInFileOrder(dumps);
    ASSERT_EQ(units.size(), 3u);
    EXPECT_TRUE(units[0].performanceCommon);
    EXPECT_EQ(units[0].slot, 0);
    EXPECT_EQ(units[1].slot, 1);
    EXPECT_EQ(units[2].slot, 2);
    EXPECT_EQ(units[1].bank, 1);
    EXPECT_EQ(static_cast<int>(units[1].area), 3);

    std::vector<const mcp::Jp8080Dump*> partOne;
    EXPECT_EQ(mcp::jp8080SelectUnit(dumps, units[1], partOne), 2);
    std::vector<const mcp::Jp8080Dump*> common;
    EXPECT_EQ(mcp::jp8080SelectUnit(dumps, units[0], common), 1);
}

TEST(PresetFileParser, Jp8080RejectsTruncatedAndSkipsForeignSysex)
{
    std::vector<uint8_t> truncated { 0xF0, 0x41, 0x10, 0x00, 0x06, 0x12, 2, 0, 0, 1, 2 };
    std::vector<mcp::Jp8080Dump> dumps;
    EXPECT_EQ(mcp::splitJp8080Syx(truncated.data(), truncated.size(), dumps), -1);

    // a Clavia dump in the same stream is skipped, never mis-decoded as JP-8080
    std::vector<uint8_t> mixed { 0xF0, 0x33, 0x00, 0x04, 0x01, 0x08, 0x00, 0xF7 };
    const auto jp = jpDt1(2, 0, 2, jpPage("ONLY"));
    mixed.insert(mixed.end(), jp.begin(), jp.end());
    std::vector<mcp::Jp8080Dump> only;
    EXPECT_EQ(mcp::splitJp8080Syx(mixed.data(), mixed.size(), only), 1);
    ASSERT_EQ(only.size(), 1u);
    EXPECT_EQ(only[0].slot, 2);
}


TEST(PresetFileParser, Jp8080UnitsCountOnlyNamedPatches)
{
    // A performance bank: an UNNAMED common block, two named parts (pages 0), and
    // a page-1 continuation. Only the two named parts are patch units, so a
    // preset index matches the sidecar survey's numbering.
    std::vector<uint8_t> unnamed(256, 0);
    unnamed[1] = 0x01;                       // binary, not a name
    std::vector<uint8_t> stream;
    for (const auto& m : { jpDt1(3, 0, 0, unnamed),
                           jpDt1(3, 0, 64, jpPage("PART ONE")),
                           jpDt1(3, 0, 65, std::vector<uint8_t>(16, 0)),
                           jpDt1(3, 0, 66, jpPage("PART TWO")) })
        stream.insert(stream.end(), m.begin(), m.end());

    std::vector<mcp::Jp8080Dump> dumps;
    ASSERT_EQ(mcp::splitJp8080Syx(stream.data(), stream.size(), dumps), 4);
    const auto units = mcp::jp8080UnitsInFileOrder(dumps);
    ASSERT_EQ(units.size(), 2u) << "the unnamed common block and page 1 are not units";
    EXPECT_EQ(units[0].slot, 1);
    EXPECT_EQ(units[1].slot, 2);

    // ...but the selected unit still carries its page-1 continuation (both pages
    // are injected, so a 2-page patch loads whole).
    std::vector<const mcp::Jp8080Dump*> sel;
    EXPECT_EQ(mcp::jp8080SelectUnit(dumps, units[0], sel), 2);
}

// Real-library contract: the loader's unit numbering must match the sidecar
// survey's (timbre-lib/je8086_patch.py). Skips when the bank library is absent.
TEST(PresetFileParser, Jp8080RealKulshanBankUnitsMatchTheSidecarIndex)
{
    const juce::File bank("D:/pdf/je8086/Kulshan Mystical Psytrance.mid");
    if (!bank.existsAsFile())
        GTEST_SKIP() << "JP-8080 bank library not mounted";
    juce::MemoryBlock block;
    ASSERT_TRUE(bank.loadFileAsData(block));
    juce::MemoryInputStream in(block, false);
    juce::MidiFile mf;
    ASSERT_TRUE(mf.readFrom(in));
    std::vector<uint8_t> run;
    int sysexEvents = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* seq = mf.getTrack(t);
        for (int ev = 0; ev < seq->getNumEvents(); ++ev)
        {
            const auto msg = seq->getEventPointer(ev)->message;
            if (!msg.isSysEx())
                continue;
            ++sysexEvents;
            const auto* raw = msg.getRawData();
            run.insert(run.end(), raw, raw + static_cast<size_t>(msg.getRawDataSize()));
        }
    }
    std::vector<mcp::Jp8080Dump> dumps;
    const int parsed = mcp::splitJp8080Syx(run.data(), run.size(), dumps);
    const size_t units = mcp::jp8080UnitsInFileOrder(dumps).size();
    std::cout << "[Jp8080 real bank] sysexEvents=" << sysexEvents
              << " parsed=" << parsed << " dumps=" << dumps.size()
              << " units=" << units << "\n";
    for (size_t i = 0; i < dumps.size() && i < 4; ++i)
        std::cout << "   dump[" << i << "] area=" << (int) dumps[i].area
                  << " bank=" << dumps[i].bank << " slot=" << dumps[i].slot
                  << " pageInPatch=" << dumps[i].pageInPatch
                  << " size=" << dumps[i].raw.size()
                  << " named=" << (mcp::jp8080MessageHasName(dumps[i]) ? 1 : 0) << "\n";
    // 320 SysEx events = 64 performances x (1 common block across 3 pages +
    // part1 + part2). The pre-fix Python decoder reported 128 because it dropped
    // every message whose SMF varint prefix was a single byte (payload < 128 B)
    // - which is exactly the cross-check this test exists to guard.
    EXPECT_EQ(sysexEvents, 320) << "SMF holds 320 DT1 events (verified 2026-09-16)";
    ASSERT_EQ(parsed, 320);
    EXPECT_EQ(units, 192u) << "64 named performance commons + 128 parts";
}

// ---------------------------------------------------------------------------
// microQ/Vavra edit-buffer retarget (src/common/WaldorfEditBuffer.h) — the ONE
// implementation both the file-loader path and apply_matrix_preset call.
//
// Source evidence (2026-09-30): all 40 timbre-lib/matrix_presets/vavra.json
// presets, all 20 vavra_morphs.json steps and 528/528 corpus .syx carry
// byte5 = 0x30 (multi-edit) — a buffer the single-mode OS does not play, so an
// injection aimed there is silent.
// ---------------------------------------------------------------------------

namespace {

constexpr size_t kMicroQDumpSize = 392;

/// Real-shaped microQ single dump, VALID for its own buffer bytes: 0xF0 3E 10,
/// byte5 = the buffer (0x30 multi-edit in the corpus), byte6 = the sound
/// location, a deterministic payload, F7 last and a correct Waldorf checksum at
/// [size-2] (sum of [4 .. size-2) & 0x7F).
std::vector<uint8_t> makeMicroQDump(uint8_t bufferByte, uint8_t locationByte,
                                    size_t size = kMicroQDumpSize,
                                    uint8_t machine = mcp::kWaldorfMachineMicroQ)
{
    std::vector<uint8_t> d(size, 0);
    d[0] = 0xF0;
    d[1] = mcp::kWaldorfId;
    d[2] = machine;
    d[3] = 0x00;   // device id
    d[4] = 0x01;   // command
    d[5] = bufferByte;
    d[6] = locationByte;
    for (size_t i = 7; i + 2 < size; ++i)
        d[i] = static_cast<uint8_t>(i & 0x7f);
    d[size - 1] = 0xF7;
    uint8_t cs = 0;
    for (size_t i = 4; i + 2 < size; ++i)
        cs += d[i];
    d[size - 2] = cs & 0x7f;
    return d;
}

uint8_t waldorfChecksum(const std::vector<uint8_t>& d)
{
    uint8_t cs = 0;
    for (size_t i = 4; i + 2 < d.size(); ++i)
        cs += d[i];
    return cs & 0x7f;
}

} // namespace

// G1: a 392-byte microQ dump is retargeted to the single-mode edit buffer and the
// checksum is RECOMPUTED (the dump was valid before, so the stored byte changes).
TEST(WaldorfEditBuffer, MicroQ392ByteDumpIsRetargetedAndRechecksummed)
{
    const auto raw = makeMicroQDump(0x30, 0x40);
    ASSERT_EQ(raw.size(), kMicroQDumpSize);
    ASSERT_EQ(raw[5], 0x30) << "the fixture must carry the real multi-edit buffer byte";
    const uint8_t rawChecksum = raw[390];

    // The expectation is built WITHOUT the helper (independent arithmetic).
    auto expected = raw;
    expected[5] = 0x20;   // SingleEditBufferSingleMode
    expected[6] = 0x00;   // EditBufferCurrentSingle
    expected[390] = waldorfChecksum(expected);
    EXPECT_NE(expected[390], rawChecksum)
        << "0x30->0x20 and 0x40->0x00 must move the checksum (+0x50 mod 0x80)";

    auto out = raw;
    EXPECT_TRUE(HDAW::retargetWaldorfDumpForSingleEditBuffer(
        out, HDAW::waldorfMachineForName("vavra")));
    EXPECT_EQ(out, expected);
    EXPECT_EQ(out[5], 0x20);
    EXPECT_EQ(out[6], 0x00);
    EXPECT_EQ(out[390], waldorfChecksum(out));
    EXPECT_NE(out[390], rawChecksum);
    // Nothing else moved.
    for (size_t i = 0; i < out.size(); ++i)
        if (i != 5 && i != 6 && i != 390)
            ASSERT_EQ(out[i], raw[i]) << "byte " << i << " must be untouched";

    // The batch spelling (the file-loader path's shape) is the same transform.
    std::vector<std::vector<uint8_t>> batch { raw, raw };
    EXPECT_TRUE(HDAW::retargetWaldorfDumpsForSingleEditBuffer(
        batch, mcp::kWaldorfMachineMicroQ));
    EXPECT_EQ(batch[0], expected);
    EXPECT_EQ(batch[1], expected);
}

// G2: WITHOUT the retarget the same dump is byte-identical to the input — i.e.
// the 0x30 bytes the corpus carries really are what goes out when the helper is
// not called (so the call-site tests below can actually fail).
TEST(WaldorfEditBuffer, UntargetedMicroQDumpKeepsTheMultiEditBufferBytes)
{
    const auto raw = makeMicroQDump(0x30, 0x40);
    const auto untouched = raw;   // a path that never calls the helper

    EXPECT_EQ(untouched, raw);
    EXPECT_EQ(untouched[5], 0x30);
    EXPECT_EQ(untouched[6], 0x40);

    // ...and it is NOT what a retargeted dump looks like.
    auto target = raw;
    HDAW::retargetWaldorfDumpForSingleEditBuffer(target, mcp::kWaldorfMachineMicroQ);
    EXPECT_NE(target, untouched);
    EXPECT_NE(target[5], untouched[5]);
    EXPECT_NE(target[6], untouched[6]);
    EXPECT_NE(target[390], untouched[390]);
}

// G3: no over-reach — a non-microQ dump (Xenia/Microwave XT) and a microQ dump
// that is not 392 bytes pass through byte-identical and report "unchanged".
TEST(WaldorfEditBuffer, NonMicroQAndNon392ByteDumpsPassThroughByteIdentical)
{
    // (a) microQ, wrong size (the 265-byte XT-ish and 528-byte bank shapes).
    const size_t sizes[] = { 391, 393, 265 };
    for (const size_t size : sizes)
    {
        auto d = makeMicroQDump(0x30, 0x40, size, mcp::kWaldorfMachineMicroQ);
        const auto before = d;
        EXPECT_FALSE(HDAW::retargetWaldorfDumpForSingleEditBuffer(
            d, mcp::kWaldorfMachineMicroQ)) << "size " << size;
        EXPECT_EQ(d, before) << "size " << size;
    }

    // (b) NON-microQ 392-byte dump with the same 0x30 buffer byte (Xenia).
    auto xenia = makeMicroQDump(0x30, 0x40, kMicroQDumpSize, mcp::kWaldorfMachineMw2);
    const auto xeniaBefore = xenia;
    EXPECT_FALSE(HDAW::retargetWaldorfDumpForSingleEditBuffer(
        xenia, HDAW::waldorfMachineForName("Xenia.clap")));
    EXPECT_EQ(xenia, xeniaBefore);
    EXPECT_EQ(xenia[5], 0x30);
    EXPECT_EQ(xenia[6], 0x40);

    // (c) Xenia's already-0x20 dump is untouched too (machine guard, not size).
    auto xenia20 = makeMicroQDump(0x20, 0x00, kMicroQDumpSize, mcp::kWaldorfMachineMw2);
    const auto xenia20Before = xenia20;
    EXPECT_FALSE(HDAW::retargetWaldorfDumpForSingleEditBuffer(
        xenia20, mcp::kWaldorfMachineMw2));
    EXPECT_EQ(xenia20, xenia20Before);
    EXPECT_EQ(xenia20[390], xenia20Before[390]);

    // (d) degenerate inputs are a clean no-op (never a write).
    EXPECT_FALSE(HDAW::retargetWaldorfDumpForSingleEditBuffer(
        static_cast<uint8_t*>(nullptr), kMicroQDumpSize, mcp::kWaldorfMachineMicroQ));
    std::vector<uint8_t> empty;
    EXPECT_FALSE(HDAW::retargetWaldorfDumpForSingleEditBuffer(
        empty, mcp::kWaldorfMachineMicroQ));
    std::vector<std::vector<uint8_t>> noDumps;
    EXPECT_FALSE(HDAW::retargetWaldorfDumpsForSingleEditBuffer(
        noDumps, mcp::kWaldorfMachineMicroQ));
}

// The identity -> machine rule is shared: a matrix sheet's engine id and a
// plugin id must resolve to the same machine byte, or the matrix route would
// silently skip (or wrongly apply) the retarget.
TEST(WaldorfEditBuffer, MachineIdentityIsSharedBySheetEngineIdAndPluginId)
{
    EXPECT_EQ(HDAW::waldorfMachineForName("vavra"), mcp::kWaldorfMachineMicroQ);
    EXPECT_EQ(HDAW::waldorfMachineForName("Vavra.clap"), mcp::kWaldorfMachineMicroQ);
    EXPECT_EQ(HDAW::waldorfMachineForName("xenia"), mcp::kWaldorfMachineMw2);
    EXPECT_EQ(HDAW::waldorfMachineForName("Xenia.clap"), mcp::kWaldorfMachineMw2);
    // A non-Waldorf engine id can never be the microQ.
    EXPECT_EQ(HDAW::waldorfMachineForName("je8086"), mcp::kWaldorfMachineMw2);
    EXPECT_EQ(HDAW::waldorfMachineForName(""), mcp::kWaldorfMachineMw2);
}