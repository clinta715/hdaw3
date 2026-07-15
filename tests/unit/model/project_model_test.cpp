#include "gtest/gtest.h"
#include "../../../src/model/ProjectModel.h"

using namespace HDAW;

TEST(ProjectModelIDs, GainEnvelopeIDsExist)
{
    // IDs should be defined in IDs namespace
    EXPECT_TRUE(juce::Identifier(IDs::GAIN_ENVELOPE).isValid());
    EXPECT_TRUE(juce::Identifier(IDs::GAIN_ENVELOPE_POINT).isValid());
    EXPECT_TRUE(juce::Identifier(IDs::pointTime).isValid());
    EXPECT_TRUE(juce::Identifier(IDs::pointGain).isValid());
}

TEST(ProjectModel, CreateGainEnvelopeForClip)
{
    ProjectModel model;
    auto clip = ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    
    auto envelope = ProjectModel::ensureGainEnvelope(clip);
    EXPECT_TRUE(envelope.isValid());
    EXPECT_TRUE(envelope.hasType(IDs::GAIN_ENVELOPE));
    EXPECT_EQ(clip.getChildWithName(IDs::GAIN_ENVELOPE), envelope);
}

TEST(ProjectModel, AddGainEnvelopePoint)
{
    ProjectModel model;
    auto clip = ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    auto envelope = ProjectModel::ensureGainEnvelope(clip);
    
    auto point = ProjectModel::addGainEnvelopePoint(envelope, 1.0, 0.5, nullptr);
    EXPECT_TRUE(point.isValid());
    EXPECT_DOUBLE_EQ(static_cast<double>(point.getProperty(IDs::pointTime)), 1.0);
    EXPECT_DOUBLE_EQ(static_cast<double>(point.getProperty(IDs::pointGain)), 0.5);
}

TEST(ProjectModel, GetGainEnvelopePoints)
{
    ProjectModel model;
    auto clip = ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    auto envelope = ProjectModel::ensureGainEnvelope(clip);
    
    ProjectModel::addGainEnvelopePoint(envelope, 0.0, 1.0, nullptr);
    ProjectModel::addGainEnvelopePoint(envelope, 2.0, 0.5, nullptr);
    ProjectModel::addGainEnvelopePoint(envelope, 4.0, 1.0, nullptr);
    
    auto points = ProjectModel::getGainEnvelopePoints(envelope);
    EXPECT_EQ(points.size(), 3u);
    EXPECT_DOUBLE_EQ(points[0].time, 0.0);
    EXPECT_DOUBLE_EQ(points[0].gain, 1.0);
}