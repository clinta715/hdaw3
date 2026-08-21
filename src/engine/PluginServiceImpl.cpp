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

void PluginServiceImpl::scanAll(ScanProgressCallback progressCb)
{
    mgr_.scanAll([progressCb](const juce::String& fileName, int completed, int total) {
        if (progressCb)
            progressCb(fileName.toStdString(), completed, total);
    });
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

std::vector<std::string> PluginServiceImpl::getCustomScanDirs() const
{
    std::vector<std::string> result;
    for (const auto& d : mgr_.getCustomScanDirs())
        result.push_back(d.toStdString());
    return result;
}

void PluginServiceImpl::addCustomScanDir(const std::string& dir)
{
    mgr_.addCustomScanDir(juce::String(dir));
}

void PluginServiceImpl::removeCustomScanDir(const std::string& dir)
{
    mgr_.removeCustomScanDir(juce::String(dir));
}

std::vector<PluginService::PresetSearchResult> PluginServiceImpl::searchPresets(const std::string& query, int limit) const
{
    std::vector<PresetSearchResult> result;
    if (query.empty()) return result;
    if (limit < 1) limit = 1;
    if (limit > 200) limit = 200;
    std::string queryLower = query;
    for (auto& c : queryLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& pd : mgr_.getPlugins()) {
        if (static_cast<int>(result.size()) >= limit) break;
        juce::String pluginId = pd.createIdentifierString();
        auto* presetInfo = mgr_.getPresetInfo(pluginId);
        if (!presetInfo || presetInfo->numPrograms <= 1) continue;
        std::string pluginName = pd.name.toStdString();
        for (int i = 0; i < presetInfo->numPrograms; ++i) {
            if (static_cast<int>(result.size()) >= limit) break;
            juce::String name = i < presetInfo->programNames.size()
                ? presetInfo->programNames[i]
                : juce::String("Preset ") + juce::String(i);
            std::string presetName = name.toStdString();
            std::string presetNameLower = presetName;
            for (auto& c : presetNameLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (presetNameLower.find(queryLower) != std::string::npos) {
                result.push_back({pluginId.toStdString(), pluginName, i, presetName});
            }
        }
    }
    return result;
}

void PluginServiceImpl::setScanCompleteCallback(ScanCallback cb)
{
    mgr_.setScanCompleteCallback(std::move(cb));
}

PluginService::ScanCallback PluginServiceImpl::getScanCompleteCallback() const
{
    return mgr_.getScanCompleteCallback();
}
