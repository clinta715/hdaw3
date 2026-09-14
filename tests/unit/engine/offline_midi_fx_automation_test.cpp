#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstring>
#include <iostream>

#include "engine/AudioEngine.h"
#include "engine/MainAudioProcessor.h"
#include "engine/ExportManager.h"
#include "common/ProjectCommands.h"
#include "model/ProjectModel.h"

// B11 (Modular Dawn audit): the layer agent A/B measured IDENTICAL offline
// renders regardless of transpose MIDI FX settings and cutoff automation
// curves, concluding the offline render path bypasses the MIDI FX chain and
// the automation reader. Code-trace refutes that: BOTH the live path and the
// offline export drive Track::processBlock through AudioProcessorGraph with a
// per-block setPlayHead (JUCE 9 NodeOp::process → processor.setPlayHead), and
// Track::processBlock runs the MIDI FX chain + the automation reader whenever
// the playhead reports playing. The export's fresh TransportManager never
// loops and always reports playing. In-tree proof the offline graph really
// processes tracks: master_gain_test.RenderAttenuation (peak halves with the
// master gain) and fm_patch_offline_export_test (patch bytes change the WAV).
// The audit's A/B ran against the stale 0.33.0 engine (audit §3 W1: the
// 0.34.0 binary was not built until after the session).
//
// These tests PIN the contract end to end (command → ValueTree → export tree
// copy → RoutingManager rebuild → MIDI FX chain / automation managers →
// Track::processBlock → WAV): a transpose slot and a cutoff automation lane
// MUST change the offline WAV. If any of these fails, the offline render path
// is genuinely bypassing per-block MIDI/automation processing again.

namespace {

bool waitForExport(HDAW::ExportManager& em, int timeoutMs = 180000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + timeoutMs;
    while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    return !em.isExporting();
}

float rmsOf(const juce::File& wav)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(wav));
    if (!r) return -1.0f;
    const int64_t len = r->lengthInSamples;
    juce::AudioBuffer<float> buf(2, static_cast<int>(len));
    r->read(&buf, 0, static_cast<int>(len), 0, true, true);
    double sumSq = 0.0;
    int64_t n = 0;
    for (int c = 0; c < 2; ++c)
        for (int s = 0; s < buf.getNumSamples(); ++s)
        {
            const float v = buf.getSample(c, s);
            sumSq += static_cast<double>(v) * static_cast<double>(v);
            ++n;
        }
    return n > 0 ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(n))) : 0.0f;
}

bool wavBytesDiffer(const juce::File& a, const juce::File& b)
{
    if (a.getSize() != b.getSize()) return true;
    juce::FileInputStream ia(a), ib(b);
    if (!ia.openedOk() || !ib.openedOk()) return true;
    const int n = static_cast<int>(a.getSize());
    std::vector<uint8_t> ba(static_cast<size_t>(n)), bb(static_cast<size_t>(n));
    ia.read(ba.data(), n);
    ib.read(bb.data(), n);
    return std::memcmp(ba.data(), bb.data(), static_cast<size_t>(n)) != 0;
}

int seedLeadPart(AudioEngine& engine)
{
    // Deterministic internal fm_synth content (fixed seed, no plugins) — the
    // same recipe master_gain_test.RenderAttenuation uses for offline renders.
    ProjectCommands::InstrumentPartParams params;
    params.trackName = "Offline Lead";
    params.style = "Lead";
    params.lengthBeats = 4.0;
    params.placement = "region";
    params.count = 1;
    params.seed = 42;
    auto res = engine.getProjectCommands().addInstrumentPart(params);
    EXPECT_TRUE(res.error.empty()) << res.error;
    EXPECT_GE(res.trackIndex, 0);
    return res.trackIndex;
}

bool exportTree(AudioEngine& engine, const juce::File& out)
{
    out.deleteFile();
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const double duration =
        HDAW::ExportManager::calculateProjectDuration(engine.getProjectModel());
    if (!engine.getMainProcessor()->getExportManager().startExport(
            engine.getProjectModel().getTree(), fm, nullptr, out,
            44100.0, 0.0, duration, HDAW::ExportManager::WAV, 24))
        return false;
    if (!waitForExport(engine.getMainProcessor()->getExportManager()))
        return false;
    return out.existsAsFile() && out.getSize() > 1000;
}

} // namespace

// Decisive probe for the B11 automation seam: does an AudioProcessorGraph
// propagate its playhead to node processors per block (the mechanism the
// offline export — and the live graph — rely on for Track::processBlock's
// automation reader)? A tiny recorder processor pins the observed playhead.
namespace {

class PlayheadProbeProcessor : public juce::AudioProcessor
{
public:
    PlayheadProbeProcessor() : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
        ++blocksSeen;
        if (auto* ph = getPlayHead())
        {
            hadPlayhead = true;
            if (auto pos = ph->getPosition())
            {
                posValid = true;
                isPlaying = pos->getIsPlaying();
                lastTimeSec = pos->getTimeInSeconds().orFallback(-1.0);
            }
        }
    }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "PHProbe"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    std::atomic<int> blocksSeen { 0 };
    std::atomic<bool> hadPlayhead { false };
    std::atomic<bool> posValid { false };
    std::atomic<bool> isPlaying { false };
    std::atomic<double> lastTimeSec { -1.0 };
};

class ProbePlayHead : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(true);
        info.setTimeInSeconds(seconds);
        info.setTimeInSamples(static_cast<juce::int64>(seconds * 48000.0));
        info.setBpm(120.0);
        return info;
    }
    double seconds = 0.0;
};

} // namespace

TEST(OfflineMidiFxAutomation, GraphPropagatesPlayheadToNodes)
{
    juce::AudioProcessorGraph graph;
    ProbePlayHead ph;
    graph.setPlayHead(&ph);

    juce::AudioProcessorGraph::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    graph.setBusesLayout(layout);

    auto probe = graph.addNode(std::make_unique<PlayheadProbeProcessor>());
    ASSERT_NE(probe, nullptr);
    auto io = graph.addNode(std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
        juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));
    ASSERT_NE(io, nullptr);
    for (int ch = 0; ch < 2; ++ch)
        ASSERT_TRUE(graph.addConnection(juce::AudioProcessorGraph::Connection{
            { probe->nodeID, ch }, { io->nodeID, ch } }));

    graph.prepareToPlay(48000.0, 512);
    graph.setNonRealtime(true);
    graph.rebuild();

    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 4; ++i)
    {
        ph.seconds = 0.25 * i;
        buf.clear();
        graph.processBlock(buf, midi);
    }

    auto* p = static_cast<PlayheadProbeProcessor*>(probe->getProcessor());
    const int blocks = p->blocksSeen.load();
    const bool hadPh = p->hadPlayhead.load();
    const bool posOk = p->posValid.load();
    const bool playing = p->isPlaying.load();
    const double t = p->lastTimeSec.load();
    EXPECT_GE(blocks, 4);
    EXPECT_TRUE(hadPh) << "graph did not hand a playhead to the node";
    EXPECT_TRUE(posOk) << "node got a null PositionInfo";
    EXPECT_TRUE(playing) << "node saw isPlaying=false";
    EXPECT_GE(t, 0.5) << "node saw a frozen playhead (automation would read one position forever)";
}

// A transpose MIDI FX slot with different semitones must render DIFFERENT
// offline WAVs (the note numbers reaching the synth differ → the synth output
// differs). Byte-identical renders are the B11 signature.
TEST(OfflineMidiFxAutomation, TransposeMidiFxChangesOfflineRender)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedLeadPart(engine), 0);
    auto& cmds = engine.getProjectCommands();

    cmds.addMidiFxSlot(0, "transpose", 0);

    const juce::File outZero = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_b11_transpose_0.wav");
    const juce::File outTwelve = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_b11_transpose_12.wav");

    ASSERT_TRUE(exportTree(engine, outZero)) << "transpose+0 export failed";
    cmds.setMidiFxSlotParam(0, 0, "semitones", 12.0);
    ASSERT_TRUE(exportTree(engine, outTwelve)) << "transpose+12 export failed";

    const float rmsZero = rmsOf(outZero);
    const float rmsTwelve = rmsOf(outTwelve);
    std::cout << "[B11] transpose rms +0=" << rmsZero
              << " +12=" << rmsTwelve << std::endl;
    ASSERT_GT(rmsZero, 0.01f) << "offline render is silent";
    ASSERT_GT(rmsTwelve, 0.01f) << "offline render is silent";
    EXPECT_TRUE(wavBytesDiffer(outZero, outTwelve))
        << "transpose semitones did not change the offline render — the MIDI FX "
           "chain is bypassed offline (B11)";

    outZero.deleteFile();
    outTwelve.deleteFile();
}

// A cutoff automation lane must drive the offline render: a lane pinned at 0
// (cutoff 20 Hz lowpass) must attenuate the track hard, a lane opened to 1
// (cutoff ~20 kHz) must pass it. Identical renders = the automation reader is
// bypassed offline (B11 signature).
TEST(OfflineMidiFxAutomation, FilterAutomationLaneChangesOfflineRender)
{
    AudioEngine engine;
    engine.initialize();
    ASSERT_GE(seedLeadPart(engine), 0);
    auto& cmds = engine.getProjectCommands();

    // Internal filter: addInstrumentPart put the fm_synth at fx slot 0, so
    // the filter appends at slot 1. Lane bound to
    // pid 200 = 100 + slot 1 * 100 + param 0 (Cutoff) — same compound the
    // live AutomationPidRouting.AudioLaneDrivesLiveFilterCutoff test uses.
    cmds.addFxSlot(0, "filter");
    cmds.setFxSlotParam(0, 1, 1, 0.0f);   // slot 1 Mode = lowpass
    ASSERT_TRUE(cmds.addAutomationLane(0, "B11Cutoff", 200));
    // Points in BEATS (command boundary converts beats → seconds).
    cmds.addAutomationPoint(0, "B11Cutoff", 0.0, 0.0f);   // closed: 20 Hz

    const juce::File outClosed = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_b11_cutoff_closed.wav");
    const juce::File outOpen = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("hdaw_b11_cutoff_open.wav");

    ASSERT_TRUE(exportTree(engine, outClosed)) << "cutoff-closed export failed";

    // Same lane, different curve: open from 0.125 s on. A different automation
    // curve MUST produce a different render.
    cmds.addAutomationPoint(0, "B11Cutoff", 0.25, 1.0f);
    ASSERT_TRUE(exportTree(engine, outOpen)) << "cutoff-open export failed";

    const float rmsClosed = rmsOf(outClosed);
    const float rmsOpen = rmsOf(outOpen);
    std::cout << "[B11] automation rms closed=" << rmsClosed
              << " open=" << rmsOpen << std::endl;
    ASSERT_GE(rmsClosed, 0.0f);
    ASSERT_GT(rmsOpen, 0.01f) << "open-cutoff offline render is silent";
    EXPECT_LT(rmsClosed, 0.4f * rmsOpen)
        << "a 20 Hz lowpass must attenuate the render — the automation lane "
           "is inert offline (B11)";
    EXPECT_TRUE(wavBytesDiffer(outClosed, outOpen))
        << "different automation curves produced IDENTICAL offline renders (B11)";

    outClosed.deleteFile();
    outOpen.deleteFile();
}
