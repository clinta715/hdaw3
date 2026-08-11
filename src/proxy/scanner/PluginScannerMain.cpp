#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include "common/DebugLog.h"
#include "common/PluginBinaryInfo.h"

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

#if _WIN32
static void probeVst3Module(const juce::String& pluginPath)
{
    HMODULE h = LoadLibraryA(pluginPath.toRawUTF8());
    if (!h) {
        DWORD err = GetLastError();
        HDAW_LOG("scanner", "raw module probe: LoadLibraryA FAILED err=" + juce::String(static_cast<int>(err))
                 + " path=" + pluginPath);
        return;
    }
    auto* initDll = reinterpret_cast<bool (*)()>(GetProcAddress(h, "InitDll"));
    auto* exitDll = reinterpret_cast<bool (*)()>(GetProcAddress(h, "ExitDll"));
    auto* getFactory = reinterpret_cast<void* (*)()>(GetProcAddress(h, "GetPluginFactory"));
    HDAW_LOG("scanner", "raw module probe: LoadLibraryA OK"
             + juce::String(" initDll=") + (initDll ? "exported" : "absent")
             + juce::String(" exitDll=") + (exitDll ? "exported" : "absent")
             + juce::String(" getFactory=") + (getFactory ? "exported" : "absent")
             + " pluginVersionMajor=" + juce::String(0));
    bool initOk = false;
    try {
        if (initDll) initOk = initDll();
    } catch (...) { initOk = false; }
    if (initDll) HDAW_LOG("scanner", "raw module probe: InitDll() returned " + juce::String(initOk ? "true" : "false"));
    void* factory = nullptr;
    try {
        if (getFactory) factory = getFactory();
    } catch (...) { factory = nullptr; }
    if (getFactory) HDAW_LOG("scanner", "raw module probe: GetPluginFactory() returned " + juce::String(factory != nullptr ? "non-null" : "NULL"));
    // Do NOT call ExitDll or FreeLibrary here — leave the module loaded for
    // the JUCE path. No cleanup needed (process exits).
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
#if _WIN32
        // Defense-in-depth: a 32-bit plugin image cannot load in this x64
        // scanner, and a bad one (e.g. FM8.vst3) hangs the process. Report as
        // a normal skip (exit 0 + ok:false) so the engine never blacklists
        // it as a scan_failure. Already filtered engine-side; this guards the
        // direct-spawn path.
        if (HDAW::is32BitPluginBinary(juce::File(juce::String(pluginPath))))
        {
            HDAW_LOG("scanner", "skipped (32-bit binary): " + juce::String(pluginPath));
            auto* obj = new juce::DynamicObject();
            obj->setProperty("ok", false);
            obj->setProperty("error", "skipped (32-bit binary)");
            std::cout << juce::JSON::toString(juce::var(obj)) << std::endl;
            return 0;
        }
#endif
        juce::ScopedJuceInitialiser_GUI juceInit;
        juce::AudioPluginFormatManager fmtMgr;
        fmtMgr.addFormat(new juce::VST3PluginFormat());
        fmtMgr.addFormat(new CLAPPluginFormat());

        juce::String pluginStr(pluginPath);
        juce::String error;

#if _WIN32
        probeVst3Module(pluginStr);
#endif

        bool anyTypeFound = false;
        for (auto* fmt : fmtMgr.getFormats()) {
            if (!fmt->fileMightContainThisPluginType(pluginStr)) {
                HDAW_LOG("scanner", "fmt " + fmt->getName() + ": fileMightContain=false for " + pluginStr);
                continue;
            }

            juce::OwnedArray<juce::PluginDescription> types;
            fmt->findAllTypesForFile(types, pluginStr);
            HDAW_LOG("scanner", "fmt " + fmt->getName() + ": findAllTypesForFile=" + juce::String(types.size()) + " types for " + pluginStr);

            if (fmt->getName() == "VST3") {
                juce::File f(pluginStr);
                bool ext = f.getChildFile("Contents").getChildFile("Resources")
                                .getChildFile("moduleinfo.json").existsAsFile();
                bool legacy = f.getChildFile("Contents").getChildFile("moduleinfo.json").existsAsFile();
                if (ext || legacy)
                    HDAW_LOG("scanner", "fmt VST3: moduleinfo.json fast-path present (ext=" + juce::String(ext ? "yes" : "no") + " legacy=" + juce::String(legacy ? "yes" : "no") + ")");
                else
                    HDAW_LOG("scanner", "fmt VST3: moduleinfo.json fast-path ABSENT (desc discovery will instantiate components)");
            }

            for (auto* t : types) {
                anyTypeFound = true;
                HDAW_LOG("scanner", "fmt " + fmt->getName() + ": desc name='" + t->name + "' uid=" + juce::String(static_cast<juce::int64>(t->uniqueId)) + " id='" + t->createIdentifierString() + "' version='" + t->version + "'");
                juce::String creationError;
                auto instance = fmtMgr.createPluginInstance(*t, 44100.0, 512, creationError);
                if (instance) {
                    HDAW_LOG("scanner", "fmt " + fmt->getName() + ": createPluginInstance OK name='" + instance->getPluginDescription().name + "'");
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
                error = creationError;
                HDAW_LOG("scanner", "fmt " + fmt->getName() + ": createPluginInstance FAILED error='" + creationError + "' for desc " + t->createIdentifierString());
            }
        }

        if (!anyTypeFound) {
            HDAW_LOG("scanner", "fmt loop: no types discovered for " + pluginStr);
            if (error.isEmpty())
                error = "no types discovered for " + pluginStr;
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
