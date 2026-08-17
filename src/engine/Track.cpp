#include "Track.h"
#include "../common/DebugLog.h"
#include "../common/BufferCheck.h"
#include <cmath>

namespace HDAW {

Track::Track()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    volumeGain.setCurrentAndTargetValue(1.0f);
    panPosition.setCurrentAndTargetValue(0.0f);
    modulationManager = std::make_unique<ModulationManager>();
}

Track::~Track() = default;

void Track::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    volumeGain.reset(sampleRate, 0.05);
    panPosition.reset(sampleRate, 0.05);

    // Guard the fxChain / automationManagers / modulationManager iteration
    // with stateLock. prepareToPlay runs on the message thread inside the
    // graph's async applySettings (Pimpl::handleAsyncUpdate) AND on the audio
    // thread at device start, while the command thread can be concurrently
    // clearing those same vectors in rebuildFXChain / setAutomationTrees /
    // rebuildModulation (which hold stateLock). An unlocked iteration there
    // races the vector clear -> use-after-free / heap corruption. Follow the
    // processBlock pattern: tryEnter() and skip the re-prepare if the lock is
    // contended (the rebuild that holds the lock prepares the chain itself).
    if (!stateLock.tryEnter())
        return;

    fxSpec.sampleRate = sampleRate;
    fxSpec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    fxSpec.numChannels = 2;

    meter.prepare(sampleRate, samplesPerBlock);

    for (const auto& slot : fxChain)
        if (slot)
            slot->prepare(fxSpec);

    for (const auto& am : automationManagers)
        if (am)
            am->rebuildCache();

    if (modulationManager)
        modulationManager->prepare(sampleRate);

    updateLatency();
    stateLock.exit();
}

void Track::updateLatency()
{
    int totalLatency = 0;
    for (const auto& slot : fxChain)
    {
        if (slot != nullptr && slot->getPluginInstance() != nullptr)
            totalLatency += slot->getPluginInstance()->getLatencySamples();
    }
    setLatencySamples(totalLatency);
}

void Track::setAutomationTrees(const juce::ValueTree& automationList)
{
    juce::SpinLock::ScopedLockType lock(stateLock);
    automationManagers.clear();
    if (!automationList.isValid()) return;

    for (int i = 0; i < automationList.getNumChildren(); ++i)
    {
        auto autoTree = automationList.getChild(i);
        auto am = std::make_unique<AutomationManager>();
        am->setAutomationTree(autoTree);
        am->setParamID(autoTree.getProperty(IDs::paramID));
        am->setEnabled(autoTree.getProperty(IDs::automationEnabled));
        automationManagers.push_back(std::move(am));
    }
}

void Track::releaseResources()
{
    // CLAP spec requires reset() on the audio thread. Defer to the
    // next processBlock() call instead of calling here (which may be
    // on the message thread during graph rebuild).
    pendingReset.store(true, std::memory_order_release);
}

void Track::rebuildFXChain(const juce::ValueTree& fxChainTree)
{
    juce::SpinLock::ScopedLockType lock(stateLock);

    // Save plugin state from existing slots before clearing
    std::vector<char> matched(static_cast<size_t>(fxChainTree.getNumChildren()), 0);
    for (const auto& slot : fxChain)
    {
        if (slot && slot->isPlugin() && slot->getPluginInstance())
        {
            auto* instance = slot->getPluginInstance();
            juce::MemoryBlock state;
            instance->getStateInformation(state);

            // Find matching FX_SLOT in the tree by pluginID
            if (fxChainTree.isValid())
            {
                for (int i = 0; i < fxChainTree.getNumChildren(); ++i)
                {
                    if (matched[static_cast<size_t>(i)])
                        continue;
                    auto child = fxChainTree.getChild(i);
                    if (child.getProperty(IDs::pluginID).toString() == slot->getPluginID())
                    {
                        matched[static_cast<size_t>(i)] = 1;
                        // empty state (dead plugin process) would clobber the last-good saved state
                        if (state.getSize() > 0)
                            child.setProperty(IDs::pluginState, state.toBase64Encoding(), nullptr);
                        break;
                    }
                }
            }
        }
    }

    // Close all open editors before clearing
    for (const auto& slot : fxChain)
        if (slot)
            slot->closeEditor();

    fxChain.clear();

    if (!fxChainTree.isValid())
        return;

    for (int i = 0; i < fxChainTree.getNumChildren(); ++i)
    {
        auto slotTree = fxChainTree.getChild(i);
        juce::String type = slotTree.getProperty(IDs::fxType).toString();
        
        if (type == "none" || type.isEmpty())
        {
            fxChain.push_back(std::make_unique<TrackFXSlot>("none"));
            continue;
        }

        if (type == "plugin")
        {
            juce::String pluginID = slotTree.getProperty(IDs::pluginID).toString();
            juce::String pluginFormat = slotTree.getProperty(IDs::pluginFormat).toString();
            if (pluginID.isEmpty())
            {
                fxChain.push_back(std::make_unique<TrackFXSlot>("none"));
                continue;
            }

            juce::PluginDescription desc;
            desc.fileOrIdentifier = pluginID;
            if (pluginFormat == "VST3")
                desc.pluginFormatName = "VST3";
            else if (pluginFormat == "CLAP")
                desc.pluginFormatName = "CLAP";
            else
            {
                fxChain.push_back(std::make_unique<TrackFXSlot>("none"));
                continue;
            }

            juce::String error;
            bool wantIsolated = pluginManager && pluginManager->isolationEnabled;
            double effectiveSr = (fxSpec.sampleRate > 0.0) ? fxSpec.sampleRate : getSampleRate();
            int effectiveBs = (fxSpec.maximumBlockSize > 0) ? static_cast<int>(fxSpec.maximumBlockSize) : getBlockSize();
            HDAW_LOG("FXRebuild", (juce::String("rebuildFXChain pluginID=") + pluginID + " fmt=" + pluginFormat + " isolated=" + (wantIsolated?"true":"false") + " pluginMgr=" + (pluginManager?"ok":"null") + " sr=" + juce::String(effectiveSr) + " getSr=" + juce::String(getSampleRate())).toStdString());
            auto plugin = pluginManager != nullptr
                ? pluginManager->createPluginInstance(desc, error, effectiveSr, effectiveBs, wantIsolated)
                : nullptr;

            HDAW_LOG("FXRebuild", (juce::String("createPluginInstance result: ") + (plugin ? "ok" : "NULL") + " error=" + error).toStdString());

            if (plugin != nullptr)
            {
                auto slot = std::make_unique<TrackFXSlot>(std::move(plugin), pluginID, wantIsolated);
                slot->setBypassed(slotTree.getProperty(IDs::bypassed));

                juce::String stateStr = slotTree.getProperty(IDs::pluginState).toString();
                if (stateStr.isNotEmpty())
                {
                    juce::MemoryBlock state;
                    const bool decOk = state.fromBase64Encoding(stateStr);
                    if (decOk)
                        slot->getPluginInstance()->setStateInformation(state.getData(),
                            static_cast<int>(state.getSize()));
                }

                if (fxSpec.sampleRate > 0)
                {
                    slot->prepare(fxSpec);
                }

                if (wantIsolated && pluginManager) {
                    int sid = slot->proxySlotId();
                    if (sid >= 0) pluginManager->registerSlotTrackIndex(static_cast<uint32_t>(sid), trackIndex);
                }

                fxChain.push_back(std::move(slot));
            }
            else
            {
                if (error.isNotEmpty())
                    juce::Logger::writeToLog("HDAW: Failed to load plugin " + pluginID + ": " + error);
                fxChain.push_back(std::make_unique<TrackFXSlot>("none"));
            }
            continue;
        }

        if (type == "sampler")
        {
            auto slot = std::make_unique<TrackFXSlot> ("sampler");
            slot->setBypassed (slotTree.getProperty (IDs::bypassed));
            if (fxSpec.sampleRate > 0)
            {
                slot->prepare (fxSpec);
                slot->loadSamplerState (slotTree, nullptr, decodedPool);
                slot->loadParamsFromTree (slotTree);
            }
            fxChain.push_back (std::move (slot));
            continue;
        }

        auto slot = std::make_unique<TrackFXSlot>(type);
        slot->setBypassed(slotTree.getProperty(IDs::bypassed));

        if (fxSpec.sampleRate > 0)
        {
            slot->prepare(fxSpec);
            slot->loadParamsFromTree(slotTree);
        }

        fxChain.push_back(std::move(slot));
    }

    updateLatency();
}

void Track::rebuildModulation(const juce::ValueTree& modulationListTree)
{
    if (!modulationManager) return;
    // Hold stateLock while mutating the source list: processBlock reads
    // `modulationManager->getModulation(...)` per-sample (see the volume/pan
    // loop). Without this lock an LFO add/remove/param change during playback
    // races the audio thread's iteration over `sources` (a vector of
    // unique_ptr that rebuild() clears + reallocates). Matches the
    // rebuildFXChain / setAutomationTrees pattern. processBlock uses
    // tryEnter() on the read side so a contested block skips modulation
    // rather than blocking the audio thread.
    juce::SpinLock::ScopedLockType lock(stateLock);
    modulationManager->rebuild(modulationListTree, getSampleRate());
}

void Track::rebuildMidiFXChain(const juce::ValueTree& midiFxChainTree)
{
    juce::SpinLock::ScopedLockType lock(stateLock);
    midiFxChain.clear();
    if (!midiFxChainTree.isValid()) return;

    for (int i = 0; i < midiFxChainTree.getNumChildren(); ++i)
    {
        auto slotTree = midiFxChainTree.getChild(i);
        juce::String type = slotTree.getProperty(IDs::fxType).toString();
        std::unique_ptr<MidiEffect> effect;
        if (type == "arpeggiator")
        {
            auto arp = std::make_unique<Arpeggiator>();
            arp->rate = static_cast<double>(slotTree.getProperty(IDs::arpRate, 0.25));
            arp->pattern = static_cast<int>(slotTree.getProperty(IDs::arpPattern, 0));
            arp->octaves = static_cast<int>(slotTree.getProperty(IDs::arpOctaves, 1));
            arp->gate = static_cast<double>(slotTree.getProperty(IDs::arpGate, 0.5));
            effect = std::move(arp);
        }
        else if (type == "velocity")
        {
            auto v = std::make_unique<VelocityScaler>();
            v->factor = static_cast<double>(slotTree.getProperty(IDs::velFactor, 1.0));
            effect = std::move(v);
        }
        else if (type == "chord")
        {
            auto c = std::make_unique<Chorder>();
            c->chordType = static_cast<int>(slotTree.getProperty(IDs::chordType, 0));
            effect = std::move(c);
        }
        else if (type == "scale")
        {
            auto s = std::make_unique<ScaleQuantize>();
            s->root = static_cast<int>(slotTree.getProperty(IDs::scaleRoot, 0));
            s->scaleType = static_cast<int>(slotTree.getProperty(IDs::scaleType, 0));
            effect = std::move(s);
        }
        else if (type == "notelength")
        {
            auto nl = std::make_unique<NoteLengthScaler>();
            nl->factor = static_cast<double>(slotTree.getProperty(IDs::lengthFactor, 1.0));
            effect = std::move(nl);
        }
        else if (type == "transpose")
        {
            auto t = std::make_unique<Transpose>();
            t->semitones = static_cast<int>(slotTree.getProperty(IDs::semitones, 0));
            effect = std::move(t);
        }
        else if (type == "keyfilter")
        {
            auto kf = std::make_unique<KeyFilter>();
            kf->root = static_cast<int>(slotTree.getProperty(IDs::keyFilterRoot, 0));
            kf->scaleType = static_cast<int>(slotTree.getProperty(IDs::keyFilterScale, 0));
            effect = std::move(kf);
        }
        else if (type == "multinote")
        {
            auto mn = std::make_unique<MultiNote>();
            juce::String intervalStr = slotTree.getProperty(IDs::multiNoteIntervals, "0,7,12").toString();
            mn->intervals.clear();
            for (auto& tok : juce::StringArray::fromTokens(intervalStr, ",", ""))
            {
                int val = tok.trim().getIntValue();
                mn->intervals.push_back(val);
            }
            if (mn->intervals.empty()) mn->intervals = { 0, 7, 12 };
            effect = std::move(mn);
        }
        else if (type == "velocitycurve")
        {
            auto vc = std::make_unique<VelocityCurve>();
            vc->curveType = static_cast<int>(slotTree.getProperty(IDs::curveType, 0));
            vc->curveAmount = static_cast<double>(slotTree.getProperty(IDs::curveAmount, 0.5));
            effect = std::move(vc);
        }
        else if (type == "notechance")
        {
            auto nc = std::make_unique<NoteChance>();
            nc->noteChance = static_cast<double>(slotTree.getProperty(IDs::noteChance, 1.0));
            effect = std::move(nc);
        }
        else if (type == "mididelay")
        {
            auto md = std::make_unique<MidiDelay>();
            md->delayBeats = static_cast<double>(slotTree.getProperty(IDs::delayBeats, 0.25));
            md->feedback = static_cast<double>(slotTree.getProperty(IDs::delayFeedback, 0.0));
            md->mix = static_cast<double>(slotTree.getProperty(IDs::delayMix, 0.5));
            effect = std::move(md);
        }
        else if (type == "humanize")
        {
            auto h = std::make_unique<Humanize>();
            h->humanizeTiming = static_cast<double>(slotTree.getProperty(IDs::humanizeTiming, 0.0));
            h->humanizeVelocity = static_cast<double>(slotTree.getProperty(IDs::humanizeVelocity, 0.0));
            h->humanizePitch = static_cast<double>(slotTree.getProperty(IDs::humanizePitch, 0.0));
            effect = std::move(h);
        }
        else if (type == "strum")
        {
            auto st = std::make_unique<Strum>();
            st->strumTime = static_cast<double>(slotTree.getProperty(IDs::strumTime, 0.02));
            st->strumDirection = static_cast<int>(slotTree.getProperty(IDs::strumDirection, 0));
            effect = std::move(st);
        }
        if (effect)
        {
            auto slot = std::make_unique<MidiFxSlot>(std::move(effect), type);
            slot->setBypassed(static_cast<bool>(slotTree.getProperty(IDs::bypassed, false)));
            slot->loadParamsFromTree(slotTree);
            midiFxChain.push_back(std::move(slot));
        }
    }
}

void Track::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    static const bool audioDiag = juce::SystemStats::getEnvironmentVariable("HDAW_AUDIO_THREAD_DIAG", "") == "1";
    static std::atomic<int> trackPBCount{0};
    int tc = trackPBCount.fetch_add(1, std::memory_order_relaxed);
    if (audioDiag && (tc < 5 || (tc % 500) == 0))
        HDAW_LOG("TrackPB", "call=" + juce::String(tc)
            + " bufCh=" + juce::String(buffer.getNumChannels())
            + " bufS=" + juce::String(buffer.getNumSamples())
            + " midi=" + juce::String(midiMessages.getNumEvents()));

    // Service deferred reset from releaseResources(). CLAP spec requires
    // reset() on the audio thread — this is the audio thread.
    if (pendingReset.exchange(false, std::memory_order_acquire))
    {
        for (const auto& slot : fxChain)
            if (slot)
                slot->reset();
    }

    // Run automation loop BEFORE mute check so mute automation can both
    // silence and un-silence the track.
    if (auto* ph = getPlayHead())
    {
        auto pos = ph->getPosition();
        // Forward the transport playhead to hosted plugin instances: the
        // graph sets it on THIS Track node (AudioProcessorGraph::NodeOp
        // does processor.setPlayHead per block), but TrackFXSlot is not a
        // graph node, so the plugins inside it never receive a playhead and
        // CLAP transport clock events never fire. Mirror the graph's
        // per-block setPlayHead pattern — cheap pointer assignment, and the
        // playhead behind it always reads live transport state.
        // Diagnostic knob: HDAW_NO_PLAYHEAD_FORWARD=1 disables forwarding
        // (reproduces the pre-forward condition for comparison).
        static const bool noForward = juce::SystemStats::getEnvironmentVariable("HDAW_NO_PLAYHEAD_FORWARD", "") == "1";
        if (pos && !noForward)
        {
            for (const auto& slot : fxChain)
                if (slot)
                    slot->forwardPlayHead(ph);
        }
        if (pos && pos->getIsPlaying())
        {
            double timeSec = pos->getTimeInSeconds().orFallback(0.0);
            if (stateLock.tryEnter())
            {
                for (const auto& am : automationManagers)
                {
                    if (!am) continue;
                    int pid = am->getParamID();

                    if (am->shouldWrite())
                    {
                        float currentVal = 0.0f;
                        if (pid == 1)
                            currentVal = volumeGain.getCurrentValue();
                        else if (pid == 2)
                            currentVal = panPosition.getCurrentValue() * 0.5f + 0.5f;
                        else if (pid == 3)
                            currentVal = isMuted.load() ? 1.0f : 0.0f;
                        else if (pid >= 200)
                        {
                            int si = (pid - 200) / 100;
                            int pi = (pid - 200) % 100;
                            if (si < static_cast<int>(midiFxChain.size()) && midiFxChain[si])
                                currentVal = midiFxChain[si]->getAutomationParam(pi);
                        }
                        else if (pid >= 100)
                        {
                            int si = (pid - 100) / 100;
                            int pi = (pid - 100) % 100;
                            if (si < static_cast<int>(fxChain.size()) && fxChain[si])
                                currentVal = fxChain[si]->getAutomationParam(pi);
                        }
                        am->recordPoint(timeSec, static_cast<double>(currentVal));
                    }
                    else
                    {
                        double value = am->getValueAtTime(timeSec);
                        if (value >= 0.0)
                        {
                            if (pid == 1)
                                volumeGain.setTargetValue(static_cast<float>(value));
                            else if (pid == 2)
                                panPosition.setTargetValue(static_cast<float>(value * 2.0f - 1.0f));
                            else if (pid == 3)
                                isMuted.store(value >= 0.5f);
                            else if (pid >= 200)
                            {
                                int si = (pid - 200) / 100;
                                int pi = (pid - 200) % 100;
                                if (si < static_cast<int>(midiFxChain.size()) && midiFxChain[si])
                                    midiFxChain[si]->setAutomationParam(pi, static_cast<float>(value));
                            }
                            else if (pid >= 100)
                            {
                                int si = (pid - 100) / 100;
                                int pi = (pid - 100) % 100;
                                if (si < static_cast<int>(fxChain.size()) && fxChain[si])
                                    fxChain[si]->setAutomationParam(pi, static_cast<float>(value));
                            }
                        }
                    }
                }
                stateLock.exit();
            }
        }
    }

    // Mute check (after automation so mute can be toggled on/off)
    if (isMuted.load())
    {
        buffer.clear();
        return;
    }

    // Apply MIDI FX (arpeggiator etc.) first, then the audio FX chain
    // (DSP + plugins). The MIDI FX transforms midiMessages so the instrument
    // slot in the audio chain receives the arpeggiated/processed MIDI.
    if (stateLock.tryEnter())
    {
        if (!midiFxChain.empty())
        {
            juce::AudioPlayHead::PositionInfo midiPos;
            bool hasPos = false;
            if (auto* ph = getPlayHead())
            {
                if (auto p = ph->getPosition())
                {
                    midiPos = *p;
                    hasPos = true;
                }
            }
            for (const auto& slot : midiFxChain)
            {
                if (slot)
                {
                    slot->applyAutomation();
                    slot->process(midiMessages, hasPos ? &midiPos : nullptr,
                                  fxSpec.sampleRate, buffer.getNumSamples());
                }
            }
        }
        for (const auto& slot : fxChain)
        {
            if (slot)
            {
                slot->applyAutomation();
                slot->process(buffer, midiMessages);
            }
        }
        stateLock.exit();
    }

    // Volume and pan
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels >= 2)
    {
        float* leftChannel = buffer.getWritePointer(0);
        float* rightChannel = buffer.getWritePointer(1);

        // Get BPM from playhead for beat-synced modulation
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                bpm = pos->getBpm().orFallback(120.0);

        // Modulation read path: collect unique paramIDs from all sources
        // (outside the per-sample loop — no allocation inside the sample
        // loop). Each source targets a paramID; getModulation() sums all
        // sources for that paramID and advances their LFO phases per-sample.
        // rebuildModulation (UI thread) mutates the source vector under
        // stateLock; we tryEnter() once per block and, on contention, skip
        // modulation for the whole block (modGain/modPan stay 0.0). This
        // matches the tryEnter()-or-skip pattern used for automation and
        // the FX chain above, and avoids locking per-sample.
        static constexpr int kMaxModParamIDs = 16;
        int uniqueParamIDs[kMaxModParamIDs] = {};
        int numUniqueParamIDs = 0;
        if (modulationManager)
        {
            const int numSources = modulationManager->getNumSources();
            for (int i = 0; i < numSources && numUniqueParamIDs < kMaxModParamIDs; ++i)
            {
                int pid = modulationManager->getSourceParamID(i);
                if (pid <= 0) continue;
                bool found = false;
                for (int j = 0; j < numUniqueParamIDs; ++j)
                {
                    if (uniqueParamIDs[j] == pid) { found = true; break; }
                }
                if (!found)
                    uniqueParamIDs[numUniqueParamIDs++] = pid;
            }
        }

        const bool modulationLocked = modulationManager && stateLock.tryEnter();

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float baseGain = volumeGain.getNextValue();
            float basePan  = panPosition.getNextValue();

            float modGain = 0.0f, modPan = 0.0f;
            if (modulationLocked)
            {
                for (int pidIdx = 0; pidIdx < numUniqueParamIDs; ++pidIdx)
                {
                    int pid = uniqueParamIDs[pidIdx];
                    float modVal = modulationManager->getModulation(pid, bpm, getSampleRate());
                    if (pid == 1)
                        modGain = modVal;
                    else if (pid == 2)
                        modPan = modVal;
                    else if (pid >= 200)
                    {
                        int si = (pid - 200) / 100;
                        int pi = (pid - 200) % 100;
                        if (si < static_cast<int>(midiFxChain.size()) && midiFxChain[si])
                        {
                            float base = midiFxChain[si]->getAutomationParam(pi);
                            midiFxChain[si]->setAutomationParam(pi,
                                juce::jlimit(0.0f, 1.0f, base + modVal));
                        }
                    }
                    else if (pid >= 100)
                    {
                        int si = (pid - 100) / 100;
                        int pi = (pid - 100) % 100;
                        if (si < static_cast<int>(fxChain.size()) && fxChain[si])
                        {
                            float base = fxChain[si]->getAutomationParam(pi);
                            fxChain[si]->setAutomationParam(pi,
                                juce::jlimit(0.0f, 1.0f, base + modVal));
                        }
                    }
                }
            }

            // Volume modulation is a depth-scaled MULTIPLIER, not an additive
            // offset. The LFO returns a value in [-1,+1] (bipolar) or [0,1]
            // (unipolar) scaled by depth; map it to a gain factor
            // (1 + modGain) so a bipolar LFO sweeps the volume from 0 to 2x
            // and a unipolar LFO from 1x to 2x. The previous `baseGain +
            // modGain` clamped to [0,1] made volume modulation one-sided
            // (could only ever reduce, never boost). Floor at 0 so a full
            // negative bipolar swing silences rather than inverts.
            float gainMul = std::max(1.0f + modGain, 0.0f);
            float currentGain = baseGain * gainMul;
            float currentPan  = std::clamp(basePan  + modPan,  -1.0f, 1.0f);

            // Equal-power pan: pan is in [-1, +1] (the convention used by the
            // mixer/header UI and stored in IDs::pan). Map to an angle and
            // apply cosine/sine gains. The previous law leftGain*(1-pan)/
            // rightGain*pan assumed a [0,1] pan and zeroed one channel at
            // the default center pan (0.0) — causing missing/silent output.
            float panAngle = (currentPan + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);
            float leftGain = currentGain * std::cos(panAngle);
            float rightGain = currentGain * std::sin(panAngle);

            leftChannel[sample] *= leftGain;
            rightChannel[sample] *= rightGain;
        }

        // Release the modulation read lock acquired above (no-op for the
        // block if tryEnter() failed). Must stay paired with the tryEnter().
        if (modulationLocked)
            stateLock.exit();
    }

    meter.update(buffer);

    HDAW::BufferCheck::checkBuffer(buffer, getSampleRate(), trackIndex);
}

void Track::toggleFXEditor(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(fxChain.size()))
    {
        HDAW_LOG("FXEditor", (juce::String("toggleFXEditor: slotIndex=") + juce::String(slotIndex) + " out of range (chain size=" + juce::String((int)fxChain.size()) + ")").toStdString().c_str());
        return;
    }

    auto& slot = fxChain[slotIndex];
    if (!slot || !slot->isPlugin())
    {
        HDAW_LOG("FXEditor", (juce::String("toggleFXEditor: slot ") + juce::String(slotIndex) + " is not a plugin (slot=" + (slot ? "exists" : "null") + " isPlugin=" + (slot && slot->isPlugin() ? "true" : "false") + " type=" + (slot ? slot->getType().toStdString() : "n/a") + ")").toStdString().c_str());
        return;
    }

    if (slot->isEditorOpen())
        slot->closeEditor();
    else
        slot->showEditor();
}

namespace {
// Resolve the track's FX_CHAIN subtree from the project model + track index.
// Returns an invalid ValueTree if the back-pointer isn't set or the track no
// longer exists in the model.
juce::ValueTree resolveFXChainTree(ProjectModel* model, int trackIndex)
{
    if (model == nullptr || trackIndex < 0) return {};
    auto trackList = model->getTrackListTree();
    if (trackIndex >= trackList.getNumChildren()) return {};
    return trackList.getChild(trackIndex).getChildWithName(IDs::FX_CHAIN);
}
} // namespace

int Track::addFXSlotAt(const std::string& type, int pos)
{
    auto fxChainTree = resolveFXChainTree(projectModel, trackIndex);
    if (!fxChainTree.isValid()) return -1;

    const int n = fxChainTree.getNumChildren();
    int insertIdx = (pos < 0 || pos > n) ? n : pos;

    juce::ValueTree slot(IDs::FX_SLOT);
    slot.setProperty(IDs::fxType, juce::String(type), nullptr);
    slot.setProperty(IDs::bypassed, false, nullptr);
    fxChainTree.addChild(slot, insertIdx, nullptr);

    // rebuildFXChain closes plugin editors before clearing the chain. For
    // a brand-new "plugin"-type slot with no pluginID yet, this will produce
    // a "none" placeholder until setFXSlotPluginID is called.
    rebuildFXChain(fxChainTree);
    return insertIdx;
}

void Track::setFXSlotPluginID(int slotIndex, const std::string& pluginID)
{
    auto fxChainTree = resolveFXChainTree(projectModel, trackIndex);
    if (!fxChainTree.isValid()) return;
    if (slotIndex < 0 || slotIndex >= fxChainTree.getNumChildren()) return;

    const juce::String jid(pluginID);

    // Best-effort format lookup so rebuildFXChain knows which plugin format to
    // instantiate. If we don't find it the slot becomes a "none" placeholder
    // (matches the existing behaviour for unknown formats).
    juce::String format;
    juce::String resolvedID = jid; // may be overridden to the file path
    if (pluginManager != nullptr)
    {
        for (const auto& p : pluginManager->getPlugins())
        {
            if (p.fileOrIdentifier == jid || p.createIdentifierString() == jid)
            {
                format = p.pluginFormatName;
                resolvedID = p.fileOrIdentifier; // always store the file path, not the identifier string
                break;
            }
        }
        if (format.isEmpty())
        {
            juce::Logger::writeToLog("HDAW: setFXSlotPluginID could not resolve format for "
                + jid + " (plugin may not be in the scan cache)");
        }
    }

    auto slot = fxChainTree.getChild(slotIndex);
    slot.setProperty(IDs::fxType, juce::String("plugin"), nullptr);
    slot.setProperty(IDs::pluginID, resolvedID, nullptr);
    slot.setProperty(IDs::pluginFormat, format, nullptr);

    rebuildFXChain(fxChainTree);
}

void Track::removeFXSlot(int slotIndex)
{
    auto fxChainTree = resolveFXChainTree(projectModel, trackIndex);
    if (!fxChainTree.isValid()) return;
    if (slotIndex < 0 || slotIndex >= fxChainTree.getNumChildren()) return;

    fxChainTree.removeChild(slotIndex, nullptr);
    rebuildFXChain(fxChainTree);
}

void Track::setFXBypassed(int slotIndex, bool bypassed)
{
    // Hold the lock so a concurrent rebuildFXChain can't invalidate the
    // fxChain vector while we're indexing into it. The audio thread uses
    // tryEnter, so it just skips if the lock is held.
    juce::SpinLock::ScopedLockType lock(stateLock);

    if (slotIndex < 0 || slotIndex >= static_cast<int>(fxChain.size()) || !fxChain[slotIndex])
        return;
    fxChain[slotIndex]->setBypassed(bypassed);

    auto fxChainTree = resolveFXChainTree(projectModel, trackIndex);
    if (!fxChainTree.isValid()) return;
    if (slotIndex < 0 || slotIndex >= fxChainTree.getNumChildren()) return;
    fxChainTree.getChild(slotIndex).setProperty(IDs::bypassed, bypassed, nullptr);
}

void Track::setFxSlotInternalParam(int slotIndex, int paramIndex, float value)
{
    const juce::SpinLock::ScopedLockType lock(stateLock);
    if (slotIndex < 0 || slotIndex >= static_cast<int>(fxChain.size()))
        return;
    if (fxChain[static_cast<size_t>(slotIndex)])
        fxChain[static_cast<size_t>(slotIndex)]->setInternalParam(paramIndex, value);
}

void Track::setVolume(float newVolume)
{
    volumeGain.setTargetValue(newVolume);
}

void Track::setPan(float newPan)
{
    panPosition.setTargetValue(newPan);
}

} // namespace HDAW
