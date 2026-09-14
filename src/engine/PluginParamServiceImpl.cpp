#include "PluginParamServiceImpl.h"
#include "MainAudioProcessor.h"
#include "Track.h"
#include "CLAPPluginInstance.h"
#include "../proxy/PluginProxySlot.h"

PluginParamServiceImpl::PluginParamServiceImpl(MainAudioProcessor& proc)
    : proc_(proc) {}

PluginParamServiceImpl::~PluginParamServiceImpl() = default;

juce::AudioPluginInstance* PluginParamServiceImpl::resolveInstance(int trackIndex, const std::string& pluginID) const
{
    auto* track = proc_.getTrack(trackIndex);
    if (track == nullptr) return nullptr;
    auto& fxChain = track->getFXChain();
    for (const auto& slot : fxChain)
    {
        if (slot && slot->isPlugin() && slot->getPluginID() == juce::String(pluginID))
            return slot->getPluginInstance();
    }
    return nullptr;
}

std::vector<PluginParamSnapshot> PluginParamServiceImpl::getParams(int trackIndex, const std::string& pluginID)
{
    auto* instance = resolveInstance(trackIndex, pluginID);
    if (instance == nullptr) return {};

    auto& params = instance->getParameters();
    std::vector<PluginParamSnapshot> result;
    result.reserve(params.size());
    for (int i = 0; i < params.size(); ++i)
    {
        auto* p = params[i];
        PluginParamSnapshot snap;
        snap.index = i;
        snap.name = p->getName(128).toStdString();
        snap.value = p->getValue();
        snap.text = p->getText(snap.value, 128).toStdString();
        snap.label = p->getLabel().toStdString();
        snap.automatable = p->isAutomatable();
        // In-process CLAP: full range metadata from clap_param_info_t.
        // Other formats (VST3 via JUCE) expose no ranges: hasRange stays
        // false and consumers must treat value as blind normalized.
        if (auto* clap = dynamic_cast<CLAPParameter*>(p))
        {
            // Degenerate ranges (min==max: meters, triggers) carry no
            // mapping info — same guard as CLAPParameter::getValue.
            if (clap->getMaxValue() > clap->getMinValue())
            {
                snap.hasRange = true;
                snap.minVal = clap->getMinValue();
                snap.maxVal = clap->getMaxValue();
                snap.defaultVal = clap->getDefaultPlainValue();
                snap.plainValue = clap->getPlainValue();
                snap.stepped = clap->isStepped();
            }
        }
        else if (auto* prox = dynamic_cast<proxy::ProxiedParameter*>(p))
        {
            // Isolated CLAP: range metadata arrived via GET_PARAM_INFO.
            if (prox->hasRangeInfo())
            {
                snap.hasRange = true;
                snap.minVal = prox->rangeMin();
                snap.maxVal = prox->rangeMax();
                snap.defaultVal = prox->rangeDefault();
                const double range = snap.maxVal - snap.minVal;
                snap.plainValue = range > 0.0
                    ? snap.minVal + static_cast<double>(snap.value) * range
                    : snap.minVal;
                snap.stepped = prox->isSteppedInfo();
            }
        }
        result.push_back(std::move(snap));
    }
    return result;
}

std::string PluginParamServiceImpl::getParamText(int trackIndex, const std::string& pluginID, int paramIndex, float normalizedValue)
{
    auto* instance = resolveInstance(trackIndex, pluginID);
    if (instance == nullptr) return {};
    auto& params = instance->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return {};
    auto* p = params[paramIndex];
    auto text = p->getText(normalizedValue, 128);
    auto label = p->getLabel();
    if (label.isNotEmpty())
        text += " " + label;
    return text.toStdString();
}

void PluginParamServiceImpl::setParam(int trackIndex, const std::string& pluginID, int paramIndex, float normalizedValue)
{
    auto* instance = resolveInstance(trackIndex, pluginID);
    if (instance == nullptr) return;
    auto& params = instance->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return;
    auto* p = params[paramIndex];
    p->beginChangeGesture();
    p->setValueNotifyingHost(normalizedValue);
    p->endChangeGesture();
}

void PluginParamServiceImpl::clearCallback(int trackIndex, const std::string& pluginID)
{
    std::lock_guard<std::mutex> lock(cbMutex);
    for (auto it = callbacks.begin(); it != callbacks.end(); ++it)
    {
        if (it->trackIndex == trackIndex && it->pluginID == pluginID)
        {
            // Remove the JUCE listener from the instance before destroying
            auto* instance = resolveInstance(it->trackIndex, it->pluginID);
            if (instance && it->listener)
                instance->removeListener(it->listener.get());
            callbacks.erase(it);
            return;
        }
    }
}

void PluginParamServiceImpl::setParamChangeCallback(int trackIndex, const std::string& pluginID, ParamChangeCallback cb)
{
    // Clear any existing callback for this key first
    clearCallback(trackIndex, pluginID);

    if (!cb)
        return;  // just clearing, already done above

    auto* instance = resolveInstance(trackIndex, pluginID);
    if (instance == nullptr) return;

    auto listener = std::make_unique<InternalListener>();
    listener->onChanged = std::move(cb);
    auto* raw = listener.get();
    instance->addListener(raw);

    std::lock_guard<std::mutex> lock(cbMutex);
    callbacks.push_back({trackIndex, pluginID, std::move(listener)});
}

int PluginParamServiceImpl::getProgramCount(int trackIndex, const std::string& pluginID)
{
    auto* instance = resolveInstance(trackIndex, pluginID);
    return instance ? instance->getNumPrograms() : 0;
}

int PluginParamServiceImpl::getCurrentProgram(int trackIndex, const std::string& pluginID)
{
    auto* instance = resolveInstance(trackIndex, pluginID);
    return instance ? instance->getCurrentProgram() : 0;
}

std::string PluginParamServiceImpl::getProgramName(int trackIndex, const std::string& pluginID, int programIndex)
{
    auto* instance = resolveInstance(trackIndex, pluginID);
    return instance ? instance->getProgramName(programIndex).toStdString() : std::string{};
}

void PluginParamServiceImpl::setCurrentProgram(int trackIndex, const std::string& pluginID, int programIndex)
{
    auto* instance = resolveInstance(trackIndex, pluginID);
    if (instance) instance->setCurrentProgram(programIndex);
}
