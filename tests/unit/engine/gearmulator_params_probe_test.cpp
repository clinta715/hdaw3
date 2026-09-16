// gearmulator_params_probe_test.cpp — PROBE: do the core gearmulator CLAPs
// (Osirus, OsTIrus, Vavra, Xenia/XeniaFX, JE8086, NodalRed2x, Dexed) expose
// their parameters — including their INTERNAL FX (chorus/delay/reverb/dist)
// — to the host as CLAP params? Mirrors PluginParamServiceImpl::getParams
// (the exact surface list_fx_params uses). Read-only: no engine, no project,
// no mutation; in-process instantiation (isolationEnabled=false). Findings
// are written to C:/temp/gearmulator_params_probe.txt (gtest captures
// stdout, so the file is the reliable channel).
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include "engine/PluginManager.h"
#include "engine/CLAPPluginInstance.h"

#include <cstdio>

namespace {

const char* const kProbeOut = "C:/temp/gearmulator_params_probe.txt";

juce::StringArray gearmulatorFiles()
{
    juce::StringArray out;
    static const char* const kNames[] = {
        "osirus", "ostirus", "virus", "vavra", "xenia", "je8086",
        "nodal", "dexed", "jp-8000", "microq", "microwave" };
    for (const auto& d : HDAW::PluginManager::getClapDirs())
    {
        juce::DirectoryIterator it(juce::File(d), false, "*.clap");
        while (it.next())
        {
            const auto name = it.getFile().getFileNameWithoutExtension().toLowerCase();
            for (const char* n : kNames)
                if (name.contains(n)) { out.add(it.getFile().getFullPathName()); break; }
        }
    }
    return out;
}

} // namespace

TEST(GearmulatorParamsProbe, EnumerateHostParams)
{
    // MANUAL PROBE, skipped by default: it instantiates real gearmulator CLAP
    // firmware plugins in-process, which destabilises the shared full-suite
    // process (measured 2026-09-16: a full run crashed with 0xC0000005 after
    // this test; the entire suite passes with it excluded). Run with
    // HDAW_RUN_GEARMULATOR_PROBE=1 when you want the param dump.
    if (std::getenv("HDAW_RUN_GEARMULATOR_PROBE") == nullptr)
        GTEST_SKIP() << "set HDAW_RUN_GEARMULATOR_PROBE=1 to instantiate real CLAPs "
                        "(writes " << kProbeOut << ")";
    std::FILE* out = std::fopen(kProbeOut, "w");
    ASSERT_NE(out, nullptr);

    std::fprintf(out, "=== CLAP dir listing ===\n");
    for (const auto& d : HDAW::PluginManager::getClapDirs())
    {
        juce::DirectoryIterator it(juce::File(d), false, "*.clap");
        while (it.next())
            std::fprintf(out, "  %s\n", it.getFile().getFileNameWithoutExtension().toRawUTF8());
    }

    HDAW::PluginManager pm;
    pm.isolationEnabled = false;
    const auto files = gearmulatorFiles();
    std::fprintf(out, "=== Gearmulator param probe: %d file(s) ===\n", files.size());
    for (const auto& path : files)
    {
        juce::PluginDescription desc;
        desc.pluginFormatName = "CLAP";
        desc.fileOrIdentifier = path;
        desc.name = juce::File(path).getFileNameWithoutExtension();
        juce::String error;
        auto inst = pm.createPluginInstance(desc, error, 44100.0, 512, false);
        std::fprintf(out, "--- %s ---\n", desc.name.toRawUTF8());
        std::fflush(out);
        if (inst == nullptr)
        {
            std::fprintf(out, "  INSTANTIATE FAILED: %s\n", error.toRawUTF8());
            std::fflush(out);
            continue;
        }
        auto& params = inst->getParameters();
        const int n = static_cast<int>(params.size());
        std::fprintf(out, "  paramCount=%d\n", n);
        for (int i = 0; i < n; ++i)
        {
            auto* p = params[i];
            const juce::String name = p->getName(128);
            const juce::String label = p->getLabel();
            const juce::String t0 = p->getText(0.0f, 128);
            const juce::String t1 = p->getText(1.0f, 128);
            double lo = 0.0, hi = 0.0, defV = 0.0;
            bool hasRange = false;
            if (auto* clap = dynamic_cast<CLAPParameter*>(p))
                if (clap->getMaxValue() > clap->getMinValue())
                {
                    hasRange = true;
                    lo = clap->getMinValue();
                    hi = clap->getMaxValue();
                    defV = clap->getDefaultPlainValue();
                }
            std::fprintf(out,
                "  [%d] name=%s label=%s automatable=%d hasRange=%d range=[%g,%g] def=%g text0=%s text1=%s\n",
                i, name.toRawUTF8(), label.toRawUTF8(),
                p->isAutomatable() ? 1 : 0, hasRange ? 1 : 0,
                lo, hi, defV,
                t0.isEmpty() ? "-" : t0.toRawUTF8(),
                t1.isEmpty() ? "-" : t1.toRawUTF8());
        }
        std::fflush(out);
    }
    std::fprintf(out, "=== end gearmulator probe ===\n");
    std::fclose(out);
    SUCCEED();
}
