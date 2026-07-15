#include <gtest/gtest.h>
#include "engine/ClipSourceProcessor.h"
#include "model/ProjectModel.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>

TEST(ClipSourceProcessor, GainEnvelopeInterpolation)
{
    ProjectModel model;
    auto clip = ProjectModel::createAudioClip("Test", 0.0, 4.0, "dummy.wav");
    auto envelope = ProjectModel::ensureGainEnvelope(clip);
    ProjectModel::addGainEnvelopePoint(envelope, 0.0, 1.0, nullptr);
    ProjectModel::addGainEnvelopePoint(envelope, 2.0, 0.5, nullptr);
    ProjectModel::addGainEnvelopePoint(envelope, 4.0, 1.0, nullptr);
    
    // Create processor and set envelope
    HDAW::TransportManager tm;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::ClipSourceProcessor proc(tm, fm);
    proc.setSourceFile("dummy.wav");
    proc.prepareToPlay(44100.0, 512);
    
    // Use public API to inject envelope points
    std::vector<HDAW::ClipSourceProcessor::GainPoint> points = {
        {0.0, 1.0}, {2.0, 0.5}, {4.0, 1.0}
    };
    proc.setGainEnvelopePoints(points);
    
    // Test interpolation at various times
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(0.0), 1.0);
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(1.0), 0.75);  // linear between 1.0 and 0.5
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(2.0), 0.5);
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(3.0), 0.75);  // linear between 0.5 and 1.0
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(4.0), 1.0);
    EXPECT_DOUBLE_EQ(proc.getGainAtTime(5.0), 1.0);   // hold last value
}