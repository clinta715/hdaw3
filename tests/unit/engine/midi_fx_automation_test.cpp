#include <gtest/gtest.h>
#include "engine/MidiFx.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

using namespace HDAW;

TEST(MidiFxAutomation, ParamDefsLookupArpeggiator)
{
    auto defs = getMidiFxParamDefs("arpeggiator");
    ASSERT_EQ(defs.size(), 5);
    EXPECT_STREQ(defs[0].name, "rate");
    EXPECT_FLOAT_EQ(defs[0].defaultValue, 0.25f);
    EXPECT_FLOAT_EQ(defs[0].minValue, 0.01f);
    EXPECT_FLOAT_EQ(defs[0].maxValue, 2.0f);
}

TEST(MidiFxAutomation, ParamDefsLookupAllTypes)
{
    const char* types[] = {"arpeggiator", "velocity", "chord", "scale",
                           "notelength", "transpose", "keyfilter",
                           "velocitycurve", "notechance", "mididelay",
                           "humanize", "strum"};
    for (auto* type : types)
    {
        auto defs = getMidiFxParamDefs(type);
        EXPECT_GT(defs.size(), 0u) << "Type " << type << " should have params";
    }
}

TEST(MidiFxAutomation, ParamDefsLookupUnknown)
{
    auto defs = getMidiFxParamDefs("nonexistent");
    EXPECT_EQ(defs.size(), 0u);
}

TEST(MidiFxAutomation, SlotParamCacheSetGet)
{
    auto arp = std::make_unique<Arpeggiator>();
    MidiFxSlot slot(std::move(arp), "arpeggiator");

    EXPECT_EQ(slot.getAutomatableParams().size(), 5u);

    slot.setAutomationParam(0, 0.75f);
    EXPECT_FLOAT_EQ(slot.getAutomationParam(0), 0.75f);

    EXPECT_FLOAT_EQ(slot.getAutomationParam(-1), 0.0f);
    EXPECT_FLOAT_EQ(slot.getAutomationParam(99), 0.0f);
}

TEST(MidiFxAutomation, SlotApplyAutomationArpeggiator)
{
    auto arp = std::make_unique<Arpeggiator>();
    auto* arpPtr = arp.get();
    MidiFxSlot slot(std::move(arp), "arpeggiator");

    // rate: denormalized = 0.01 + 0.75 * (2.0 - 0.01) = 1.5025
    slot.setAutomationParam(0, 0.75f);
    slot.applyAutomation();
    EXPECT_NEAR(arpPtr->rate, 1.5025, 0.001);

    // pattern: denormalized = 0 + 0.6 * 5 = 3.0, rounds to 3
    slot.setAutomationParam(1, 0.6f);
    slot.applyAutomation();
    EXPECT_EQ(arpPtr->pattern, 3);
}

TEST(MidiFxAutomation, SlotApplyAutomationTranspose)
{
    auto t = std::make_unique<Transpose>();
    auto* tPtr = t.get();
    MidiFxSlot slot(std::move(t), "transpose");

    // semitones: denormalized = -24 + 0.5 * 48 = 0
    slot.setAutomationParam(0, 0.5f);
    slot.applyAutomation();
    EXPECT_EQ(tPtr->semitones, 0);

    // semitones: denormalized = -24 + 1.0 * 48 = 24
    slot.setAutomationParam(0, 1.0f);
    slot.applyAutomation();
    EXPECT_EQ(tPtr->semitones, 24);
}

TEST(MidiFxAutomation, SlotLoadParamsFromTree)
{
    auto arp = std::make_unique<Arpeggiator>();
    auto* arpPtr = arp.get();
    MidiFxSlot slot(std::move(arp), "arpeggiator");

    juce::ValueTree tree("MIDI_FX_SLOT");
    tree.setProperty("fxType", "arpeggiator", nullptr);
    tree.setProperty("rate", 0.5, nullptr);
    tree.setProperty("pattern", 2.0, nullptr);
    tree.setProperty("octaves", 3.0, nullptr);
    tree.setProperty("gate", 0.8, nullptr);
    tree.setProperty("velocity", 80.0, nullptr);

    slot.loadParamsFromTree(tree);

    // rate: normalized = (0.5 - 0.01) / (2.0 - 0.01) = 0.49 / 1.99 ~ 0.2462
    EXPECT_NEAR(slot.getAutomationParam(0), 0.2462f, 0.01f);
    // pattern: normalized = (2 - 0) / (5 - 0) = 0.4
    EXPECT_NEAR(slot.getAutomationParam(1), 0.4f, 0.001f);
    // octaves: normalized = (3 - 1) / (4 - 1) = 0.6667
    EXPECT_NEAR(slot.getAutomationParam(2), 0.6667f, 0.01f);

    // loadParamsFromTree stores normalized values but does not set dirty flags,
    // so we mark them dirty before applyAutomation to push to the effect.
    for (int i = 0; i < 5; ++i)
        slot.setAutomationParam(i, slot.getAutomationParam(i));
    slot.applyAutomation();
    EXPECT_NEAR(arpPtr->rate, 0.5, 0.01);
    EXPECT_EQ(arpPtr->pattern, 2);
    EXPECT_EQ(arpPtr->octaves, 3);
}

TEST(MidiFxAutomation, SlotDirtyFlagBehavior)
{
    auto arp = std::make_unique<Arpeggiator>();
    auto* arpPtr = arp.get();
    MidiFxSlot slot(std::move(arp), "arpeggiator");

    slot.setAutomationParam(0, 0.9f);
    slot.applyAutomation();
    double rate1 = arpPtr->rate;
    EXPECT_GT(rate1, 0.01);

    // Manually change effect rate, then apply again without setting param
    arpPtr->rate = 0.01;
    slot.applyAutomation();
    EXPECT_DOUBLE_EQ(arpPtr->rate, 0.01);
}

TEST(MidiFxAutomation, SlotParamCountAllTypes)
{
    EXPECT_EQ(getMidiFxParamCount("arpeggiator"), 5);
    EXPECT_EQ(getMidiFxParamCount("velocity"), 1);
    EXPECT_EQ(getMidiFxParamCount("chord"), 1);
    EXPECT_EQ(getMidiFxParamCount("scale"), 2);
    EXPECT_EQ(getMidiFxParamCount("notelength"), 1);
    EXPECT_EQ(getMidiFxParamCount("transpose"), 1);
    EXPECT_EQ(getMidiFxParamCount("keyfilter"), 2);
    EXPECT_EQ(getMidiFxParamCount("velocitycurve"), 2);
    EXPECT_EQ(getMidiFxParamCount("notechance"), 1);
    EXPECT_EQ(getMidiFxParamCount("mididelay"), 3);
    EXPECT_EQ(getMidiFxParamCount("humanize"), 3);
    EXPECT_EQ(getMidiFxParamCount("strum"), 2);
    EXPECT_EQ(getMidiFxParamCount("multinote"), 0);
    EXPECT_EQ(getMidiFxParamCount("nonexistent"), 0);
}
