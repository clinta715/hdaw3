// Unit tests for src/common/ParamOverrideLedger.h — the ONE ledger grammar
// shared by every writer (McpTools_Matrix matrix presets,
// AudioEngineCommands::setPluginParam plugin-param persistence) and the
// reader (ExportManager::replayAppliedParamOverrides).
//
// Plan: docs/plans/2026-09-21-plugin-param-persistence.md
// Root cause it serves: docs/plans/2026-09-21-vavra-live-param-delivery.md
#include <gtest/gtest.h>

#include "common/ParamOverrideLedger.h"

using HDAW::formatParamOverride;
using HDAW::mergeParamOverride;
using HDAW::parseParamOverrides;
using HDAW::removeParamOverride;

// The exact wire form the matrix writer emitted before the refactor: a clean
// value formats with 6 decimal places (byte-identical ledger grammar).
TEST(ParamOverrideLedger, FormatMatchesLedgerGrammar)
{
    EXPECT_EQ(formatParamOverride(12, 0.5f), juce::String("12=0.500000"));
    EXPECT_EQ(formatParamOverride(0, 0.0f), juce::String("0=0.000000"));
    EXPECT_EQ(formatParamOverride(6801, 1.0f), juce::String("6801=1.000000"));
}

TEST(ParamOverrideLedger, ParseRejectsEmptyAndMalformedTokens)
{
    EXPECT_TRUE(parseParamOverrides("").empty());
    EXPECT_TRUE(parseParamOverrides("abc").empty());   // no '='
    EXPECT_TRUE(parseParamOverrides("=1").empty());    // empty index
    EXPECT_TRUE(parseParamOverrides("-5=1").empty());  // negative index
    EXPECT_TRUE(parseParamOverrides(";").empty());
}

TEST(ParamOverrideLedger, ParsePreservesOrderAndDuplicates)
{
    const auto p = parseParamOverrides("3=0.100000;1=0.200000;3=0.300000");
    ASSERT_EQ(p.size(), 3u);
    EXPECT_EQ(p[0].first, 3);
    EXPECT_NEAR(p[0].second, 0.1f, 1e-6f);
    EXPECT_EQ(p[1].first, 1);
    EXPECT_NEAR(p[1].second, 0.2f, 1e-6f);
    EXPECT_EQ(p[2].first, 3); // duplicate kept: replay keeps last-wins semantics
    EXPECT_NEAR(p[2].second, 0.3f, 1e-6f);
}

TEST(ParamOverrideLedger, MergeReplacesFirstOccurrenceInPlace)
{
    EXPECT_EQ(mergeParamOverride("3=0.100000;1=0.200000", 3, 0.9f),
              juce::String("3=0.900000;1=0.200000"));
}

TEST(ParamOverrideLedger, MergeCollapsesDuplicateIndices)
{
    EXPECT_EQ(mergeParamOverride("3=0.100000;1=0.200000;3=0.300000", 3, 0.9f),
              juce::String("3=0.900000;1=0.200000"));
}

TEST(ParamOverrideLedger, MergeAppendsMissingIndex)
{
    EXPECT_EQ(mergeParamOverride("1=0.200000", 7, 0.5f),
              juce::String("1=0.200000;7=0.500000"));
    EXPECT_EQ(mergeParamOverride("", 7, 0.5f), juce::String("7=0.500000"));
}

TEST(ParamOverrideLedger, MergeIsIdempotentAndStableForSameValue)
{
    const juce::String once = mergeParamOverride("1=0.200000", 3, 0.4f);
    EXPECT_EQ(mergeParamOverride(once, 3, 0.4f), once);
    // Same value written again must not change the string, so the caller's
    // `next != ledger` guard skips a redundant property write (lesson 2).
    EXPECT_EQ(mergeParamOverride("5=0.500000", 5, 0.5f), juce::String("5=0.500000"));
}

TEST(ParamOverrideLedger, MergeNegativeIndexIsNoOp)
{
    EXPECT_EQ(mergeParamOverride("1=0.200000", -1, 0.5f), juce::String("1=0.200000"));
}

TEST(ParamOverrideLedger, RemoveDropsEveryOccurrence)
{
    EXPECT_EQ(removeParamOverride("3=0.100000;1=0.200000;3=0.300000", 3),
              juce::String("1=0.200000"));
    EXPECT_EQ(removeParamOverride("3=0.100000", 3), juce::String());
    EXPECT_EQ(removeParamOverride("1=0.200000", 3), juce::String("1=0.200000"));
}

TEST(ParamOverrideLedger, FormatParseRoundTrip)
{
    const auto p = parseParamOverrides(formatParamOverride(6801, 0.05f));
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].first, 6801);
    EXPECT_NEAR(p[0].second, 0.05f, 1e-6f);
}

// Bulk writes (matrix preset ledgers, JE8086's 461 params) must survive the
// grammar intact and in order.
TEST(ParamOverrideLedger, BulkMergeKeepsEveryEntryInOrder)
{
    juce::String ledger;
    for (int i = 0; i < 300; ++i)
        ledger = mergeParamOverride(ledger, i * 7 + 1, static_cast<float>(i % 100) / 100.0f);

    const auto p = parseParamOverrides(ledger);
    ASSERT_EQ(p.size(), 300u);
    for (int i = 0; i < 300; ++i)
    {
        EXPECT_EQ(p[static_cast<size_t>(i)].first, i * 7 + 1) << "entry " << i;
        EXPECT_NEAR(p[static_cast<size_t>(i)].second,
                    static_cast<float>(i % 100) / 100.0f, 1e-6f) << "entry " << i;
    }

    // Re-merging an existing index (the 300th) must not grow the ledger.
    ledger = mergeParamOverride(ledger, 299 * 7 + 1, 0.42f);
    const auto after = parseParamOverrides(ledger);
    ASSERT_EQ(after.size(), 300u);
    EXPECT_EQ(after[299].first, 2094);
    EXPECT_NEAR(after[299].second, 0.42f, 1e-6f);
}
