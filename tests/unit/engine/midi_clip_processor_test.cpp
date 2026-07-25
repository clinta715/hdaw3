#include <gtest/gtest.h>
#include "engine/MidiClipProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace {

juce::ValueTree makeCcOnlyClip(int controllerNumber, double beat, int value)
{
    juce::ValueTree clip(IDs::CLIP);
    clip.setProperty(IDs::clipType, "midi", nullptr);
    auto ccList = juce::ValueTree(IDs::CC_LIST);
    juce::ValueTree pt(IDs::CC_POINT);
    pt.setProperty(IDs::controllerNumber, controllerNumber, nullptr);
    pt.setProperty(IDs::beat, beat, nullptr);
    pt.setProperty(IDs::value, value, nullptr);
    ccList.addChild(pt, -1, nullptr);
    clip.addChild(ccList, -1, nullptr);
    return clip;
}

bool hasController(juce::MidiBuffer& midi, int controllerNumber, int value)
{
    for (const auto& meta : midi)
    {
        const auto& msg = meta.getMessage();
        if (msg.isController()
            && msg.getControllerNumber() == controllerNumber
            && msg.getControllerValue() == value)
            return true;
    }
    return false;
}

} // namespace

TEST(MidiClipProcessor, CcPlaybackEmitsAtBeat)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    proc.setClipTree(makeCcOnlyClip(74, 2.0, 100));
    proc.setStartTime(0.0);
    proc.setDuration(8.0);
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);

    tm.setCurrentSample(0);
    juce::MidiBuffer early;
    proc.processBlock(buffer, early);
    EXPECT_FALSE(hasController(early, 74, 100));

    tm.setCurrentSample(44100);
    juce::MidiBuffer atBeat;
    proc.processBlock(buffer, atBeat);
    EXPECT_TRUE(hasController(atBeat, 74, 100));
}

TEST(MidiClipProcessor, CcPlaybackEmitsOnlyOnce)
{
    HDAW::TransportManager tm;
    tm.setSampleRate(44100.0);
    tm.setBPM(120.0);

    HDAW::MidiClipProcessor proc(tm);
    proc.setClipTree(makeCcOnlyClip(74, 1.0, 90));
    proc.setStartTime(0.0);
    proc.setDuration(8.0);
    proc.prepareToPlay(44100.0, 512);

    juce::AudioBuffer<float> buffer(2, 512);

    tm.setCurrentSample(22050);
    juce::MidiBuffer first;
    proc.processBlock(buffer, first);
    EXPECT_TRUE(hasController(first, 74, 90));

    tm.setCurrentSample(22050 + 512);
    juce::MidiBuffer second;
    proc.processBlock(buffer, second);
    EXPECT_FALSE(hasController(second, 74, 90));
}
