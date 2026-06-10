#include "Track.h"

namespace HDAW {

Track::Track()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    volumeGain.setCurrentAndTargetValue(1.0f);
    panPosition.setCurrentAndTargetValue(0.5f);
}

Track::~Track() = default;

void Track::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    volumeGain.reset(sampleRate, 0.05);
    panPosition.reset(sampleRate, 0.05);

    fxSpec.sampleRate = sampleRate;
    fxSpec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    fxSpec.numChannels = 2;

    for (const auto& slot : fxChain)
        if (slot)
            slot->prepare(fxSpec);

    for (const auto& am : automationManagers)
        if (am)
            am->rebuildCache();
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
    for (const auto& slot : fxChain)
        if (slot)
            slot->reset();
}

void Track::rebuildFXChain(const juce::ValueTree& fxChainTree)
{
    juce::SpinLock::ScopedLockType lock(stateLock);

    // Save plugin state from existing slots before clearing
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
                    auto child = fxChainTree.getChild(i);
                    if (child.getProperty(IDs::pluginID).toString() == slot->getPluginID())
                    {
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
            continue;

        if (type == "plugin")
        {
            juce::String pluginID = slotTree.getProperty(IDs::pluginID).toString();
            juce::String pluginFormat = slotTree.getProperty(IDs::pluginFormat).toString();
            if (pluginID.isEmpty())
                continue;

            juce::PluginDescription desc;
            desc.fileOrIdentifier = pluginID;
            if (pluginFormat == "VST3")
                desc.pluginFormatName = "VST3";
            else
                continue;

            juce::String error;
            auto plugin = pluginManager != nullptr
                ? pluginManager->createPluginInstance(desc, error, getSampleRate(), getBlockSize())
                : nullptr;

            if (plugin != nullptr)
            {
                auto slot = std::make_unique<TrackFXSlot>(std::move(plugin), pluginID);
                slot->setBypassed(slotTree.getProperty(IDs::bypassed));

                juce::String stateStr = slotTree.getProperty(IDs::pluginState).toString();
                if (stateStr.isNotEmpty())
                {
                    juce::MemoryBlock state;
                    if (state.fromBase64Encoding(stateStr))
                        slot->getPluginInstance()->setStateInformation(state.getData(),
                            static_cast<int>(state.getSize()));
                }

                if (fxSpec.sampleRate > 0)
                    slot->prepare(fxSpec);

                fxChain.push_back(std::move(slot));
            }
            else if (error.isNotEmpty())
            {
                juce::Logger::writeToLog("HDAW: Failed to load plugin " + pluginID + ": " + error);
            }
            continue;
        }

        auto slot = std::make_unique<TrackFXSlot>(type);
        slot->setBypassed(slotTree.getProperty(IDs::bypassed));

        if (fxSpec.sampleRate > 0)
            slot->prepare(fxSpec);

        fxChain.push_back(std::move(slot));
    }
}

void Track::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);

    if (isMuted.load())
    {
        buffer.clear();
        return;
    }

    if (auto* ph = getPlayHead())
    {
        auto pos = ph->getPosition();
        if (pos && pos->getIsPlaying())
        {
            double timeSec = pos->getTimeInSeconds().orFallback(0.0);
            if (stateLock.tryEnter())
            {
                for (const auto& am : automationManagers)
                {
                    if (!am) continue;
                    double value = am->getValueAtTime(timeSec);
                    if (value >= 0.0)
                    {
                        int pid = am->getParamID();
                        if (pid == 1)
                            volumeGain.setTargetValue(static_cast<float>(value));
                        else if (pid == 2)
                            panPosition.setTargetValue(static_cast<float>(value));
                    }
                }
                stateLock.exit();
            }
        }
    }

    // Apply FX chain (DSP + plugins)
    if (stateLock.tryEnter())
    {
        for (const auto& slot : fxChain)
            if (slot)
                slot->process(buffer, midiMessages);
        stateLock.exit();
    }

    // Volume and pan
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels >= 2)
    {
        float* leftChannel = buffer.getWritePointer(0);
        float* rightChannel = buffer.getWritePointer(1);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float currentGain = volumeGain.getNextValue();
            float currentPan = panPosition.getNextValue();

            float leftGain = currentGain * (1.0f - currentPan);
            float rightGain = currentGain * currentPan;

            leftChannel[sample] *= leftGain;
            rightChannel[sample] *= rightGain;
        }
    }

    meter.update(buffer);
}

void Track::toggleFXEditor(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(fxChain.size()))
        return;

    auto& slot = fxChain[slotIndex];
    if (!slot || !slot->isPlugin())
        return;

    if (slot->isEditorOpen())
        slot->closeEditor();
    else
        slot->showEditor();
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
