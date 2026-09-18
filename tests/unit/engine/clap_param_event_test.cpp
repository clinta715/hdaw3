// clap_param_event_test.cpp — C2b-rev regression: CLAPParameter::setValue must
// deliver the value to the PLUGIN via a CLAP_EVENT_PARAM_VALUE event in the next
// process() input list (the only host→plugin param channel the gearmulator
// clap-juce-extensions wrapper consumes: handleParameterChangeEvent →
// processorParam->setValue). Before C2b-rev setValue only updated HDAW's cache
// (flushParameter), so the plugin's real JUCE parameter never ran and captured
// state stayed at boot.
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <clap/all.h>
#include <cstring>
#include <vector>
#include <memory>
#include "engine/CLAPPluginInstance.h"
#include "engine/CLAPPluginFormat.h"

namespace {

// ── Minimal stub CLAP plugin: records process() input events ──────────────
struct StubClap
{
    static std::vector<clap_event_param_value> receivedParamValues;

    static void reset() { receivedParamValues.clear(); }

    static uint32_t CLAP_ABI paramsCount(const clap_plugin_t*)
    { return 1; }

    static bool CLAP_ABI paramsGetInfo(const clap_plugin_t*, uint32_t idx,
                                       clap_param_info_t* info)
    {
        std::memset(info, 0, sizeof(*info));
        info->id = 42u;                       // the REAL clap param id
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        info->min_value = 0.0;
        info->max_value = 1.0;
        info->default_value = 0.5;
        std::strncpy(info->name, "Cut", CLAP_NAME_SIZE);
        return true;
    }

    static uint32_t CLAP_ABI audioPortsCount(const clap_plugin_t*, bool)
    { return 1; }

    static bool CLAP_ABI audioPortsGet(const clap_plugin_t*, uint32_t, bool isInput,
                                       clap_audio_port_info_t* info)
    {
        std::memset(info, 0, sizeof(*info));
        info->id = isInput ? 1u : 2u;
        info->channel_count = 2;
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        std::strncpy(info->name, isInput ? "in" : "out", CLAP_NAME_SIZE);
        return true;
    }

    static bool CLAP_ABI stubActivate(const clap_plugin_t*, double, uint32_t, uint32_t)
    { return true; }
    static void CLAP_ABI stubDeactivate(const clap_plugin_t*) {}
    static bool CLAP_ABI stubStartProcessing(const clap_plugin_t*) { return true; }
    static void CLAP_ABI stubStopProcessing(const clap_plugin_t*) {}
    static void CLAP_ABI stubDestroy(const clap_plugin_t*) {}

    static clap_process_status CLAP_ABI stubProcess(const clap_plugin_t*,
                                                    const clap_process_t* proc)
    {
        receivedParamValues.clear();
        if (proc->in_events != nullptr)
        {
            const uint32_t n = proc->in_events->size(proc->in_events);
            for (uint32_t i = 0; i < n; ++i)
            {
                const auto* ev = proc->in_events->get(proc->in_events, i);
                if (ev->type == CLAP_EVENT_PARAM_VALUE)
                    receivedParamValues.push_back(
                        *reinterpret_cast<const clap_event_param_value*>(ev));
            }
        }
        return CLAP_PROCESS_CONTINUE;
    }

    static const void* CLAP_ABI stubGetExtension(const clap_plugin_t*,
                                                 const char* id)
    {
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &paramsExt;
        if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
            return &audioPortsExt;
        return nullptr;                        // state/gui/note-ports/preset-load
    }

    static const clap_plugin_t* plugin()
    {
        static clap_plugin_descriptor desc{};
        static const char* descName = "StubParamEcho";
        static const char* descId = "hdaw.test.stubparamecho";
        desc.name = descName;
        desc.id = descId;
        desc.version = "1.0.0";
        static clap_plugin_t p{};
        p.desc = &desc;
        p.plugin_data = nullptr;
        p.init = nullptr;
        p.destroy = stubDestroy;
        p.activate = stubActivate;
        p.deactivate = stubDeactivate;
        p.start_processing = stubStartProcessing;
        p.stop_processing = stubStopProcessing;
        p.process = stubProcess;
        p.get_extension = stubGetExtension;
        return &p;
    }

    static clap_plugin_params_t paramsExt;
    static clap_plugin_audio_ports_t audioPortsExt;
};

std::vector<clap_event_param_value> StubClap::receivedParamValues;
clap_plugin_params_t StubClap::paramsExt{ StubClap::paramsCount, StubClap::paramsGetInfo,
                                          nullptr, nullptr, nullptr, nullptr };
clap_plugin_audio_ports_t StubClap::audioPortsExt{ StubClap::audioPortsCount,
                                                   StubClap::audioPortsGet };

} // namespace

TEST(CLAPParamEvents, SetValueDeliversParamEventToPlugin)
{
    StubClap::reset();

    auto module = std::make_shared<CLAPModule>();
    auto host = std::make_unique<CLAPHost>(nullptr);
    CLAPPluginInstance inst(module, StubClap::plugin(), std::move(host));
    inst.initialize();                          // buildParameters/buildBuses

    ASSERT_EQ(inst.getParameters().size(), 1u);
    auto* p = inst.getParameters()[0];
    EXPECT_NEAR(p->getValue(), 0.5f, 1e-6f);    // default value

    inst.prepareToPlay(44100.0, 128);

    juce::AudioBuffer<float> buf(2, 128);
    buf.clear();
    juce::MidiBuffer midi;

    // No pending param set → no param events in the block.
    inst.processBlock(buf, midi);
    EXPECT_TRUE(StubClap::receivedParamValues.empty());

    // A host param set must arrive at the plugin as a param-value event with
    // the REAL clap param id (42), carrying the plain value.
    p->setValue(0.7f);
    inst.processBlock(buf, midi);
    ASSERT_EQ(StubClap::receivedParamValues.size(), 1u);
    EXPECT_EQ(StubClap::receivedParamValues[0].param_id, 42u);
    EXPECT_NEAR(StubClap::receivedParamValues[0].value, 0.7, 1e-6);

    // Delivered sets are drained — a second block carries nothing.
    inst.processBlock(buf, midi);
    EXPECT_TRUE(StubClap::receivedParamValues.empty());

    // Last-value-wins coalescing: two sets before one block → one event, newest.
    p->setValue(0.3f);
    p->setValue(0.9f);
    inst.processBlock(buf, midi);
    ASSERT_EQ(StubClap::receivedParamValues.size(), 1u);
    EXPECT_NEAR(StubClap::receivedParamValues[0].value, 0.9, 1e-6);

    // The param cache reflects the applied value (plain == normalized here).
    EXPECT_NEAR(p->getValue(), 0.9f, 1e-5f);
}
