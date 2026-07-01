#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>

// CLAP format is compiled into HDAW_lib; the scanner links against it.
#include "engine/CLAPPluginFormat.h"

static const char* parseArg(int argc, char** argv, const char* prefix)
{
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], prefix, strlen(prefix)) == 0)
            return argv[i] + strlen(prefix);
    }
    return nullptr;
}

int main(int argc, char* argv[])
{
    const char* pluginPath = parseArg(argc, argv, "--plugin=");
    const char* pedalFile  = parseArg(argc, argv, "--pedal-file=");

    if (!pluginPath || !pedalFile) {
        std::cerr << "Usage: hdaw_plugin_scanner --plugin=PATH --pedal-file=PATH" << std::endl;
        return 1;
    }

    // Write plugin path to dead-man's-pedal BEFORE attempting load.
    // If we crash, the parent reads this to identify the culprit.
    {
        std::ofstream ofs(pedalFile, std::ios::trunc);
        ofs << pluginPath;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioPluginFormatManager fmtMgr;
    fmtMgr.addFormat(new juce::VST3PluginFormat());
    fmtMgr.addFormat(new CLAPPluginFormat());

    juce::String pluginStr(pluginPath);
    juce::String error;

    // Find which format handles this file
    for (auto* fmt : fmtMgr.getFormats()) {
        if (!fmt->fileMightContainThisPluginType(pluginStr))
            continue;

        juce::PluginDescription desc;
        desc.fileOrIdentifier = pluginStr;
        desc.pluginFormatName = fmt->getName();

        auto instance = fmtMgr.createPluginInstance(desc, 44100.0, 512, error);
        if (instance) {
            // Build JSON via JUCE to handle escaping properly
            auto* obj = new juce::DynamicObject();
            obj->setProperty("ok", true);
            obj->setProperty("name", desc.name);
            obj->setProperty("manufacturer", desc.manufacturerName);
            obj->setProperty("category", desc.category);
            obj->setProperty("format", desc.pluginFormatName);
            obj->setProperty("file", desc.fileOrIdentifier);
            obj->setProperty("id", desc.createIdentifierString());
            std::cout << juce::JSON::toString(juce::var(obj)) << std::endl;

            // Clear pedal on success
            std::ofstream ofs(pedalFile, std::ios::trunc);
            ofs << "";
            return 0;
        }
    }

    // Load failed (not a crash — just couldn't instantiate)
    std::cerr << "Failed to load plugin: " << error.toRawUTF8() << std::endl;
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("ok", false);
        obj->setProperty("error", error);
        std::cout << juce::JSON::toString(juce::var(obj)) << std::endl;
    }

    // Clear pedal — this wasn't a crash
    std::ofstream ofs(pedalFile, std::ios::trunc);
    ofs << "";
    return 1;
}
