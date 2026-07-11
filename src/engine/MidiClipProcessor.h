#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>
#include "TransportManager.h"
#include "../model/ProjectModel.h"

namespace HDAW {

struct NoteData {
    int noteNumber;
    float velocity;
    double startBeat;
    double durationBeats;
};

class MidiClipProcessor : public juce::AudioProcessor
{
public:
    static constexpr int MAX_NOTES = 512;

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
        rebuildNoteCache();
    }

    void rebuildClipCache() { rebuildNoteCache(); }

    juce::ValueTree getClipTree() const { return clipTree; }

    void setStartTime(double t) { startTime.store(t); }
    void setDuration(double d) { duration.store(d); }
    void setGain(float g) { gain.store(g); }
    void setMidiChannel(int ch) { midiChannel.store(juce::jlimit(1, 16, ch)); }
    int  getMidiChannel() const { return midiChannel.load(); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
        std::fill(activeNotes.begin(), activeNotes.end(), false);
        // midiChannel is set externally via setMidiChannel; default is 1.
    }

    void releaseResources() override
    {
        std::fill(activeNotes.begin(), activeNotes.end(), false);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override
    {
        const int numSamples = buffer.getNumSamples();

        buffer.clear();

        int idx = activeCacheIndex.load(std::memory_order_acquire);
        int count = noteCount.load(std::memory_order_acquire);
        if (count <= 0 || numSamples <= 0)
            return;

        int64_t transportSample = transportManager.getCurrentSample();
        double sr = transportManager.getSampleRate();
        double startSec = startTime.load();
        double durSec = duration.load();

        double currentTimeSec = static_cast<double>(transportSample) / sr;
        double clipLocalSec = currentTimeSec - startSec;

        if (clipLocalSec < 0.0 || clipLocalSec > durSec)
        {
            const int channel = midiChannel.load();
            for (int note = 0; note < 128; ++note)
            {
                if (!activeNotes[note]) continue;
                midiMessages.addEvent(juce::MidiMessage::noteOff(channel, note, 0.0f),
                                       juce::jmin(numSamples - 1, 0));
            }
            std::fill(activeNotes.begin(), activeNotes.end(), false);
            return;
        }

        double currentBeat = transportManager.secondsToPpq(currentTimeSec)
                           - transportManager.secondsToPpq(startSec);

        const int channel = midiChannel.load();

        for (int i = 0; i < count; ++i)
        {
            const NoteData& note = noteCaches[idx][i];
            double noteEnd = note.startBeat + note.durationBeats;

            if (currentBeat >= note.startBeat && currentBeat < noteEnd)
            {
                if (!activeNotes[note.noteNumber])
                {
                    float vel = note.velocity * gain.load();
                    vel = (std::max)(0.0f, (std::min)(1.0f, vel));
                    uint8_t velByte = static_cast<uint8_t>(vel * 127.0f);
                    midiMessages.addEvent(juce::MidiMessage::noteOn(channel, note.noteNumber, velByte),
                                          0);
                    activeNotes[note.noteNumber] = true;
                }
            }
            else if (activeNotes[note.noteNumber])
            {
                midiMessages.addEvent(juce::MidiMessage::noteOff(channel, note.noteNumber, 0.0f),
                                      0);
                activeNotes[note.noteNumber] = false;
            }
        }

        float ccVal = gain.load();
        uint8_t ccByte = static_cast<uint8_t>((std::max)(0.0f, (std::min)(1.0f, ccVal)) * 127.0f);
        if (ccByte != lastCcByte)
        {
            midiMessages.addEvent(juce::MidiMessage::controllerEvent(channel, 7, ccByte), 0);
            lastCcByte = ccByte;
        }
    }

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
    void rebuildNoteCache()
    {
        int inactiveIdx = 1 - activeCacheIndex.load(std::memory_order_relaxed);
        auto& inactive = noteCaches[inactiveIdx];

        auto nl = clipTree.getChildWithName(IDs::MIDI_NOTE_LIST);
        if (!nl.isValid())
        {
            noteCount.store(0, std::memory_order_release);
            activeCacheIndex.store(inactiveIdx, std::memory_order_release);
            return;
        }

        int count = (std::min)(nl.getNumChildren(), MAX_NOTES);
        for (int i = 0; i < count; ++i)
        {
            auto n = nl.getChild(i);
            inactive[i].noteNumber = n.getProperty(IDs::noteNumber);
            inactive[i].velocity = n.getProperty(IDs::velocity);
            inactive[i].startBeat = n.getProperty(IDs::startBeat);
            inactive[i].durationBeats = n.getProperty(IDs::durationBeats);
        }

        noteCount.store(count, std::memory_order_release);
        activeCacheIndex.store(inactiveIdx, std::memory_order_release);
    }

    HDAW::TransportManager& transportManager;
    juce::ValueTree clipTree;

    std::atomic<double> startTime{ 0.0 };
    std::atomic<double> duration{ 1.0 };
    std::atomic<float> gain{ 1.0f };

    std::atomic<int> midiChannel{ 1 }; // 1-16 = specific MIDI channel
    uint8_t lastCcByte = 255;
    std::array<bool, 128> activeNotes{};

    std::array<std::array<NoteData, MAX_NOTES>, 2> noteCaches{};
    std::atomic<int> activeCacheIndex{0};
    std::atomic<int> noteCount{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiClipProcessor)
};

} // namespace HDAW
