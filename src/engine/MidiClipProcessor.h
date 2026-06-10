#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include "TransportManager.h"
#include "../model/ProjectModel.h"

namespace HDAW {

class MidiClipProcessor : public juce::AudioProcessor
{
public:
    MidiClipProcessor(HDAW::TransportManager& tm)
        : AudioProcessor(BusesProperties()
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          transportManager(tm)
    {
    }

    ~MidiClipProcessor() override = default;

    void setClipTree(juce::ValueTree tree)
    {
        clipTree = tree;
        noteList = tree.getChildWithName(IDs::MIDI_NOTE_LIST);
    }

    juce::ValueTree getClipTree() const { return clipTree; }

    void setStartTime(double t) { startTime.store(t); }
    void setDuration(double d) { duration.store(d); }
    void setGain(float g) { gain.store(g); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
        std::fill(activeNotes.begin(), activeNotes.end(), false);
        midiChannel = 1;
    }

    void releaseResources() override
    {
        std::fill(activeNotes.begin(), activeNotes.end(), false);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
    {
        const int numSamples = buffer.getNumSamples();

        // Clear audio output
        buffer.clear();

        if (!noteList.isValid() || numSamples <= 0)
            return;

        // Calculate current beat position
        int64_t transportSample = transportManager.getCurrentSample();
        double sr = transportManager.getSampleRate();
        double bpm = transportManager.getBPM();
        double startSec = startTime.load();
        double durSec = duration.load();

        double currentTimeSec = static_cast<double>(transportSample) / sr;
        double clipLocalSec = currentTimeSec - startSec;

        // Clip bounds
        if (clipLocalSec < 0.0 || clipLocalSec > durSec)
        {
            // Send note-offs for all active notes
            for (int note = 0; note < 128; ++note)
            {
                if (!activeNotes[note]) continue;
                midiMessages.addEvent(juce::MidiMessage::noteOff(midiChannel, note, 0.0f),
                                      juce::jmin(numSamples - 1, 0));
            }
            std::fill(activeNotes.begin(), activeNotes.end(), false);
            return;
        }

        double secondsPerBeat = 60.0 / bpm;
        double currentBeat = clipLocalSec / secondsPerBeat;

        // Iterate MIDI notes in the clip
        for (int i = 0; i < noteList.getNumChildren(); ++i)
        {
            auto note = noteList.getChild(i);
            int noteNumber = note.getProperty(IDs::noteNumber);
            float velocity = note.getProperty(IDs::velocity);
            double noteStart = note.getProperty(IDs::startBeat);
            double noteDur = note.getProperty(IDs::durationBeats);
            double noteEnd = noteStart + noteDur;

            if (currentBeat >= noteStart && currentBeat < noteEnd)
            {
                if (!activeNotes[noteNumber])
                {
                    float vel = velocity * gain.load();
                    vel = std::max(0.0f, std::min(1.0f, vel));
                    uint8_t velByte = static_cast<uint8_t>(vel * 127.0f);
                    midiMessages.addEvent(juce::MidiMessage::noteOn(midiChannel, noteNumber, velByte),
                                          0);
                    activeNotes[noteNumber] = true;
                }
            }
            else if (activeNotes[noteNumber])
            {
                uint8_t velByte = static_cast<uint8_t>(gain.load() * 127.0f);
                midiMessages.addEvent(juce::MidiMessage::noteOff(midiChannel, noteNumber, 0.0f),
                                      0);
                activeNotes[noteNumber] = false;
            }
        }

        // Apply CC7 for clip gain
        float ccVal = gain.load();
        uint8_t ccByte = static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, ccVal)) * 127.0f);
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(midiChannel, 7, ccByte), 0);
    }

    // AudioProcessor boilerplate
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "MidiClip"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    HDAW::TransportManager& transportManager;
    juce::ValueTree clipTree;
    juce::ValueTree noteList;

    std::atomic<double> startTime{ 0.0 };
    std::atomic<double> duration{ 1.0 };
    std::atomic<float> gain{ 1.0f };

    int midiChannel = 1;
    std::array<bool, 128> activeNotes{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiClipProcessor)
};

} // namespace HDAW
