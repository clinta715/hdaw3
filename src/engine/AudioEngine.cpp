#include "AudioEngine.h"
#include <juce_events/juce_events.h>
#include <QSettings>
#include <cmath>
#include "../common/SettingsKeys.h"
#include <algorithm>

namespace {
// Resolve the track index owning a MODULATION or MODULATION_LIST subtree.
// `tree` may be the MODULATION node itself (parent = MODULATION_LIST) or the
// MODULATION_LIST (parent = TRACK). Returns -1 if the track can't be found.
int modulationTrackIndexOf(const juce::ValueTree& tree)
{
    if (!tree.isValid()) return -1;
    auto parent = tree.getParent();
    if (!parent.isValid()) return -1;

    // MODULATION → MODULATION_LIST → TRACK
    juce::ValueTree modList;
    if (tree.hasType(IDs::MODULATION))
        modList = parent;
    else if (tree.hasType(IDs::MODULATION_LIST))
        modList = tree;
    else
        return -1;

    auto trackTree = modList.getParent();
    if (!trackTree.isValid() || !trackTree.hasType(IDs::TRACK)) return -1;

    auto trackList = trackTree.getParent();
    if (!trackList.isValid()) return -1;
    int idx = trackList.indexOf(trackTree);
    return idx;
}
} // namespace

AudioEngine::AudioEngine()
    : sessionManager(transportManager, projectModel)
{
    mainProcessor = std::make_unique<MainAudioProcessor>();
    projectModel.getTree().addListener(this);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}
void AudioEngine::initialize()
{
    // Link bridge, project model, and format manager to processor
    mainProcessor->setBridge(&spscBridge);
    mainProcessor->setProjectModel(&projectModel);
    mainProcessor->setFormatManager(projectPool.getFormatManager());
    mainProcessor->setPluginManager(&pluginManager);
    mainProcessor->setStretchCache(&stretchCache);

    // When a background stretch render completes, swap the stretched buffer
    // into the playing clip via a routing graph rebuild. The signal is emitted
    // on the message thread (StretchCache hops internally), so this slot is
    // safe to call rebuildRoutingGraph directly. Use StretchCache (a QObject)
    // as the connection context so Qt cleans up the connection when the cache
    // is destroyed; mainProcessor is owned by AudioEngine and outlives it.
    QObject::connect(&stretchCache, &HDAW::StretchCache::entryReady,
                     &stretchCache, [this](int)
    {
        if (mainProcessor)
            mainProcessor->rebuildRoutingGraph();
    });

    // Sync initial modulation state from the project model
    auto trackList = projectModel.getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto modList = trackList.getChild(t).getChildWithName(IDs::MODULATION_LIST);
        if (modList.isValid() && mainProcessor)
            mainProcessor->rebuildModulation(t);
    }

    // Setup transport
    transportManager.setBPM(projectModel.getTree().getProperty(IDs::tempo));
    rebuildTempoMap();

    // Sync loop state from ValueTree to TransportManager atomics.
    // The listener only fires on property changes, so we must push the
    // initial values here during setup to ensure advance() has valid
    // loopStartSample / loopEndSample from the start.
    {
        auto transportTree = projectModel.getTransportTree();
        transportManager.setLooping(transportTree.getProperty(IDs::isLooping));
        double sr = transportManager.getSampleRate();
        transportManager.setLoopStartSample(
            static_cast<int64_t>(static_cast<double>(transportTree.getProperty(IDs::loopStart)) * sr));
        transportManager.setLoopEndSample(
            static_cast<int64_t>(static_cast<double>(transportTree.getProperty(IDs::loopEnd)) * sr));
    }

    {
        auto transportTree = projectModel.getTransportTree();
        int tsNum = transportTree.getProperty(IDs::timeSigNumerator, 4);
        mainProcessor->getMetronome().setBeatsPerBar(tsNum > 0 ? tsNum : 4);
    }

    mainProcessor->setTransportManager(&transportManager);

    // Initialize plugin manager — load cache (scan happens asynchronously after UI starts)
    pluginManager.loadCache();

    // Initialize default audio device (2 in, 2 out) as fallback
    auto error = deviceManager.initialiseWithDefaultDevices(2, 2);
    if (error.isNotEmpty())
        juce::Logger::writeToLog("AudioEngine::initialize Error: " + error);

    // Restore saved audio device preferences if available
    {
        QSettings s;
        QString savedDriver = s.value(SettingsKeys::kKeyAudioDriver).toString();
        QString savedOutput = s.value(SettingsKeys::kKeyAudioOutputDevice).toString();
        QString savedInput  = s.value(SettingsKeys::kKeyAudioInputDevice).toString();
        int savedRate       = s.value(SettingsKeys::kKeyAudioSampleRate, 0).toInt();
        int savedBuffer     = s.value(SettingsKeys::kKeyAudioBufferSize, 0).toInt();

        if (!savedDriver.isEmpty() || !savedOutput.isEmpty())
        {
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            setup = deviceManager.getAudioDeviceSetup();

            if (!savedDriver.isEmpty())
                deviceManager.setCurrentAudioDeviceType(
                    juce::String(savedDriver.toUtf8().constData()), true);

            if (!savedOutput.isEmpty())
                setup.outputDeviceName = juce::String(savedOutput.toUtf8().constData());
            if (!savedInput.isEmpty())
                setup.inputDeviceName = juce::String(savedInput.toUtf8().constData());
            if (savedRate > 0)
                setup.sampleRate = savedRate;
            if (savedBuffer > 0)
                setup.bufferSize = savedBuffer;

            auto err = deviceManager.setAudioDeviceSetup(setup, true);
            if (err.isNotEmpty())
            {
                juce::Logger::writeToLog("AudioEngine: saved device restore failed: " + err
                    + " — using defaults");
                deviceManager.initialiseWithDefaultDevices(2, 2);
            }
        }
    }

    // Connect processor to player (triggers prepareToPlay → RoutingManager rebuild)
    processorPlayer.setProcessor(mainProcessor.get());

    // Add player as audio callback
    deviceManager.addAudioCallback(&processorPlayer);

    // Initialize preview player for file browser audio preview
    previewPlayer = std::make_unique<HDAW::AudioPreviewPlayer>(
        deviceManager, projectPool.getFormatManager());
    previewPlayer->setTransportManager(&transportManager);

    // Wire MIDI input to processor
    midiInputManager.setNoteCallback([this](const juce::MidiMessage& msg) {
        // If CC recording is armed and the transport is playing, capture
        // controller events and dispatch them to the main thread. The audio
        // thread is never allowed to touch the ValueTree, so we route through
        // the message manager.
        if (msg.isController() && midiCcRecordArmed
            && transportManager.isPlayingNow() && midiCcCallback)
        {
            int channel = msg.getChannel();
            int controller = msg.getControllerNumber();
            int value = msg.getControllerValue();
            juce::MessageManager::callAsync([this, channel, controller, value]() {
                if (midiCcCallback)
                    midiCcCallback(channel, controller, value);
            });
        }
        if ((msg.isNoteOn() || msg.isNoteOff()) && midiNoteRecordArmed
            && transportManager.isPlayingNow())
        {
            int channel = msg.getChannel();
            int note = msg.getNoteNumber();
            int vel = msg.getVelocity();
            bool noteOn = msg.isNoteOn();
            int64_t sample = transportManager.getCurrentSample();
            juce::MessageManager::callAsync([this, channel, note, vel, noteOn, sample]() {
                recordMidiNoteEvent(channel, note, vel, noteOn, sample);
            });
        }
        mainProcessor->addExternalMidiMessage(msg);
    });

    commands = std::make_unique<AudioEngineCommands>(*this);
    readModel = std::make_unique<ReadModelImpl>(projectModel);
    static_cast<ReadModelImpl*>(readModel.get())->setEngine(this);

    pluginService = std::make_unique<PluginServiceImpl>(pluginManager);
    paramService = std::make_unique<PluginParamServiceImpl>(*mainProcessor);
    midiService = std::make_unique<MidiServiceImpl>(midiInputManager);

    // Wiring that previously lived in MainWindow
    projectModel.setPluginManager(&pluginManager);

    // Poll for audio-thread auto-stop requests (position exceeded project end).
    // The audio thread sets an atomic flag; this timer fires the proper
    // ValueTree stop command on the message thread so the UI updates.
    startTimer(50);
}

void AudioEngine::recordMidiCc(int channel, int controllerNumber, int value)
{
    if (!transportManager.isPlayingNow() || !commands)
        return;

    const double globalBeat = transportManager.samplesToPpq(transportManager.getCurrentSample());

    auto trackList = projectModel.getTrackListTree();
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto track = trackList.getChild(t);
        if (static_cast<int>(track.getProperty(IDs::isArm, 0)) == 0)
            continue;
        if (static_cast<int>(track.getProperty(IDs::midiChannel, 1)) != channel)
            continue;

        auto clipList = track.getChildWithName(IDs::CLIP_LIST);
        if (!clipList.isValid()) continue;
        for (int c = 0; c < clipList.getNumChildren(); ++c)
        {
            auto clip = clipList.getChild(c);
            if (clip.getProperty(IDs::clipType).toString() != "midi")
                continue;
            double startSec = static_cast<double>(clip.getProperty(IDs::startTime, 0.0));
            double durSec = static_cast<double>(clip.getProperty(IDs::duration, 0.0));
            double clipStartBeat = transportManager.secondsToPpq(startSec);
            double clipEndBeat = transportManager.secondsToPpq(startSec + durSec);
            if (globalBeat >= clipStartBeat && globalBeat < clipEndBeat)
            {
                int clipId = static_cast<int>(clip.getProperty(IDs::clipID, 0));
                commands->addCcPoint(clipId, controllerNumber, globalBeat - clipStartBeat, value);
                return;
            }
        }
    }
}

void AudioEngine::setMidiNoteRecordArmed(bool armed)
{
    bool wasArmed = midiNoteRecordArmed.exchange(armed);
    if (armed)
    {
        midiNoteRecClips.clear();
        midiPendingNotes.clear();
        return;
    }
    if (wasArmed)
    {
        flushAllPendingMidiNotes(transportManager.getCurrentSample());
        finalizeMidiRecClips();
        midiNoteRecClips.clear();
        midiPendingNotes.clear();
    }
}

void AudioEngine::recordMidiNoteEvent(int channel, int noteNumber, int velocity, bool isNoteOn, int64_t sample)
{
    if (!commands) return;

    auto trackList = projectModel.getTrackListTree();
    int trackIndex = -1;
    for (int t = 0; t < trackList.getNumChildren(); ++t)
    {
        auto track = trackList.getChild(t);
        if (static_cast<int>(track.getProperty(IDs::isArm, 0)) == 0) continue;
        if (static_cast<int>(track.getProperty(IDs::midiChannel, 1)) != channel) continue;
        trackIndex = t;
        break;
    }
    if (trackIndex < 0) return;

    auto& pending = midiPendingNotes[trackIndex];
    if (isNoteOn && velocity > 0)
    {
        if (pending.find(noteNumber) != pending.end())
            flushPendingMidiNote(trackIndex, noteNumber, sample);
        pending[noteNumber] = { sample, velocity };
    }
    else
    {
        flushPendingMidiNote(trackIndex, noteNumber, sample);
    }
}

void AudioEngine::flushPendingMidiNote(int trackIndex, int noteNumber, int64_t endSample)
{
    auto tit = midiPendingNotes.find(trackIndex);
    if (tit == midiPendingNotes.end()) return;
    auto it = tit->second.find(noteNumber);
    if (it == tit->second.end()) return;

    int64_t startSample = it->second.first;
    int velocity = it->second.second;
    tit->second.erase(it);

    int clipId = ensureMidiRecClip(trackIndex, startSample);
    if (clipId < 0) return;

    int64_t clipStartSample = startSample;
    for (const auto& rc : midiNoteRecClips)
        if (rc.trackIndex == trackIndex) { clipStartSample = rc.startSample; break; }

    double localStartBeat = transportManager.samplesToPpq(startSample)
                          - transportManager.samplesToPpq(clipStartSample);
    double durationBeats = transportManager.samplesToPpq(endSample)
                         - transportManager.samplesToPpq(startSample);
    if (durationBeats < 1e-6) durationBeats = 0.25;

    commands->addNote(clipId, noteNumber, velocity, localStartBeat, durationBeats);

    for (auto& rc : midiNoteRecClips)
        if (rc.trackIndex == trackIndex && endSample > rc.maxEndSample)
            rc.maxEndSample = endSample;
}

void AudioEngine::flushAllPendingMidiNotes(int64_t endSample)
{
    for (auto& tkv : midiPendingNotes)
    {
        std::vector<int> pitches;
        for (const auto& pkv : tkv.second) pitches.push_back(pkv.first);
        for (int p : pitches) flushPendingMidiNote(tkv.first, p, endSample);
    }
}

int AudioEngine::ensureMidiRecClip(int trackIndex, int64_t startSample)
{
    for (const auto& rc : midiNoteRecClips)
        if (rc.trackIndex == trackIndex) return rc.clipId;

    double sr = transportManager.getSampleRate();
    double startSec = sr > 0 ? static_cast<double>(startSample) / sr : 0.0;
    // addMidiClip expects beats; convert seconds → beats
    double bpm = transportManager.getBPM();
    double startBeat = (bpm > 0) ? startSec * bpm / 60.0 : startSec;
    double durBeats = (bpm > 0) ? 8.0 * bpm / 60.0 : 8.0;
    int clipId = commands->addMidiClip(trackIndex, startBeat, durBeats, "Recording");
    if (clipId < 0) return -1;
    midiNoteRecClips.push_back({ clipId, trackIndex, startSample, startSample });
    return clipId;
}

void AudioEngine::finalizeMidiRecClips()
{
    double sr = transportManager.getSampleRate();
    if (sr <= 0) return;
    auto trackList = projectModel.getTrackListTree();
    auto& um = projectModel.getUndoManager();
    for (const auto& rc : midiNoteRecClips)
    {
        double durSec = static_cast<double>(rc.maxEndSample - rc.startSample) / sr;
        if (durSec < 0.5) durSec = 0.5;
        for (int t = 0; t < trackList.getNumChildren(); ++t)
        {
            auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                auto clip = clipList.getChild(c);
                if (static_cast<int>(clip.getProperty(IDs::clipID, 0)) == rc.clipId)
                    clip.setProperty(IDs::duration, durSec, &um);
            }
        }
    }
}

void AudioEngine::shutdown()
{
    projectModel.getTree().removeListener(this);
    cancelPendingUpdate(); // no deferred rebuild fires after teardown
    deviceManager.removeAudioCallback(&processorPlayer);
    processorPlayer.setProcessor(nullptr);
}

float AudioEngine::getTrackVolume(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::volume);
    return 0.0f;
}

void AudioEngine::setTrackVolume(int trackIndex, float volume)
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
    {
        trackList.getChild(trackIndex).setProperty(IDs::volume, static_cast<double>(volume), &projectModel.getUndoManager());
    }
}

float AudioEngine::getTrackPan(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::pan);
    return 0.0f;
}

void AudioEngine::setTrackPan(int trackIndex, float pan)
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
    {
        trackList.getChild(trackIndex).setProperty(IDs::pan, static_cast<double>(pan), &projectModel.getUndoManager());
    }
}

bool AudioEngine::isTrackMuted(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::isMuted);
    return false;
}

void AudioEngine::setTrackMuted(int trackIndex, bool muted)
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
    {
        trackList.getChild(trackIndex).setProperty(IDs::isMuted, muted, &projectModel.getUndoManager());
        ParamUpdate update{ trackIndex, 3, muted ? 1.0f : 0.0f };
        spscBridge.pushUpdate(update);
    }
}

bool AudioEngine::isTrackArmed(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::isArm);
    return false;
}

void AudioEngine::setTrackArmed(int trackIndex, bool armed)
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        trackList.getChild(trackIndex).setProperty(IDs::isArm, armed, &projectModel.getUndoManager());
}

juce::String AudioEngine::getTrackName(int trackIndex) const
{
    auto trackList = projectModel.getTrackListTree();
    if (trackIndex >= 0 && trackIndex < trackList.getNumChildren())
        return trackList.getChild(trackIndex).getProperty(IDs::name).toString();
    return {};
}

std::vector<AudioEngine::FxProgramListEntry> AudioEngine::getFxProgramList(int trackIndex, int slotIndex) const
{
    std::vector<FxProgramListEntry> result;
    if (mainProcessor == nullptr)
        return result;
    auto* track = mainProcessor->getTrack(trackIndex);
    if (track == nullptr)
        return result;
    auto& chain = track->getFXChain();
    if (slotIndex < 0 || slotIndex >= static_cast<int>(chain.size()) || chain[slotIndex] == nullptr)
        return result;
    auto* slot = chain[slotIndex].get();
    if (!slot->isPlugin())
        return result;
    int num = slot->getNumPrograms();
    result.reserve(num > 0 ? static_cast<size_t>(num) : 0);
    for (int i = 0; i < num; ++i)
        result.push_back({ i, slot->getProgramName(i).toStdString() });
    return result;
}

AudioEngine::WavePeaks AudioEngine::getWaveformPeaks(int clipId, int numBins)
{
    WavePeaks result;

    auto trackList = projectModel.getTrackListTree();
    juce::ValueTree clip;
    for (int i = 0; i < trackList.getNumChildren(); ++i)
    {
        auto list = trackList.getChild(i).getChildWithName(IDs::CLIP_LIST);
        for (int j = 0; j < list.getNumChildren(); ++j)
        {
            if (static_cast<int>(list.getChild(j).getProperty(IDs::clipID)) == clipId)
            {
                clip = list.getChild(j);
                break;
            }
        }
        if (clip.isValid()) break;
    }
    if (!clip.isValid())
    {
        result.error = "clip not found";
        result.errorCode = -32602;
        return result;
    }
    if (clip.getProperty(IDs::clipType).toString() != juce::String("audio"))
    {
        result.error = "not an audio clip";
        result.errorCode = -32602;
        return result;
    }
    auto sourceFile = clip.getProperty(IDs::sourceFile).toString();
    if (sourceFile.isEmpty())
    {
        result.error = "no source file";
        result.errorCode = -32602;
        return result;
    }
    auto file = juce::File(sourceFile);
    if (!file.existsAsFile())
    {
        result.error = "source file missing";
        result.errorCode = -32602;
        return result;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(projectPool.getFormatManager().createReaderFor(file));
    if (!reader)
    {
        result.error = "cannot open audio file";
        result.errorCode = -32602;
        return result;
    }
    auto totalSamples = reader->lengthInSamples;
    if (totalSamples <= 0)
    {
        result.error = "empty audio";
        result.errorCode = -32602;
        return result;
    }

    int numChannels = static_cast<int>(reader->numChannels);
    result.sampleRate = reader->sampleRate;
    result.numSamples = totalSamples;
    numBins = std::clamp(numBins, 100, 10000);
    int64_t samplesPerBin = totalSamples / static_cast<int64_t>(numBins);
    if (samplesPerBin < 1) samplesPerBin = 1;

    juce::AudioBuffer<float> buffer(numChannels, static_cast<int>(samplesPerBin));
    result.peaks.reserve(static_cast<size_t>(numBins) * 2u);
    for (int i = 0; i < numBins; ++i)
    {
        int64_t startSample = static_cast<int64_t>(i) * samplesPerBin;
        int numToRead = static_cast<int>((std::min)(samplesPerBin, totalSamples - startSample));
        if (numToRead <= 0)
        {
            result.peaks.push_back(0.0);
            result.peaks.push_back(0.0);
            continue;
        }
        buffer.clear();
        // A failed read leaves the cleared buffer full of zeros, which would
        // otherwise be reported (and cached client-side) as a silent-but-valid
        // waveform — a sticky blank waveform. Reading past the end returns true
        // with zeros, so this only trips on a genuine I/O error (file
        // busy/locked); surface it as an error so the client re-fetches instead
        // of caching silent garbage.
        if (!reader->read(&buffer, 0, numToRead, startSample, true, true))
        {
            result.ok = false;
            result.error = "could not read audio data";
            result.errorCode = -32602;
            result.peaks.clear();
            return result;
        }

        float minVal = 0.0f, maxVal = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getReadPointer(ch);
            for (int s = 0; s < numToRead; ++s)
            {
                if (data[s] < minVal) minVal = data[s];
                if (data[s] > maxVal) maxVal = data[s];
            }
        }
        result.peaks.push_back(static_cast<double>(minVal));
        result.peaks.push_back(static_cast<double>(maxVal));
    }
    result.ok = true;
    return result;
}

void AudioEngine::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property)
{
    if (treeWhosePropertyHasChanged.hasType(IDs::TRANSPORT))
    {
        if (property == IDs::isPlaying)
        {
            bool playing = treeWhosePropertyHasChanged.getProperty(IDs::isPlaying);
            transportManager.setPlaying(playing);
            juce::Logger::writeToLog("AudioEngine: Playback state changed to: " + juce::String(playing ? "Playing" : "Stopped"));
        }
        else if (property == IDs::position)
        {
            double pos = treeWhosePropertyHasChanged.getProperty(IDs::position);
            transportManager.setCurrentSample(static_cast<int64_t>(pos * transportManager.getSampleRate()));
        }
        else if (property == IDs::isLooping)
        {
            transportManager.setLooping(treeWhosePropertyHasChanged.getProperty(IDs::isLooping));
        }
        else if (property == IDs::loopStart)
        {
            double t = treeWhosePropertyHasChanged.getProperty(IDs::loopStart);
            transportManager.setLoopStartSample(static_cast<int64_t>(t * transportManager.getSampleRate()));
        }
        else if (property == IDs::loopEnd)
        {
            double t = treeWhosePropertyHasChanged.getProperty(IDs::loopEnd);
            transportManager.setLoopEndSample(static_cast<int64_t>(t * transportManager.getSampleRate()));
        }
        else if (property == IDs::metronomeEnabled)
        {
            mainProcessor->getMetronome().setEnabled(
                treeWhosePropertyHasChanged.getProperty(IDs::metronomeEnabled));
        }
        else if (property == IDs::timeSigNumerator)
        {
            int num = treeWhosePropertyHasChanged.getProperty(IDs::timeSigNumerator);
            mainProcessor->getMetronome().setBeatsPerBar(num > 0 ? num : 4);
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::PROJECT))
    {
        if (property == IDs::tempo)
        {
            double newBpm = treeWhosePropertyHasChanged.getProperty(IDs::tempo);
            transportManager.setBPM(newBpm);

            // Phase 3 — Follow project tempo: iterate all TempoMatch clips and
            // re-derive their stretch ratios. Temporarily remove the ValueTree
            // listener so the batch property writes don't trigger individual
            // rebuildRoutingGraph calls; we issue one explicit rebuild at the end.
            projectModel.getTree().removeListener(this);
            bool anyDirty = false;
            auto trackList = projectModel.getTrackListTree();
            for (int t = 0; t < trackList.getNumChildren(); ++t)
            {
                auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
                if (!clipList.isValid()) continue;
                for (int c = 0; c < clipList.getNumChildren(); ++c)
                {
                    auto clip = clipList.getChild(c);
                    if (static_cast<int>(clip.getProperty(IDs::stretchMode, 0)) != 1)
                        continue;
                    double sourceBpm = clip.getProperty(IDs::sourceBpm, 0.0);
                    if (sourceBpm <= 0.0) continue;

                    double ratio = sourceBpm / newBpm;
                    double sourceDur = clip.getProperty(IDs::sourceDuration, 0.0);
                    clip.setProperty(IDs::stretchRatio, ratio, nullptr);
                    if (sourceDur > 0.0)
                        clip.setProperty(IDs::duration, sourceDur * ratio, nullptr);
                    anyDirty = true;
                }
            }
            projectModel.getTree().addListener(this);

            if (anyDirty && mainProcessor != nullptr)
            {
                // Invalidate the stretch cache for these clips so they are
                // re-rendered with the new ratio.
                for (int t = 0; t < trackList.getNumChildren(); ++t)
                {
                    auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
                    if (!clipList.isValid()) continue;
                    for (int c = 0; c < clipList.getNumChildren(); ++c)
                    {
                        auto clip = clipList.getChild(c);
                        if (static_cast<int>(clip.getProperty(IDs::stretchMode, 0)) != 1)
                            continue;
                        if (static_cast<double>(clip.getProperty(IDs::sourceBpm, 0.0)) <= 0.0)
                            continue;
                        int clipId = clip.getProperty(IDs::clipID);
                        stretchCache.invalidate(clipId);
                    }
                }
                mainProcessor->rebuildRoutingGraph();
            }
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::TEMPO_POINT))
    {
        rebuildTempoMap();
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::TRACK))
    {
        if (property == IDs::midiChannel)
        {
            int newChannel = treeWhosePropertyHasChanged.getProperty(IDs::midiChannel);
            auto trackList = projectModel.getTrackListTree();
            int tIdx = -1;
            for (int i = 0; i < trackList.getNumChildren(); ++i)
            {
                if (trackList.getChild(i) == treeWhosePropertyHasChanged)
                {
                    tIdx = i;
                    break;
                }
            }
            if (tIdx >= 0 && mainProcessor != nullptr)
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->setTrackMidiChannel(tIdx, newChannel);
            }
        }
        else if (property == IDs::volume || property == IDs::pan)
        {
            float value = treeWhosePropertyHasChanged.getProperty(property);
            int paramID = (property == IDs::volume) ? 1 : 2;

            auto trackList = projectModel.getTrackListTree();
            for (int i = 0; i < trackList.getNumChildren(); ++i)
            {
                if (trackList.getChild(i) == treeWhosePropertyHasChanged)
                {
                    ParamUpdate update{ i, paramID, value };
                    spscBridge.pushUpdate(update);

                    if (transportManager.isPlayingNow())
                    {
                        auto autoList = treeWhosePropertyHasChanged.getChildWithName(IDs::AUTOMATION_LIST);
                        for (int a = 0; a < autoList.getNumChildren(); ++a)
                        {
                            auto autoTree = autoList.getChild(a);
                            if (static_cast<int>(autoTree.getProperty(IDs::paramID)) == paramID)
                            {
                                double timeSec = static_cast<double>(transportManager.getCurrentSample())
                                    / transportManager.getSampleRate();
                                auto pointList = autoTree.getChildWithName(IDs::POINT_LIST);
                                if (!pointList.isValid())
                                {
                                    pointList = juce::ValueTree(IDs::POINT_LIST);
                                    autoTree.addChild(pointList, -1, nullptr);
                                }
                                juce::ValueTree pt(IDs::POINT);
                                pt.setProperty(IDs::startTime, timeSec, nullptr);
                                pt.setProperty(IDs::gain, static_cast<double>(value), nullptr);
                                pointList.addChild(pt, -1, nullptr);
                                if (mainProcessor != nullptr)
                                    mainProcessor->rebuildAutomationCache(i);
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }
        else if (property == IDs::isMuted || property == IDs::isSoloed)
        {
            pushEffectiveMuteState();
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::CLIP))
    {
        if (property == IDs::gain    || property == IDs::fadeIn  || property == IDs::fadeOut ||
            property == IDs::startTime || property == IDs::duration ||
            property == IDs::offset   || property == IDs::looping || property == IDs::muted)
        {
            float value = treeWhosePropertyHasChanged.getProperty(property);
            int paramID;
            if      (property == IDs::gain)      paramID = 10;
            else if (property == IDs::fadeIn)    paramID = 11;
            else if (property == IDs::fadeOut)   paramID = 12;
            else if (property == IDs::startTime) paramID = 13;
            else if (property == IDs::duration)  paramID = 14;
            else if (property == IDs::offset)    paramID = 15;
            else if (property == IDs::looping)   paramID = 16;
            else                                  paramID = 17; // muted

            // Find which track + clip index this belongs to
            auto trackList = projectModel.getTrackListTree();
            for (int t = 0; t < trackList.getNumChildren(); ++t)
            {
                auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
                if (!clipList.isValid()) continue;

                for (int c = 0; c < clipList.getNumChildren(); ++c)
                {
                    if (clipList.getChild(c) == treeWhosePropertyHasChanged)
                    {
                        ParamUpdate update{ t, paramID, value, c };
                        spscBridge.pushUpdate(update);
                        break;
                    }
                }
            }
        }
        else if (property == IDs::stretchMode || property == IDs::stretchRatio)
        {
            // Stretch is decided at graph-build time (not RT-parametric),
            // so we don't push an SPSC update. Instead, trigger a rebuild:
            // RoutingManager::rebuildClipsForTrack reads the resolved ratio
            // and either adopts a cached stretched buffer or requests a
            // background render via StretchCache.
            if (mainProcessor != nullptr)
                mainProcessor->rebuildRoutingGraph();
        }

        // Ghost propagation: when a source clip's content property changes,
        // propagate to all ghosts. Guard against re-entrant writes from the
        // propagation itself.
        if (!isPropagating_ && mainProcessor != nullptr)
        {
            int clipIsGhost = static_cast<int>(treeWhosePropertyHasChanged.getProperty(IDs::isGhost, 0));
            if (clipIsGhost == 0)
            {
                int srcClipID = static_cast<int>(treeWhosePropertyHasChanged.getProperty(IDs::clipID, -1));
                if (srcClipID >= 0)
                {
                    // Propagated properties
                    static const std::vector<juce::Identifier> propagatedProps = {
                        IDs::gain, IDs::fadeIn, IDs::fadeOut, IDs::looping,
                        IDs::offset, IDs::sourceFile, IDs::sourceBpm,
                        IDs::stretchMode, IDs::stretchRatio, IDs::sourceDuration
                    };
                    bool isPropagated = false;
                    for (const auto& p : propagatedProps)
                    {
                        if (property == p) { isPropagated = true; break; }
                    }

                    if (isPropagated)
                    {
                        auto newValue = treeWhosePropertyHasChanged.getProperty(property);
                        auto trackList = projectModel.getTrackListTree();
                        for (int t = 0; t < trackList.getNumChildren(); ++t)
                        {
                            auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
                            if (!clipList.isValid()) continue;
                            for (int c = 0; c < clipList.getNumChildren(); ++c)
                            {
                                auto ghostClip = clipList.getChild(c);
                                int ghostSrc = static_cast<int>(ghostClip.getProperty(IDs::ghostSourceId, -1));
                                if (ghostSrc == srcClipID)
                                {
                                    isPropagating_ = true;
                                    ghostClip.setProperty(property, newValue, nullptr);
                                    isPropagating_ = false;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::MIDI_NOTE))
    {
        auto noteList = treeWhosePropertyHasChanged.getParent();
        if (noteList.isValid() && noteList.hasType(IDs::MIDI_NOTE_LIST))
        {
            auto clipTree = noteList.getParent();
            if (clipTree.isValid() && clipTree.hasType(IDs::CLIP) && mainProcessor != nullptr)
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->rebuildMidiClipCache(clipTree);
            }
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::CC_POINT))
    {
        auto ccList = treeWhosePropertyHasChanged.getParent();
        if (ccList.isValid() && ccList.hasType(IDs::CC_LIST))
        {
            auto clipTree = ccList.getParent();
            if (clipTree.isValid() && clipTree.hasType(IDs::CLIP) && mainProcessor != nullptr)
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->rebuildMidiClipCache(clipTree);
            }
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::MODULATION))
    {
        // Walk up CLIP_LIST is NOT the parent chain here: the MODULATION
        // node lives under TRACK → MODULATION_LIST → MODULATION. Resolve the
        // owning track so we can rebuild its modulation sources. This covers
        // the MCP/undo/load paths that mutate the tree without going through
        // ModulationWidget (which calls rebuildModulation directly).
        if (mainProcessor != nullptr)
        {
            if (int tIdx = modulationTrackIndexOf(treeWhosePropertyHasChanged); tIdx >= 0)
                mainProcessor->rebuildModulation(tIdx);
        }
    }
    else if (treeWhosePropertyHasChanged.hasType(IDs::FX_SLOT))
    {
        if (!mainProcessor) return;
        // Forward param_N property changes to the live TrackFXSlot for
        // internal (non-plugin) FX. Plugin param changes go through the
        // SPSC bridge — they are not handled here.
        juce::String propStr = property.toString();
        if (!propStr.startsWith("param_"))
            return;

        // Extract param index from "param_N"
        int paramIndex = propStr.substring(6).getIntValue();
        float value = static_cast<float>(treeWhosePropertyHasChanged.getProperty(property));

        // Find track index and slot index by walking the tree
        auto fxChain = treeWhosePropertyHasChanged.getParent();
        if (!fxChain.isValid() || !fxChain.hasType(IDs::FX_CHAIN))
            return;
        auto trackTree = fxChain.getParent();
        if (!trackTree.isValid() || !trackTree.hasType(IDs::TRACK))
            return;

        auto trackList = projectModel.getTrackListTree();
        int trackIdx = -1;
        int slotIdx = -1;
        for (int t = 0; t < trackList.getNumChildren(); ++t)
        {
            if (trackList.getChild(t) == trackTree)
            {
                trackIdx = t;
                break;
            }
        }
        if (trackIdx < 0) return;

        // Find slot index
        for (int s = 0; s < fxChain.getNumChildren(); ++s)
        {
            if (fxChain.getChild(s) == treeWhosePropertyHasChanged)
            {
                slotIdx = s;
                break;
            }
        }
        if (slotIdx < 0) return;

        // Only forward to internal (non-plugin) slots
        juce::String fxType = treeWhosePropertyHasChanged.getProperty(IDs::fxType).toString();
        if (fxType == "plugin" || fxType.isEmpty())
            return;

        auto* track = mainProcessor->getTrack(trackIdx);
        if (track == nullptr) return;
        auto& chain = track->getFXChain();
        if (slotIdx >= static_cast<int>(chain.size()) || chain[slotIdx] == nullptr)
            return;

        chain[slotIdx]->setInternalParam(paramIndex, value);
    }
}

void AudioEngine::valueTreeChildAdded(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenAdded)
{
    if (childWhichHasBeenAdded.hasType(IDs::TRANSPORT))
    {
        // A new transport node was added (e.g. after File→New).
        // Properties were set before addChild, so valueTreePropertyChanged
        // never fired for them. Push the initial state to the audio thread.
        transportManager.setLooping(childWhichHasBeenAdded.getProperty(IDs::isLooping));
        double sr = transportManager.getSampleRate();
        transportManager.setLoopStartSample(static_cast<int64_t>(
            static_cast<double>(childWhichHasBeenAdded.getProperty(IDs::loopStart)) * sr));
        transportManager.setLoopEndSample(static_cast<int64_t>(
            static_cast<double>(childWhichHasBeenAdded.getProperty(IDs::loopEnd)) * sr));
        transportManager.setPlaying(childWhichHasBeenAdded.getProperty(IDs::isPlaying));
    }
    if (parentTree.hasType(IDs::TEMPO_POINT_LIST) || parentTree.hasType(IDs::PROJECT))
        rebuildTempoMap();

    if (childWhichHasBeenAdded.hasType(IDs::MIDI_NOTE) && mainProcessor != nullptr)
    {
        if (parentTree.hasType(IDs::MIDI_NOTE_LIST))
        {
            auto clipTree = parentTree.getParent();
            if (clipTree.isValid() && clipTree.hasType(IDs::CLIP))
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->rebuildMidiClipCache(clipTree);

                // Propagate the new note to all ghosts of the source clip
                int srcIsGhost = static_cast<int>(clipTree.getProperty(IDs::isGhost, 0));
                if (srcIsGhost == 0)
                {
                    int srcClipID = static_cast<int>(clipTree.getProperty(IDs::clipID, -1));
                    if (srcClipID >= 0)
                    {
                        auto trackList = projectModel.getTrackListTree();
                        for (int t = 0; t < trackList.getNumChildren(); ++t)
                        {
                            auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
                            if (!clipList.isValid()) continue;
                            for (int c = 0; c < clipList.getNumChildren(); ++c)
                            {
                                auto ghostClip = clipList.getChild(c);
                                int ghostSrc = static_cast<int>(ghostClip.getProperty(IDs::ghostSourceId, -1));
                                if (ghostSrc == srcClipID)
                                {
                                    auto ghostNoteList = ghostClip.getChildWithName(IDs::MIDI_NOTE_LIST);
                                    if (!ghostNoteList.isValid())
                                    {
                                        ghostNoteList = juce::ValueTree(IDs::MIDI_NOTE_LIST);
                                        ghostClip.addChild(ghostNoteList, -1, nullptr);
                                    }
                                    auto noteCopy = childWhichHasBeenAdded.createCopy();
                                    ghostNoteList.addChild(noteCopy, -1, nullptr);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (childWhichHasBeenAdded.hasType(IDs::CC_POINT) && mainProcessor != nullptr)
    {
        if (parentTree.hasType(IDs::CC_LIST))
        {
            auto clipTree = parentTree.getParent();
            if (clipTree.isValid() && clipTree.hasType(IDs::CLIP))
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->rebuildMidiClipCache(clipTree);
            }
        }
    }

    if (childWhichHasBeenAdded.hasType(IDs::MODULATION) && mainProcessor != nullptr)
    {
        if (int tIdx = modulationTrackIndexOf(parentTree); tIdx >= 0)
            mainProcessor->rebuildModulation(tIdx);
    }

    // Rebuild the routing graph when a new clip or track is added at runtime,
    // so clips/tracks created by the frontend are connected to the audio graph.
    // Without this, the ValueTree has the new clip/track (visible in the
    // arrange window) but the AudioProcessorGraph never processes it (silent).
    if (childWhichHasBeenAdded.hasType(IDs::CLIP) && mainProcessor != nullptr)
    {
        HDAW_LOG("DIAG", "valueTreeChildAdded: new CLIP, scheduling routing graph rebuild");
        triggerAsyncUpdate(); // coalesced — see handleAsyncUpdate()
    }
    if (childWhichHasBeenAdded.hasType(IDs::TRACK) && mainProcessor != nullptr)
    {
        HDAW_LOG("DIAG", "valueTreeChildAdded: new TRACK, scheduling routing graph rebuild");
        triggerAsyncUpdate(); // coalesced — see handleAsyncUpdate()
    }
}

void AudioEngine::valueTreeChildRemoved(juce::ValueTree& parentTree, juce::ValueTree& childWhichHasBeenRemoved, int indexFromWhichItWasRemoved)
{
    juce::ignoreUnused(indexFromWhichItWasRemoved);
    if (parentTree.hasType(IDs::TEMPO_POINT_LIST) || parentTree.hasType(IDs::PROJECT))
        rebuildTempoMap();

    if (childWhichHasBeenRemoved.hasType(IDs::TRANSPORT))
    {
        transportManager.setPlaying(false);
        transportManager.setLooping(false);
        transportManager.setCurrentSample(0);
    }

    if (childWhichHasBeenRemoved.hasType(IDs::MIDI_NOTE) && mainProcessor != nullptr)
    {
        if (parentTree.hasType(IDs::MIDI_NOTE_LIST))
        {
            auto clipTree = parentTree.getParent();
            if (clipTree.isValid() && clipTree.hasType(IDs::CLIP))
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->rebuildMidiClipCache(clipTree);

                // Remove matching note from all ghosts of the source clip.
                // Ghosts get fresh noteIDs at creation time, so we match by
                // content (noteNumber, startBeat) per the ghost-copy spec §2.4.
                int srcIsGhost = static_cast<int>(clipTree.getProperty(IDs::isGhost, 0));
                if (srcIsGhost == 0)
                {
                    int srcClipID = static_cast<int>(clipTree.getProperty(IDs::clipID, -1));
                    if (srcClipID >= 0)
                    {
                        int noteNumber = static_cast<int>(childWhichHasBeenRemoved.getProperty(IDs::noteNumber, -1));
                        double startBeat = childWhichHasBeenRemoved.getProperty(IDs::startBeat, -1e18);
                        if (noteNumber >= 0)
                        {
                            auto trackList = projectModel.getTrackListTree();
                            for (int t = 0; t < trackList.getNumChildren(); ++t)
                            {
                                auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
                                if (!clipList.isValid()) continue;
                                for (int c = 0; c < clipList.getNumChildren(); ++c)
                                {
                                    auto ghostClip = clipList.getChild(c);
                                    int ghostSrc = static_cast<int>(ghostClip.getProperty(IDs::ghostSourceId, -1));
                                    if (ghostSrc == srcClipID)
                                    {
                                        auto ghostNoteList = ghostClip.getChildWithName(IDs::MIDI_NOTE_LIST);
                                        if (ghostNoteList.isValid())
                                        {
                                            for (int n = ghostNoteList.getNumChildren() - 1; n >= 0; --n)
                                            {
                                                auto gn = ghostNoteList.getChild(n);
                                                if (static_cast<int>(gn.getProperty(IDs::noteNumber, -1)) == noteNumber
                                                    && std::abs(static_cast<double>(gn.getProperty(IDs::startBeat, 1e18)) - startBeat) < 1e-9)
                                                {
                                                    ghostNoteList.removeChild(gn, nullptr);
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (childWhichHasBeenRemoved.hasType(IDs::CC_POINT) && mainProcessor != nullptr)
    {
        if (parentTree.hasType(IDs::CC_LIST))
        {
            auto clipTree = parentTree.getParent();
            if (clipTree.isValid() && clipTree.hasType(IDs::CLIP))
            {
                if (auto* rm = mainProcessor->getRoutingManager())
                    rm->rebuildMidiClipCache(clipTree);
            }
        }
    }

    if (childWhichHasBeenRemoved.hasType(IDs::CC_LIST) && mainProcessor != nullptr)
    {
        if (parentTree.hasType(IDs::CLIP))
        {
            if (auto* rm = mainProcessor->getRoutingManager())
                rm->rebuildMidiClipCache(parentTree);
        }
    }

    if (childWhichHasBeenRemoved.hasType(IDs::MODULATION) && mainProcessor != nullptr)
    {
        if (int tIdx = modulationTrackIndexOf(parentTree); tIdx >= 0)
            mainProcessor->rebuildModulation(tIdx);
    }

    if (childWhichHasBeenRemoved.hasType(IDs::CLIP))
    {
        // Before rebuilding, if the removed clip is a source (not a ghost),
        // remove all ghosts that reference it. Use a re-entrancy guard to
        // prevent infinite recursion when ghost removals trigger this handler.
        if (!removingGhosts_)
        {
            int clipIsGhost = static_cast<int>(childWhichHasBeenRemoved.getProperty(IDs::isGhost, 0));
            if (clipIsGhost == 0)
            {
                int srcID = static_cast<int>(childWhichHasBeenRemoved.getProperty(IDs::clipID, -1));
                if (srcID >= 0)
                {
                    removingGhosts_ = true;
                    auto trackList = projectModel.getTrackListTree();
                    for (int t = 0; t < trackList.getNumChildren(); ++t)
                    {
                        auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
                        if (!clipList.isValid()) continue;
                        for (int c = clipList.getNumChildren() - 1; c >= 0; --c)
                        {
                            auto ghostClip = clipList.getChild(c);
                            int ghostSrc = static_cast<int>(ghostClip.getProperty(IDs::ghostSourceId, -1));
                            if (ghostSrc == srcID)
                                clipList.removeChild(ghostClip, nullptr);
                        }
                    }
                    removingGhosts_ = false;
                }
            }
        }

        if (mainProcessor != nullptr)
        {
            HDAW_LOG("DIAG", "valueTreeChildRemoved: CLIP removed, scheduling routing graph rebuild");
            triggerAsyncUpdate(); // coalesced — see handleAsyncUpdate()
        }
    }
    if (childWhichHasBeenRemoved.hasType(IDs::TRACK) && mainProcessor != nullptr)
    {
        HDAW_LOG("DIAG", "valueTreeChildRemoved: TRACK removed, scheduling routing graph rebuild");
        triggerAsyncUpdate(); // coalesced — see handleAsyncUpdate()
    }
}

void AudioEngine::handleAsyncUpdate()
{
    // Runs on the message thread. Any number of clip/track add/remove events
    // that called triggerAsyncUpdate() during the same tick collapse into this
    // single rebuild.
    if (mainProcessor != nullptr)
        mainProcessor->rebuildRoutingGraph();
}

void AudioEngine::timerCallback()
{
    if (transportManager.consumeAutoStopRequested())
    {
        // The audio thread stopped playback because position exceeded the
        // project end. Update the ValueTree so the frontend transport bar
        // reflects the stopped state, and reset position to zero so the
        // next Play starts from the beginning (standard DAW behavior).
        transportManager.setCurrentSample(0);
        auto transportTree = projectModel.getTransportTree();
        if (transportTree.isValid())
        {
            auto& um = projectModel.getUndoManager();
            transportTree.setProperty(IDs::isPlaying, false, &um);
            transportTree.setProperty(IDs::position, 0.0, &um);
        }
    }

    if (transportManager.consumePunchOutRequested())
    {
        if (auto* proc = getMainProcessor())
            proc->stopRecording();
    }

    if (auto* proc = getMainProcessor())
    {
        if (proc->consumeRecordStartPending())
            proc->beginActualRecording();
    }
}

void AudioEngine::rebuildTempoMap()
{
    auto tempoList = projectModel.getTree().getChildWithName(IDs::TEMPO_POINT_LIST);
    if (!tempoList.isValid())
        return;

    auto map = std::make_shared<std::vector<HDAW::TempoPoint>>();
    for (int i = 0; i < tempoList.getNumChildren(); ++i)
    {
        auto pt = tempoList.getChild(i);
        double t = pt.getProperty(IDs::startTime);
        double b = pt.getProperty(IDs::tempo);
        map->push_back({ t, b });
    }

    std::sort(map->begin(), map->end(),
              [](const HDAW::TempoPoint& a, const HDAW::TempoPoint& b) {
                  return a.timeInSeconds < b.timeInSeconds;
              });

    transportManager.setTempoMap(map);
}

ProjectCommands& AudioEngine::getProjectCommands()  { return *commands; }
TransportCommands& AudioEngine::getTransportCommands() { return *commands; }
AudioGraphCommands& AudioEngine::getAudioGraphCommands() { return *commands; }
ReadModel& AudioEngine::getReadModel() { return *readModel; }

void AudioEngine::pushEffectiveMuteState()
{
    auto trackList = projectModel.getTrackListTree();
    int numTracks = trackList.getNumChildren();

    bool anySoloed = false;
    for (int i = 0; i < numTracks; ++i)
    {
        if (static_cast<bool>(trackList.getChild(i).getProperty(IDs::isSoloed, false)))
        {
            anySoloed = true;
            break;
        }
    }

    for (int i = 0; i < numTracks; ++i)
    {
        auto t = trackList.getChild(i);
        bool muted = t.getProperty(IDs::isMuted, false);
        bool soloed = t.getProperty(IDs::isSoloed, false);
        bool effectiveMute = muted || (anySoloed && !soloed);
        ParamUpdate update{ i, 3, effectiveMute ? 1.0f : 0.0f };
        spscBridge.pushUpdate(update);
    }
}
