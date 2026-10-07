// clap_port_layout_test.cpp — regression net for the CLAP multi-output-port
// shape bug.
//
// TU NOTE (root cause, do not "simplify" the port shape away):
// The CLAP audio-ports contract requires the host to pass the plugin exactly
// the ports it declared, each with that port's own channel count. HDAW used to
// collapse every output port into ONE host port whose channel_count was the SUM
// of each port's channels. Surge XT declares three 2-channel output ports
// (Output 2ch, Scene A 2ch, Scene B 2ch): handed a single 6-channel port it
// returned CLAP_PROCESS_ERROR (= 0) from every process() and wrote nothing —
// and HDAW never inspected the status, so the failure was completely silent.
// A standalone (no-JUCE) CLAP host reproduced it: 1 port x 2ch -> status 1,
// audible; 1 port x 6ch -> status 0, silent; 3 ports x 2ch -> status 1,
// audible. That last shape is the fix. Vavra/Osirus declare the same 3x2ch
// shape and merely tolerated the old one; Dexed/JE8086 are 1x2ch.
//
// These tests pin the per-port shape so a future "simplification" back to one
// summed port fails loudly here instead of silently in the field.
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <clap/all.h>
#include <atomic>
#include <cstring>
#include <vector>
#include <memory>
#include "engine/CLAPPluginInstance.h"
#include "engine/CLAPPluginFormat.h"

namespace {

// ── Configurable stub CLAP plugin: declares N output ports of width W and
//    records exactly what the host passed to process(). ─────────────────────
struct StubPorts
{
    static int outPortCount;      // declared output ports
    static int outPortWidth;      // declared width of each output port
    static bool returnError;      // make process() return CLAP_PROCESS_ERROR

    static std::atomic<int> processCalls;
    static std::vector<uint32_t> seenOutputCount;
    static std::vector<uint32_t> seenPortWidths;   // flattened per port
    static std::vector<float*>   seenPortPtrs;     // flattened per port*width

    static void reset()
    {
        outPortCount = 1;
        outPortWidth = 2;
        returnError = false;
        processCalls.store(0);
        seenOutputCount.clear();
        seenPortWidths.clear();
        seenPortPtrs.clear();
    }

    static uint32_t CLAP_ABI audioPortsCount(const clap_plugin_t*, bool)
    { return static_cast<uint32_t>(outPortCount); }

    static bool CLAP_ABI audioPortsGet(const clap_plugin_t*, uint32_t, bool isInput,
                                       clap_audio_port_info_t* info)
    {
        std::memset(info, 0, sizeof(*info));
        info->id = isInput ? 1u : 2u;
        info->channel_count = static_cast<uint32_t>(isInput ? 2 : outPortWidth);
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
        processCalls.fetch_add(1, std::memory_order_relaxed);
        seenOutputCount.push_back(proc->audio_outputs_count);

        const uint32_t ports = proc->audio_outputs_count;
        for (uint32_t p = 0; p < ports; ++p)
        {
            const auto& port = proc->audio_outputs[p];
            seenPortWidths.push_back(port.channel_count);
            for (uint32_t c = 0; c < port.channel_count; ++c)
            {
                float* dst = port.data32 ? port.data32[c] : nullptr;
                seenPortPtrs.push_back(dst);
                if (dst != nullptr)
                {
                    // Recognisable constant per port/channel.
                    const float v = 0.5f * static_cast<float>(p + 1)
                                  + 0.01f * static_cast<float>(c);
                    for (uint32_t f = 0; f < proc->frames_count; ++f)
                        dst[f] = v;
                }
            }
        }

        if (returnError)
            return CLAP_PROCESS_ERROR;
        return CLAP_PROCESS_CONTINUE;
    }

    static const void* CLAP_ABI stubGetExtension(const clap_plugin_t*, const char* id)
    {
        if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
            return &audioPortsExt;
        return nullptr;                        // params/state/gui/note-ports
    }

    static const clap_plugin_t* plugin()
    {
        static clap_plugin_descriptor desc{};
        static const char* descName = "StubPortLayout";
        static const char* descId = "hdaw.test.stubportlayout";
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

    static clap_plugin_audio_ports_t audioPortsExt;
};

int StubPorts::outPortCount = 1;
int StubPorts::outPortWidth = 2;
bool StubPorts::returnError = false;
std::atomic<int> StubPorts::processCalls{ 0 };
std::vector<uint32_t> StubPorts::seenOutputCount;
std::vector<uint32_t> StubPorts::seenPortWidths;
std::vector<float*>   StubPorts::seenPortPtrs;
clap_plugin_audio_ports_t StubPorts::audioPortsExt{ StubPorts::audioPortsCount,
                                                    StubPorts::audioPortsGet };

// Build an instance against the stub. Caller keeps the module alive.
std::unique_ptr<CLAPPluginInstance> makeInstance()
{
    auto module = std::make_shared<CLAPModule>();
    auto host = std::make_unique<CLAPHost>(nullptr);
    auto inst = std::make_unique<CLAPPluginInstance>(module, StubPorts::plugin(),
                                                     std::move(host));
    inst->initialize();
    return inst;
}

constexpr int kFrames = 64;

} // namespace

// 1. The bug: 3 ports x 2ch must be declared to the plugin as THREE ports of
//    2 channels, not one 6-channel port — even when the host buffer only has
//    two channels.
TEST(CLAPPortLayout, ThreePortsTwoChannelsHostStereo)
{
    StubPorts::reset();
    StubPorts::outPortCount = 3;
    StubPorts::outPortWidth = 2;

    auto inst = makeInstance();
    ASSERT_EQ(inst->getNumOutputChannels(), 6);   // total stays the sum
    inst->prepareToPlay(44100.0, kFrames);

    juce::AudioBuffer<float> buf(2, kFrames);
    buf.clear();
    juce::MidiBuffer midi;
    float* hostCh0 = buf.getWritePointer(0);
    float* hostCh1 = buf.getWritePointer(1);

    inst->processBlock(buf, midi);

    ASSERT_EQ(StubPorts::seenOutputCount.size(), 1u);
    EXPECT_EQ(StubPorts::seenOutputCount[0], 3u);            // three ports, not one
    ASSERT_EQ(StubPorts::seenPortWidths.size(), 3u);
    EXPECT_EQ(StubPorts::seenPortWidths[0], 2u);
    EXPECT_EQ(StubPorts::seenPortWidths[1], 2u);
    EXPECT_EQ(StubPorts::seenPortWidths[2], 2u);

    // Port 0 maps to host channels 0 and 1; every pointer is writable.
    ASSERT_EQ(StubPorts::seenPortPtrs.size(), 6u);
    EXPECT_EQ(StubPorts::seenPortPtrs[0], hostCh0);
    EXPECT_EQ(StubPorts::seenPortPtrs[1], hostCh1);
    for (float* p : StubPorts::seenPortPtrs)
        EXPECT_NE(p, nullptr);

    // Host channels 0/1 hold port 0's constants after process().
    EXPECT_FLOAT_EQ(buf.getSample(0, 0), 0.5f);
    EXPECT_FLOAT_EQ(buf.getSample(1, 0), 0.51f);
}

// 2. Single-port plugins (Dexed, JE8086) keep today's behaviour bit-for-bit.
TEST(CLAPPortLayout, SinglePortHostStereoUnchanged)
{
    StubPorts::reset();
    StubPorts::outPortCount = 1;
    StubPorts::outPortWidth = 2;

    auto inst = makeInstance();
    ASSERT_EQ(inst->getNumOutputChannels(), 2);
    inst->prepareToPlay(44100.0, kFrames);

    juce::AudioBuffer<float> buf(2, kFrames);
    buf.clear();
    juce::MidiBuffer midi;
    float* hostCh0 = buf.getWritePointer(0);

    inst->processBlock(buf, midi);

    ASSERT_EQ(StubPorts::seenOutputCount.size(), 1u);
    EXPECT_EQ(StubPorts::seenOutputCount[0], 1u);
    ASSERT_EQ(StubPorts::seenPortWidths.size(), 1u);
    EXPECT_EQ(StubPorts::seenPortWidths[0], 2u);
    ASSERT_EQ(StubPorts::seenPortPtrs.size(), 2u);
    EXPECT_EQ(StubPorts::seenPortPtrs[0], hostCh0);

    EXPECT_FLOAT_EQ(buf.getSample(0, 0), 0.5f);
}

// 3. With a 6-channel host buffer the three ports map to 0-1, 2-3, 4-5.
TEST(CLAPPortLayout, ThreePortsSixChannelHost)
{
    StubPorts::reset();
    StubPorts::outPortCount = 3;
    StubPorts::outPortWidth = 2;

    auto inst = makeInstance();
    inst->prepareToPlay(44100.0, kFrames);

    juce::AudioBuffer<float> buf(6, kFrames);
    buf.clear();
    juce::MidiBuffer midi;

    inst->processBlock(buf, midi);

    ASSERT_EQ(StubPorts::seenOutputCount.size(), 1u);
    EXPECT_EQ(StubPorts::seenOutputCount[0], 3u);
    ASSERT_EQ(StubPorts::seenPortWidths.size(), 3u);
    EXPECT_EQ(StubPorts::seenPortWidths[0], 2u);
    EXPECT_EQ(StubPorts::seenPortWidths[1], 2u);
    EXPECT_EQ(StubPorts::seenPortWidths[2], 2u);

    for (int p = 0; p < 3; ++p)
    {
        EXPECT_EQ(StubPorts::seenPortPtrs[p * 2], buf.getWritePointer(p * 2));
        EXPECT_EQ(StubPorts::seenPortPtrs[p * 2 + 1], buf.getWritePointer(p * 2 + 1));
    }

    EXPECT_FLOAT_EQ(buf.getSample(0, 0), 0.5f);
    EXPECT_FLOAT_EQ(buf.getSample(2, 0), 1.0f);
    EXPECT_FLOAT_EQ(buf.getSample(4, 0), 1.5f);
}

// 4. A plugin declaring no output ports still gets the documented 1x2 fallback.
TEST(CLAPPortLayout, NoDeclaredOutputPortsFallsBackToStereo)
{
    StubPorts::reset();
    StubPorts::outPortCount = 0;

    auto inst = makeInstance();
    ASSERT_EQ(inst->getNumOutputChannels(), 2);   // fallback total
    inst->prepareToPlay(44100.0, kFrames);

    juce::AudioBuffer<float> buf(2, kFrames);
    buf.clear();
    juce::MidiBuffer midi;

    inst->processBlock(buf, midi);

    ASSERT_EQ(StubPorts::seenOutputCount.size(), 1u);
    EXPECT_EQ(StubPorts::seenOutputCount[0], 1u);
    ASSERT_EQ(StubPorts::seenPortWidths.size(), 1u);
    EXPECT_EQ(StubPorts::seenPortWidths[0], 2u);
    ASSERT_EQ(StubPorts::seenPortPtrs.size(), 2u);
    for (float* p : StubPorts::seenPortPtrs)
        EXPECT_NE(p, nullptr);
}

// 5. CLAP_PROCESS_ERROR is observed (and never crashes the host); a later
//    block with a healthy status keeps working.
TEST(CLAPPortLayout, ProcessErrorStatusIsObservedAndRecovers)
{
    StubPorts::reset();
    StubPorts::outPortCount = 1;
    StubPorts::outPortWidth = 2;
    StubPorts::returnError = true;

    auto inst = makeInstance();
    inst->prepareToPlay(44100.0, kFrames);

    juce::AudioBuffer<float> buf(2, kFrames);
    buf.clear();
    juce::MidiBuffer midi;

    inst->processBlock(buf, midi);              // must not crash
    EXPECT_EQ(StubPorts::processCalls.load(), 1);

    StubPorts::returnError = false;
    buf.clear();
    inst->processBlock(buf, midi);              // recovers
    EXPECT_EQ(StubPorts::processCalls.load(), 2);
    EXPECT_FLOAT_EQ(buf.getSample(0, 0), 0.5f);
}
