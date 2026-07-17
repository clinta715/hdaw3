#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

namespace HDAW {

/**
 * Lightweight audio file preview player with volume control.
 * Uses the application's AudioDeviceManager for output.
 */
class AudioPreviewPlayer
{
public:
    AudioPreviewPlayer(juce::AudioDeviceManager& dm, juce::AudioFormatManager& fm);
    ~AudioPreviewPlayer();

    void loadFile(const juce::File& file);
    void play();
    void stop();
    bool isPlaying() const { return playing; }

    void setVolume(float vol);
    float getVolume() const { return volume; }

    void setTempoMatch(bool enabled, double fileBpm = 0.0);
    bool isTempoMatched() const { return tempoMatch; }

    void setProjectBpm(double bpm);
    double getPlaybackLength() const;

    juce::File getCurrentFile() const { return currentFile; }

private:
    juce::AudioDeviceManager& deviceManager;
    juce::AudioFormatManager& formatManager;

    juce::File currentFile;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::AudioSourcePlayer sourcePlayer;  // bridges AudioSource → AudioIODeviceCallback

    float volume = 0.8f;
    bool tempoMatch = false;
    double fileBpm = 0.0;
    double projectBpm = 120.0;
    bool playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPreviewPlayer)
};

} // namespace HDAW
