#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include "CLAPPluginFormat.h"
#include "CrashRecoveryManager.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>

namespace proxy { class PluginProxySlot; }

namespace HDAW {

class PluginManager : private juce::Timer
{
public:
    PluginManager();
    ~PluginManager();

    bool isolationEnabled = true;

    std::atomic<uint32_t> nextProxySlotId{ 1 };

    void setGraphLock(juce::SpinLock* lock) { graphLockPtr = lock; }

    CrashRecoveryManager& recovery() { return *crashRecovery; }

    using ScanProgressCallback = std::function<void(const juce::String& fileName, int completed, int total)>;

    void scanAll(ScanProgressCallback progressCb = nullptr);
    void abortScan() { abortRequested.store(true); }
    bool isLoading() const { return scanning.load(); }
    int getLastScanCrashCount() const { return lastScanCrashCount; }

    const std::vector<juce::PluginDescription>& getPlugins() const { return knownPlugins; }

    // Returns the default VST3/CLAP directories for the current platform.
    static juce::StringArray getVst3Dirs();
    static juce::StringArray getClapDirs();

    std::vector<juce::PluginDescription> getInstrumentPlugins() const;
    std::vector<juce::PluginDescription> getEffectPlugins() const;

    std::unique_ptr<juce::AudioPluginInstance> createPluginInstance(
        const juce::PluginDescription& desc, juce::String& errorMessage,
        double sampleRate = 44100.0, int blockSize = 512,
        bool isolated = false);

    void loadCache();
    void saveCache();

    // Blacklist
    bool isBlacklisted(const juce::String& pluginID) const;
    void blacklistPlugin(const juce::String& pluginID);
    void blacklistPlugin(const juce::String& pluginID, const juce::String& reason);
    void unblacklistPlugin(const juce::String& pluginID);
    const std::vector<juce::String>& getBlacklistedIDs() const { return blacklistedIDs; }
    juce::String getBlacklistReason(const juce::String& pluginID) const;
    void loadBlacklist();
    void saveBlacklist();

    using ScanCallback = std::function<void()>;
    void setScanCompleteCallback(ScanCallback cb) { scanCallback = cb; }
    ScanCallback getScanCompleteCallback() const { return scanCallback; }

    juce::Array<juce::File> findPluginFiles(const juce::StringArray& dirs);

    bool respawnIsolatedSlot(uint32_t oldSlotId, const juce::String& pluginPath);

    void killProxyForTesting(uint32_t slotId);

private:
    void onScanFinished();
    void timerCallback() override;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;
    std::vector<juce::PluginDescription> knownPlugins;
    std::atomic<bool> scanning{false};
    std::atomic<bool> abortRequested{false};
    ScanCallback scanCallback;
    juce::File cacheFile;

    std::vector<juce::String> blacklistedIDs;
    std::unordered_map<juce::String, juce::String> blacklistReasons;
    juce::File blacklistFile;

    // Isolated scanning
    juce::File scannerExePath;
    struct ScanResult { bool ok; bool isInstrument = false; int uid = 0; juce::String name, manufacturer, category, format, file, id, error; };
    ScanResult scanPluginIsolated(const juce::String& pluginPath);
    int lastScanCrashCount = 0;

    std::unique_ptr<CrashRecoveryManager> crashRecovery;
    std::unordered_map<uint32_t, proxy::PluginProxySlot*> liveProxySlots;
    juce::SpinLock* graphLockPtr = nullptr;
    double lastSampleRate = 44100.0;
    int lastBlockSize = 512;
};

} // namespace HDAW
