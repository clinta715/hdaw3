#include "CLAPPluginFormat.h"
#include "CLAPPluginInstance.h"
#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// ── CLAPModule ──────────────────────────────────────────────────

bool CLAPModule::load(const juce::String& path)
{
    unload();
    loadedPath = path;

#if JUCE_WINDOWS
    auto* wPath = static_cast<const wchar_t*>(path.toWideCharPointer());
    handle = ::LoadLibraryExW(wPath, nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                              LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle == nullptr)
        return false;

    auto* entryPtr = reinterpret_cast<clap_plugin_entry_t*>(
        ::GetProcAddress(static_cast<HMODULE>(handle), "clap_entry"));
    if (entryPtr == nullptr) { ::FreeLibrary(static_cast<HMODULE>(handle)); handle = nullptr; return false; }
    entry = entryPtr;
#else
    handle = ::dlopen(path.toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
        return false;

    auto* entryPtr = reinterpret_cast<clap_plugin_entry_t*>(
        ::dlsym(handle, "clap_entry"));
    if (entryPtr == nullptr) { ::dlclose(handle); handle = nullptr; return false; }
    entry = entryPtr;
#endif

    if (CLAP_VERSION_LT(1, 0, 0))
    {
        unload();
        return false;
    }

    if (!entry->init(path.toRawUTF8()))
    {
        unload();
        return false;
    }
    initialized = true;

    factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr)
    {
        unload();
        return false;
    }

    return true;
}

void CLAPModule::unload()
{
    if (initialized && entry != nullptr)
        entry->deinit();
    initialized = false;
    entry = nullptr;
    factory = nullptr;

#if JUCE_WINDOWS
    if (handle != nullptr)
    {
        ::FreeLibrary(static_cast<HMODULE>(handle));
        handle = nullptr;
    }
#else
    if (handle != nullptr)
    {
        ::dlclose(handle);
        handle = nullptr;
    }
#endif
}

// ── CLAPPluginFormat ────────────────────────────────────────────

void CLAPPluginFormat::findAllTypesForFile(
    juce::OwnedArray<juce::PluginDescription>& results,
    const juce::String& fileOrIdentifier)
{
    CLAPModule module;
    if (!module.load(fileOrIdentifier))
        return;

    auto* factory = module.factory;
    uint32_t count = factory->get_plugin_count(factory);

    auto modTime = juce::File(fileOrIdentifier).getLastModificationTime();
    auto now = juce::Time::getCurrentTime();

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* desc = factory->get_plugin_descriptor(factory, i);
        if (desc == nullptr)
            continue;

        auto pd = std::make_unique<juce::PluginDescription>();
        pd->name = juce::String(desc->name);
        pd->descriptiveName = juce::String(desc->name);
        pd->pluginFormatName = "CLAP";
        pd->manufacturerName = juce::String(desc->vendor ? desc->vendor : "");
        pd->version = juce::String(desc->version ? desc->version : "1.0.0");
        pd->fileOrIdentifier = fileOrIdentifier;
        pd->lastFileModTime = modTime;
        pd->lastInfoUpdateTime = now;
        pd->uniqueId = static_cast<int>(std::hash<juce::String>{}(fileOrIdentifier + "_" + juce::String(desc->id)));
        pd->isInstrument = false;
        pd->numInputChannels = 0;
        pd->numOutputChannels = 0;

        // Check features for instrument flag
        if (desc->features)
        {
            const auto* features = desc->features;
            while (*features != nullptr)
            {
                if (std::strcmp(*features, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0)
                    pd->isInstrument = true;
                ++features;
            }
        }

        pd->category = "CLAP";

        results.add(std::move(pd));
    }
}

bool CLAPPluginFormat::fileMightContainThisPluginType(
    const juce::String& fileOrIdentifier)
{
    return fileOrIdentifier.toLowerCase().endsWith(".clap");
}

juce::String CLAPPluginFormat::getNameOfPluginFromIdentifier(
    const juce::String& fileOrIdentifier)
{
    return juce::File(fileOrIdentifier).getFileNameWithoutExtension();
}

bool CLAPPluginFormat::pluginNeedsRescanning(
    const juce::PluginDescription& desc)
{
    auto f = juce::File(desc.fileOrIdentifier);
    return !f.exists() || f.getLastModificationTime() != desc.lastFileModTime;
}

bool CLAPPluginFormat::doesPluginStillExist(
    const juce::PluginDescription& desc)
{
    return juce::File(desc.fileOrIdentifier).exists();
}

juce::StringArray CLAPPluginFormat::searchPathsForPlugins(
    const juce::FileSearchPath& directoriesToSearch,
    bool recursive, bool)
{
    juce::StringArray results;

    for (int i = 0; i < directoriesToSearch.getNumPaths(); ++i)
    {
        auto dir = directoriesToSearch[i];
        if (!dir.isDirectory())
            continue;

        for (auto entry : juce::RangedDirectoryIterator(
                 dir, recursive, "*.clap", juce::File::findFiles))
            results.add(entry.getFile().getFullPathName());
    }

    return results;
}

juce::FileSearchPath CLAPPluginFormat::getDefaultLocationsToSearch()
{
    juce::FileSearchPath paths;

#if JUCE_WINDOWS
    auto prog = juce::File::getSpecialLocation(
        juce::File::globalApplicationsDirectory);
    paths.add(prog.getChildFile("Common Files\\CLAP"));
    paths.add(juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory)
        .getChildFile("CLAP"));
#elif JUCE_MAC
    paths.add("/Library/Audio/Plug-Ins/CLAP");
    paths.add("~/Library/Audio/Plug-Ins/CLAP");
#elif JUCE_LINUX
    paths.add("/usr/lib/clap");
    paths.add("/usr/local/lib/clap");
    paths.add("~/.clap");
#endif

    // CLAP_PATH env var
    auto envPath = juce::SystemStats::getEnvironmentVariable("CLAP_PATH", {});
    if (envPath.isNotEmpty())
    {
        juce::StringArray dirs;
        dirs.addTokens(envPath, juce::StringRef(
#if JUCE_WINDOWS
            ";"
#else
            ":"
#endif
        ), {});
        for (const auto& d : dirs)
            paths.add(juce::File(d.trim()));
    }

    return paths;
}

void CLAPPluginFormat::createPluginInstance(
    const juce::PluginDescription& desc,
    double sampleRate, int blockSize,
    PluginCreationCallback callback)
{
    juce::String error;

    auto module = std::make_shared<CLAPModule>();
    if (!module->load(desc.fileOrIdentifier))
    {
        callback(nullptr, "Failed to load CLAP module: " + desc.fileOrIdentifier);
        return;
    }

    // Find the matching plugin ID in the factory
    auto* factory = module->factory;
    uint32_t count = factory->get_plugin_count(factory);
    const char* pluginID = nullptr;

    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* d = factory->get_plugin_descriptor(factory, i);
        if (d != nullptr && desc.name == juce::String(d->name))
        {
            pluginID = d->id;
            break;
        }
    }

    if (pluginID == nullptr)
    {
        callback(nullptr, "Plugin not found in CLAP module: " + desc.name);
        return;
    }

    // Create host first (instance pointer set after construction)
    auto host = std::make_unique<CLAPHost>(nullptr);
    const auto* clapHost = host->getClapHost();

    const auto* plugin = factory->create_plugin(factory, clapHost, pluginID);
    if (plugin == nullptr)
    {
        callback(nullptr, "Failed to create CLAP plugin instance");
        return;
    }

    if (!plugin->init(plugin))
    {
        plugin->destroy(plugin);
        callback(nullptr, "CLAP plugin init failed");
        return;
    }

    auto instance = std::make_unique<CLAPPluginInstance>(
        module, plugin, std::move(host), sampleRate, blockSize);
    instance->initialize();

    callback(std::move(instance), {});
}
