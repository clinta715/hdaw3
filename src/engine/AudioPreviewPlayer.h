#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "SyncableAudioSource.h"

class TransportManager;

namespace HDAW {

class AudioPreviewPlayer
{
public:
    AudioPreviewPlayer(juce::AudioDeviceManager& dm, juce::AudioFormatManager& fm);
    ~AudioPreviewPlayer();

    void loadFile(const juce::File& file);
    void play();
    void stop();
    bool isPlaying() const { return syncSource.isPlaying(); }

    void setVolume(float vol);
    float getVolume() const { return volume; }

    void setTempoMatch(bool enabled, double fileBpm = 0.0);
    bool isTempoMatched() const { return tempoMatch; }

    void setProjectBpm(double bpm);
    double getPlaybackLength() const { return syncSource.getLengthInSeconds(); }

    void setTransportManager(TransportManager* tm) { syncSource.setTransportManager(tm); }

    juce::File getCurrentFile() const { return currentFile; }

private:
    void applyPlaybackRate();

    juce::AudioDeviceManager& deviceManager;
    juce::AudioFormatManager& formatManager;

    juce::File currentFile;
    SyncableAudioSource syncSource;
    juce::AudioSourcePlayer sourcePlayer;

    float volume = 0.8f;
    bool tempoMatch = false;
    double fileBpm = 0.0;
    double projectBpm = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPreviewPlayer)
};

} // namespace HDAW
