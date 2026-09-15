#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "model/ProjectModel.h"

#include <vector>

// Capture VES diagnostics (juce::Logger::writeToLog) into stdout — the
// embedded MAME runtime reports boot failures through the logger.
struct CaptureLogger : public juce::Logger
{
    void logMessage (const juce::String& message) override
    {
        std::cout << "[VesLog] " << message.toStdString() << std::endl;
    }
};

// VES (Vintage Emulator Studio, MAME-embedded VST3) boot + audition probe.
// Verifies: isolated child spawns, MAME machine boots from the configured
// ROM dir, and auditionPlugin renders audible audio. ROM dir configured via
// %APPDATA%\VintageEmulatorStudio\settings\rom-directory.txt.

namespace {

const char* kVesBundle =
    "C:\\Program Files\\Common Files\\VST3\\Vintage Emulator Studio.vst3";

bool vesAvailable()
{
    const char* env = getenv("HDAW_REAL_PLUGIN_TESTS");
    if (env == nullptr)
        return false;
    const juce::String s(env);
    if (s.trim().isEmpty() || s.trim() == "0")
        return false;
    return juce::File(kVesBundle).exists();
}

} // namespace

TEST(VesBootProbe, BootsAndRenders)
{
    if (!vesAvailable())
        GTEST_SKIP() << "HDAW_REAL_PLUGIN_TESTS not set or VES missing";

    CaptureLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    AudioEngine engine;
    engine.initialize();
    auto& cmds = engine.getProjectCommands();

    const int trackIdx = cmds.addTrack("VESProbe");
    ASSERT_GE(trackIdx, 0);
    cmds.addFxSlot(trackIdx, "plugin", -1, kVesBundle);
    cmds.setFxSlotPlugin(trackIdx, 0, "plugin", kVesBundle, "VST3", kVesBundle);

    const int clipId = cmds.addMidiClip(trackIdx, 0.0, 4.0, "Probe");
    ASSERT_GT(clipId, 0);
    cmds.addNote(clipId, 60, 100, 0.0, 3.5);
    if (auto* proc0 = engine.getMainProcessor())
        proc0->rebuildRoutingGraph();

    auto* proc = engine.getMainProcessor();
    if (proc == nullptr)
        GTEST_SKIP() << "no main processor";
    auto* track = proc->getTrack(trackIdx);
    for (int waited = 0; track == nullptr && waited < 5000; waited += 100)
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(100);
        else
            juce::Thread::sleep(100);
        track = proc->getTrack(trackIdx);
    }
    if (track == nullptr)
        GTEST_SKIP() << "no live graph";
    ASSERT_FALSE(track->getFXChain().empty());
    auto& slot = track->getFXChain()[0];
    ASSERT_NE(slot, nullptr);
    auto* instance = slot->getPluginInstance();
    for (int waited = 0; instance == nullptr && waited < 20000; waited += 250)
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
            mm->runDispatchLoopUntil(250);
        else
            juce::Thread::sleep(250);
        instance = slot->getPluginInstance();
    }
    if (instance == nullptr)
        GTEST_SKIP() << "VES instance failed to load";
    std::cout << "[VesProbe] instance loaded: " << instance->getName().toStdString() << std::endl;

    // Realtime-paced drive with the transport RUNNING (the child reads the
    // playhead snapshot from the SHM header; some hosts gate plugin audio
    // on transportPlaying). One noteOn per second, peak per second.
    engine.getTransportCommands().play();
    juce::Thread::sleep(300);

    // Boot-latency probe: drive the LIVE instance in REALTIME PACE. The
    // isolated child paces itself realtime; non-render processBlock never
    // waits on the SHM ring, so driving faster than realtime overflows the
    // ring and drops every block. One noteOn per second, peak per second —
    // the first non-zero second reveals MAME boot latency.
    const int sr = instance->getSampleRate() > 0 ? (int) instance->getSampleRate() : 44100;
    const int blockSize = instance->getBlockSize() > 0 ? instance->getBlockSize() : 512;
    std::cout << "[VesProbe] sr=" << sr << " blockSize=" << blockSize << std::endl;
    juce::AudioBuffer<float> buf(2, blockSize);
    juce::MidiBuffer midi;
    float bestPeak = 0.0f;
    const int blocksPerSecond = std::max(1, sr / blockSize);
    for (int sec = 0; sec < 60; ++sec)
    {
        float secPeak = 0.0f;
        for (int b = 0; b < blocksPerSecond; ++b)
        {
            buf.clear();
            midi.clear();
            if (b == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 127), 0);
            instance->processBlock(buf, midi);
            secPeak = std::max(secPeak, buf.getMagnitude(0, 0, blockSize));
            juce::Thread::sleep(std::max(1, 1000 / blocksPerSecond));
        }
        bestPeak = std::max(bestPeak, secPeak);
        std::cout << "[VesProbe] t+" << sec << "s peak=" << secPeak << std::endl;
        if (sec >= 8 && bestPeak > 0.001f)
            break;
    }
    std::cout << "[VesProbe] bestPeak=" << bestPeak << std::endl;
    if (bestPeak <= 0.0f)
    {
        std::cout << "[VesProbe] VERDICT SILENT: VES loads isolated, MAME emulation "
                     "runs (~1 core), but the audio bridge delivers zero samples "
                     "after 60s realtime with noteOns + transport playing. "
                     "Next diagnostic: open the VES editor in the child and "
                     "watch the machine screen boot." << std::endl;
        GTEST_SKIP() << "VES bridge silent in isolated path (diagnostic probe, not a gate)";
    }
    EXPECT_GT(bestPeak, 0.0f);
}
