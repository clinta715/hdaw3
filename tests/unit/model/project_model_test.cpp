#include "gtest/gtest.h"
#include "../../../src/model/ProjectModel.h"

TEST(ProjectModelIDs, GainEnvelopeIDsExist)
{
    // IDs should be defined in IDs namespace
    EXPECT_TRUE(juce::Identifier(IDs::GAIN_ENVELOPE).isValid());
    EXPECT_TRUE(juce::Identifier(IDs::GAIN_ENVELOPE_POINT).isValid());
    EXPECT_TRUE(juce::Identifier(IDs::pointTime).isValid());
    EXPECT_TRUE(juce::Identifier(IDs::pointGain).isValid());
}