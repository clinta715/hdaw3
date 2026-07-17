#include "AudioPreviewPlayer.h"

namespace HDAW {

AudioPreviewPlayer::AudioPreviewPlayer(juce::AudioDeviceManager& dm,
                                       juce::AudioFormatManager& fm)
    : deviceManager(dm), formatManager(fm)
{
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

    auto* reader = formatManager.createReaderFor(file);
    if (!reader) return;

    double sampleRate = reader->sampleRate;
    readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    transport.setSource(readerSource.get(), 0, nullptr, sampleRate);
    transport.setGain(volume);
}

void AudioPreviewPlayer::play()
{
    if (!readerSource) return;

    // AudioSourcePlayer bridges AudioSource → AudioIODeviceCallback
    sourcePlayer.setSource(&transport);
    deviceManager.addAudioCallback(&sourcePlayer);
    transport.start();
    playing = true;
}

void AudioPreviewPlayer::stop()
{
    if (playing)
    {
        transport.stop();
        deviceManager.removeAudioCallback(&sourcePlayer);
        sourcePlayer.setSource(nullptr);
        transport.setPosition(0);
    }
    playing = false;
}

void AudioPreviewPlayer::setVolume(float vol)
{
    volume = juce::jlimit(0.0f, 1.0f, vol);
    transport.setGain(volume);
}

void AudioPreviewPlayer::setTempoMatch(bool enabled, double newFileBpm)
{
    tempoMatch = enabled;
    fileBpm = newFileBpm;
}

void AudioPreviewPlayer::setProjectBpm(double bpm)
{
    projectBpm = bpm;
}

double AudioPreviewPlayer::getPlaybackLength() const
{
    return transport.getLengthInSeconds();
}

} // namespace HDAW
