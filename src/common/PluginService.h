#pragma once
#include <string>
#include <vector>
#include <functional>

struct PluginInfo {
    std::string name;
    std::string format;
    std::string manufacturer;
    std::string fileOrIdentifier;
    bool isInstrument = false;
};

class PluginService {
public:
    virtual ~PluginService() = default;

    // Scanning
    virtual void scanAll() = 0;
    virtual bool isLoading() const = 0;

    using ScanProgressCallback = std::function<void(const std::string& fileName, int completed, int total)>;
    virtual void scanAll([[maybe_unused]] ScanProgressCallback progressCb) { scanAll(); }

    // Plugin inventory
    virtual std::vector<PluginInfo> getPlugins() const = 0;
    virtual std::vector<PluginInfo> getInstrumentPlugins() const = 0;
    virtual std::vector<PluginInfo> getEffectPlugins() const = 0;

    // Blacklist
    virtual bool isBlacklisted(const std::string& pluginID) const = 0;
    virtual void blacklistPlugin(const std::string& pluginID) = 0;
    virtual void unblacklistPlugin(const std::string& pluginID) = 0;
    virtual std::string getBlacklistReason(const std::string& pluginID) const = 0;

    // Custom scan paths
    virtual std::vector<std::string> getCustomScanDirs() const = 0;
    virtual void addCustomScanDir(const std::string& dir) = 0;
    virtual void removeCustomScanDir(const std::string& dir) = 0;

    // Preset search
    struct PresetSearchResult {
        std::string pluginId;
        std::string pluginName;
        int presetIndex = 0;
        std::string presetName;
    };
    virtual std::vector<PresetSearchResult> searchPresets(const std::string& query, int limit = 50) const = 0;

    // Scan-complete notification
    using ScanCallback = std::function<void()>;
    virtual void setScanCompleteCallback(ScanCallback cb) = 0;
    virtual ScanCallback getScanCompleteCallback() const = 0;
};
