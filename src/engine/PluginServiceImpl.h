#pragma once
#include "../common/PluginService.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace HDAW { class PluginManager; }

class PluginServiceImpl : public PluginService {
public:
    explicit PluginServiceImpl(HDAW::PluginManager& mgr);
    ~PluginServiceImpl() override;

    void scanAll() override;
    bool isLoading() const override;

    std::vector<PluginInfo> getPlugins() const override;
    std::vector<PluginInfo> getInstrumentPlugins() const override;
    std::vector<PluginInfo> getEffectPlugins() const override;

    bool isBlacklisted(const std::string& pluginID) const override;
    void blacklistPlugin(const std::string& pluginID) override;
    void unblacklistPlugin(const std::string& pluginID) override;
    std::string getBlacklistReason(const std::string& pluginID) const override;

    void setScanCompleteCallback(ScanCallback cb) override;
    ScanCallback getScanCompleteCallback() const override;

private:
    static PluginInfo toPluginInfo(const juce::PluginDescription& desc);
    HDAW::PluginManager& mgr_;
};
