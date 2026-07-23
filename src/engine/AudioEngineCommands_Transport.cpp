#include "AudioEngineCommands.h"
#include "AudioEngine.h"
#include "../model/ProjectModel.h"

// ─── ProjectCommands — Transport properties ───────────────────────

void AudioEngineCommands::setTempo(double bpm)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    engine_.getProjectModel().getTree().setProperty(IDs::tempo, bpm, &um);
}

int AudioEngineCommands::addTempoPoint(double timeSeconds, double bpm)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto tempoList = model.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid())
    {
        tempoList = juce::ValueTree(IDs::TEMPO_POINT_LIST);
        model.getTree().addChild(tempoList, -1, &um);
    }
    juce::ValueTree pt(IDs::TEMPO_POINT);
    pt.setProperty(IDs::startTime, timeSeconds, &um);
    pt.setProperty(IDs::tempo, bpm, &um);
    int idx = tempoList.getNumChildren();
    tempoList.addChild(pt, -1, &um);
    return idx;
}

void AudioEngineCommands::removeTempoPoint(int index)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto tempoList = model.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid()) return;
    if (index < 0 || index >= tempoList.getNumChildren()) return;
    tempoList.removeChild(index, &um);
}

void AudioEngineCommands::setTempoPointBpm(int index, double bpm)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto tempoList = model.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid()) return;
    if (index < 0 || index >= tempoList.getNumChildren()) return;
    tempoList.getChild(index).setProperty(IDs::tempo, bpm, &um);
}

void AudioEngineCommands::setTempoPointTime(int index, double timeSeconds)
{
    auto& model = engine_.getProjectModel();
    auto& um = model.getUndoManager();
    auto tempoList = model.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid()) return;
    if (index < 0 || index >= tempoList.getNumChildren()) return;
    tempoList.getChild(index).setProperty(IDs::startTime, timeSeconds, &um);
}

void AudioEngineCommands::setLoopStart(double beat)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::loopStart, beat, &um);
}

void AudioEngineCommands::setLoopEnd(double beat)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::loopEnd, beat, &um);
}

void AudioEngineCommands::setLooping(bool looping)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::isLooping, looping, &um);
}

void AudioEngineCommands::setMetronomeEnabled(bool enabled)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::metronomeEnabled, enabled, &um);
}

void AudioEngineCommands::setTimeSignature(int numerator, int denominator)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
    {
        transport.setProperty(IDs::timeSigNumerator, numerator, &um);
        transport.setProperty(IDs::timeSigDenominator, denominator, &um);
    }
}

// ─── TransportCommands ────────────────────────────────────────────

void AudioEngineCommands::play()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::isPlaying, true, &um);
}

void AudioEngineCommands::stop()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
    {
        transport.setProperty(IDs::isPlaying, false, &um);
        transport.setProperty(IDs::position, 0.0, &um);
    }
}

void AudioEngineCommands::pause()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::isPlaying, false, &um);
}

void AudioEngineCommands::rewind()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::position, 0.0, &um);
}

void AudioEngineCommands::toggleLoop()
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
    {
        bool current = transport.getProperty(IDs::isLooping, false);
        transport.setProperty(IDs::isLooping, !current, &um);
    }
}

void AudioEngineCommands::seekToSample(int64_t sample)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
    {
        double sr = engine_.getTransportManager().getSampleRate();
        if (sr <= 0.0) return;
        transport.setProperty(IDs::position, static_cast<double>(sample) / sr, &um);
    }
}

void AudioEngineCommands::seekToSeconds(double seconds)
{
    auto& um = engine_.getProjectModel().getUndoManager();
    auto transport = engine_.getProjectModel().getTransportTree();
    if (transport.isValid())
        transport.setProperty(IDs::position, seconds, &um);
}

void AudioEngineCommands::startRecording()
{
    if (auto* proc = engine_.getMainProcessor())
        proc->startRecording();
}

void AudioEngineCommands::stopRecording()
{
    if (auto* proc = engine_.getMainProcessor())
        proc->stopRecording();
}

bool AudioEngineCommands::isRecording() const
{
    if (auto* proc = engine_.getMainProcessor())
        return proc->isRecording();
    return false;
}
