#include "AudioPreviewPlayer.h"

namespace HDAW {

AudioPreviewPlayer::AudioPreviewPlayer(juce::AudioDeviceManager& dm,
                                       juce::AudioFormatManager& fm)
    : deviceManager(dm), formatManager(fm)
{
    sourcePlayer.setSource(&syncSource);
}

AudioPreviewPlayer::~AudioPreviewPlayer()
{
    stop();
}

void AudioPreviewPlayer::loadFile(const juce::File& file)
{
    stop();
    if (!file.existsAsFile()) return;
    currentFile = file;
    syncSource.loadFile(file, formatManager);
    syncSource.setGain(volume);
    applyPlaybackRate();
}

void AudioPreviewPlayer::play()
{
    if (syncSource.getLengthInSeconds() <= 0.0) return;
    deviceManager.addAudioCallback(&sourcePlayer);
    syncSource.play();
}

void AudioPreviewPlayer::stop()
{
    if (syncSource.isPlaying())
    {
        syncSource.stop();
        deviceManager.removeAudioCallback(&sourcePlayer);
    }
}

void AudioPreviewPlayer::setVolume(float vol)
{
    volume = juce::jlimit(0.0f, 1.0f, vol);
    syncSource.setGain(volume);
}

void AudioPreviewPlayer::setTempoMatch(bool enabled, double newFileBpm)
{
    tempoMatch = enabled;
    if (newFileBpm > 0.0)
        fileBpm = newFileBpm;
    applyPlaybackRate();
}

void AudioPreviewPlayer::setProjectBpm(double bpm)
{
    if (bpm > 0.0)
        projectBpm = bpm;
    applyPlaybackRate();
}

void AudioPreviewPlayer::applyPlaybackRate()
{
    double ratio = 1.0;
    if (tempoMatch && fileBpm > 0.0 && projectBpm > 0.0)
        ratio = fileBpm / projectBpm;
    syncSource.setResamplingRatio(ratio);
}

} // namespace HDAW
