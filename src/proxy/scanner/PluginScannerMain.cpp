#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>

#if _WIN32
#include <windows.h>
#include <crtdbg.h>
#include <eh.h>
#endif

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

#if _WIN32
// SEH exception translated to a C++ exception for catch().
// This lets us use try/catch around code that creates C++ objects
// with destructors (which __try/__except forbids).
struct SehException {
    unsigned int code;
    EXCEPTION_POINTERS* info;
};

static void seTranslator(unsigned int code, EXCEPTION_POINTERS* info)
{
    throw SehException{code, info};
}

static LONG WINAPI scannerExceptionFilter(EXCEPTION_POINTERS*)
{
    _Exit(3);
}

static void scannerInvalidParameterHandler(const wchar_t*, const wchar_t*,
                                           const wchar_t*, unsigned int, uintptr_t)
{
    _Exit(4);
}

static void scannerPurecallHandler()
{
    _Exit(5);
}

static void configureCrashProtection()
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler(scannerInvalidParameterHandler);
    _set_purecall_handler(scannerPurecallHandler);
    SetUnhandledExceptionFilter(scannerExceptionFilter);
    _set_se_translator(seTranslator);
}
#endif

// Core scanner logic. Separated from main() so the JUCE init object
// has a clear scope. SEH exceptions are translated to C++ exceptions
// via _set_se_translator so we can use try/catch with destructors.
// Returns: 0 = success, 1 = load failed, 2 = crash during load.
static int scanPlugin(const char* pluginPath, const char* pedalFile)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        juce::AudioPluginFormatManager fmtMgr;
        fmtMgr.addFormat(new juce::VST3PluginFormat());
        fmtMgr.addFormat(new CLAPPluginFormat());

        juce::String pluginStr(pluginPath);
        juce::String error;

        for (auto* fmt : fmtMgr.getFormats()) {
            if (!fmt->fileMightContainThisPluginType(pluginStr))
                continue;

            juce::PluginDescription desc;
            desc.fileOrIdentifier = pluginStr;
            desc.pluginFormatName = fmt->getName();

            auto instance = fmtMgr.createPluginInstance(desc, 44100.0, 512, error);
            if (instance) {
                auto pluginDesc = instance->getPluginDescription();
                auto* obj = new juce::DynamicObject();
                obj->setProperty("ok", true);
                obj->setProperty("name", pluginDesc.name);
                obj->setProperty("manufacturer", pluginDesc.manufacturerName);
                obj->setProperty("category", pluginDesc.category);
                obj->setProperty("format", pluginDesc.pluginFormatName);
                obj->setProperty("file", pluginDesc.fileOrIdentifier);
                obj->setProperty("uid", static_cast<juce::int64>(pluginDesc.uniqueId));
                obj->setProperty("id", pluginDesc.createIdentifierString());
                obj->setProperty("isInstrument", pluginDesc.isInstrument);

                // Probe program/preset enumeration — catch crashes
                int numPrograms = 0;
                try {
                    numPrograms = instance->getNumPrograms();
                } catch (...) {
                    numPrograms = 0;
                }
                obj->setProperty("numPrograms", numPrograms);
                if (numPrograms > 1)
                {
                    juce::StringArray programNames;
                    for (int i = 0; i < numPrograms; ++i)
                    {
                        try {
                            programNames.add(instance->getProgramName(i));
                        } catch (...) {
                            programNames.add("(crash)");
                            break;
                        }
                    }
                    obj->setProperty("programNames", programNames.joinIntoString("\x01"));
                }

                std::cout << juce::JSON::toString(juce::var(obj)) << std::endl;

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

        {
            std::ofstream ofs(pedalFile, std::ios::trunc);
            ofs << "";
        }
        return 1;
    }
    catch (const SehException&)
    {
        // Plugin crashed during load/instantiation. The pedal file
        // already contains the culprit plugin path. Exit cleanly.
        return 2;
    }
    catch (...)
    {
        // Unexpected C++ exception from a plugin.
        return 2;
    }
}

int main(int argc, char* argv[])
{
#if _WIN32
    configureCrashProtection();
#endif

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

    int result = scanPlugin(pluginPath, pedalFile);

    // Ensure pedal file is cleared on success or load-failure
    // (crash paths keep it intact so the parent can read it).
    if (result == 0 || result == 1)
    {
        std::ofstream ofs(pedalFile, std::ios::trunc);
        ofs << "";
    }

    return result;
}
