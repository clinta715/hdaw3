#include "PluginServiceImpl.h"
#include "PluginManager.h"

PluginServiceImpl::PluginServiceImpl(HDAW::PluginManager& mgr) : mgr_(mgr) {}
PluginServiceImpl::~PluginServiceImpl() = default;

PluginInfo PluginServiceImpl::toPluginInfo(const juce::PluginDescription& desc)
{
    PluginInfo p;
    p.name = desc.name.toStdString();
    p.format = desc.pluginFormatName.toStdString();
    p.manufacturer = desc.manufacturerName.toStdString();
    p.fileOrIdentifier = desc.fileOrIdentifier.toStdString();
    p.isInstrument = desc.isInstrument;
    return p;
}

void PluginServiceImpl::scanAll()
{
    mgr_.scanAll();
}

bool PluginServiceImpl::isLoading() const
{
    return mgr_.isLoading();
}

std::vector<PluginInfo> PluginServiceImpl::getPlugins() const
{
    std::vector<PluginInfo> result;
    for (const auto& desc : mgr_.getPlugins())
        result.push_back(toPluginInfo(desc));
    return result;
}

std::vector<PluginInfo> PluginServiceImpl::getInstrumentPlugins() const
{
    std::vector<PluginInfo> result;
    auto plugins = mgr_.getInstrumentPlugins();
    for (const auto& desc : plugins)
        result.push_back(toPluginInfo(desc));
    return result;
}

std::vector<PluginInfo> PluginServiceImpl::getEffectPlugins() const
{
    std::vector<PluginInfo> result;
    auto plugins = mgr_.getEffectPlugins();
    for (const auto& desc : plugins)
        result.push_back(toPluginInfo(desc));
    return result;
}

bool PluginServiceImpl::isBlacklisted(const std::string& pluginID) const
{
    return mgr_.isBlacklisted(juce::String(pluginID));
}

void PluginServiceImpl::blacklistPlugin(const std::string& pluginID)
{
    mgr_.blacklistPlugin(juce::String(pluginID));
}

void PluginServiceImpl::unblacklistPlugin(const std::string& pluginID)
{
    mgr_.unblacklistPlugin(juce::String(pluginID));
}

std::string PluginServiceImpl::getBlacklistReason(const std::string& pluginID) const
{
    return mgr_.getBlacklistReason(juce::String(pluginID)).toStdString();
}

void PluginServiceImpl::setScanCompleteCallback(ScanCallback cb)
{
    mgr_.setScanCompleteCallback(std::move(cb));
}

PluginService::ScanCallback PluginServiceImpl::getScanCompleteCallback() const
{
    return mgr_.getScanCompleteCallback();
}
