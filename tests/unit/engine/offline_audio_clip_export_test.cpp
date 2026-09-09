#include <gtest/gtest.h>
#include "engine/ExportManager.h"
#include "model/ProjectModel.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;
constexpr double kPi = 3.14159265358979323846;

struct WavStats
{
    float peakL = 0.0f;
    float peakR = 0.0f;
    double rmsL = 0.0;
    double rmsR = 0.0;
    int blocksWithSignalL = 0;
    int blocksWithSignalR = 0;
    int totalBlocks = 0;
};

juce::File tempFile(const juce::String& name)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile(name);
}

juce::File writeSineWav(const juce::String& name, int channels, double seconds,
                        float amplitude = 0.6f)
{
    const int lengthSamples = static_cast<int>(seconds * kSampleRate);
    juce::File f = tempFile(name);
    f.deleteFile();

    juce::AudioBuffer<float> buf(channels, lengthSamples);
    for (int s = 0; s < lengthSamples; ++s)
    {
        const float left = amplitude * static_cast<float>(std::sin(2.0 * kPi * 440.0 * s / kSampleRate));
        const float right = amplitude * static_cast<float>(std::sin(2.0 * kPi * 220.0 * s / kSampleRate));
        buf.setSample(0, s, left);
        if (channels > 1)
            buf.setSample(1, s, right);
    }

    std::unique_ptr<juce::FileOutputStream> out(f.createOutputStream());
    if (out == nullptr)
        return f;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(out.get(), kSampleRate, static_cast<unsigned int>(channels), 16, {}, 0));
    if (writer == nullptr)
        return f;
    out.release();
    if (!writer->writeFromAudioSampleBuffer(buf, 0, lengthSamples))
        return f;
    return f;
}

juce::ValueTree makeProjectWithAudioClip(const juce::File& source, double durationSeconds)
{
    ProjectModel model;
    auto clip = model.createAudioClip("offline-audio", 0.0, durationSeconds,
                                      source.getFullPathName());
    auto tracks = model.getTrackListTree();
    EXPECT_TRUE(tracks.isValid());
    EXPECT_GT(tracks.getNumChildren(), 0);
    auto clipList = tracks.getChild(0).getChildWithName(IDs::CLIP_LIST);
    EXPECT_TRUE(clipList.isValid());
    clipList.removeAllChildren(nullptr);
    clipList.addChild(clip, -1, nullptr);
    return model.getTree().createCopy();
}

bool waitForExport(HDAW::ExportManager& em, int timeoutMs = 60000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + timeoutMs;
    while (em.isExporting() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(10);
    return !em.isExporting();
}

void exportProject(const juce::ValueTree& project, const juce::File& outFile,
                   double durationSeconds)
{
    outFile.deleteFile();
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    HDAW::ExportManager em;
    ASSERT_TRUE(em.startExport(project, fm, nullptr, outFile, kSampleRate, 0.0,
                               durationSeconds, HDAW::ExportManager::WAV, 24));
    ASSERT_TRUE(waitForExport(em));
    ASSERT_TRUE(outFile.existsAsFile());
    ASSERT_GT(outFile.getSize(), 1000);
}

WavStats measureWav(const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(f));
    EXPECT_NE(reader, nullptr);
    WavStats stats;
    if (!reader)
        return stats;

    const int channels = juce::jmin(2, static_cast<int>(reader->numChannels));
    const int64_t length = reader->lengthInSamples;
    juce::AudioBuffer<float> block(juce::jmax(2, channels), kBlockSize);
    double sumSq[2] = { 0.0, 0.0 };
    int64_t samplesSeen = 0;

    for (int64_t pos = 0; pos < length; pos += kBlockSize)
    {
        const int n = static_cast<int>(std::min<int64_t>(kBlockSize, length - pos));
        block.clear();
        reader->read(&block, 0, n, pos, true, true);
        float blockPeak[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = channels > 1 ? ch : 0;
            for (int s = 0; s < n; ++s)
            {
                const float v = block.getSample(srcCh, s);
                blockPeak[ch] = std::max(blockPeak[ch], std::abs(v));
                sumSq[ch] += static_cast<double>(v) * static_cast<double>(v);
            }
        }
        stats.peakL = std::max(stats.peakL, blockPeak[0]);
        stats.peakR = std::max(stats.peakR, blockPeak[1]);
        if (blockPeak[0] > 0.01f) ++stats.blocksWithSignalL;
        if (blockPeak[1] > 0.01f) ++stats.blocksWithSignalR;
        ++stats.totalBlocks;
        samplesSeen += n;
    }

    if (samplesSeen > 0)
    {
        stats.rmsL = std::sqrt(sumSq[0] / static_cast<double>(samplesSeen));
        stats.rmsR = std::sqrt(sumSq[1] / static_cast<double>(samplesSeen));
    }
    return stats;
}

void expectNonSparseSignal(const WavStats& s)
{
    EXPECT_GT(s.peakL, 0.2f);
    EXPECT_GT(s.peakR, 0.2f);
    EXPECT_GT(s.rmsL, 0.05);
    EXPECT_GT(s.rmsR, 0.05);
    ASSERT_GT(s.totalBlocks, 0);
    EXPECT_GT(s.blocksWithSignalL, static_cast<int>(s.totalBlocks * 0.80));
    EXPECT_GT(s.blocksWithSignalR, static_cast<int>(s.totalBlocks * 0.80));
}

} // namespace

TEST(OfflineAudioClipExport, StereoLevelSurvivesProjectTreeReload)
{
    // >8s forces the StreamingClipSource path; the regression was sparse/quiet
    // exports after rebuilding from a cold project tree copy.
    juce::File source = writeSineWav("hdaw_offline_stereo_source.wav", 2, 12.0);
    const juce::ValueTree project = makeProjectWithAudioClip(source, 10.0);
    const juce::ValueTree reloaded = project.createCopy();

    const juce::File first = tempFile("hdaw_offline_stereo_first.wav");
    const juce::File afterReload = tempFile("hdaw_offline_stereo_after_reload.wav");
    exportProject(project, first, 10.0);
    exportProject(reloaded, afterReload, 10.0);

    const WavStats a = measureWav(first);
    const WavStats b = measureWav(afterReload);
    expectNonSparseSignal(a);
    expectNonSparseSignal(b);

    EXPECT_NEAR(b.peakL, a.peakL, a.peakL * 0.10f);
    EXPECT_NEAR(b.peakR, a.peakR, a.peakR * 0.10f);
    EXPECT_NEAR(b.rmsL, a.rmsL, a.rmsL * 0.10);
    EXPECT_NEAR(b.rmsR, a.rmsR, a.rmsR * 0.10);

    source.deleteFile();
    first.deleteFile();
    afterReload.deleteFile();
}

TEST(OfflineAudioClipExport, MonoClipExportsToBothOutputChannels)
{
    // Mono RAVE output must upmix to both output channels and must not zero the mix.
    juce::File source = writeSineWav("hdaw_offline_mono_source.wav", 1, 12.0);
    const juce::ValueTree project = makeProjectWithAudioClip(source, 10.0);

    const juce::File out = tempFile("hdaw_offline_mono_export.wav");
    exportProject(project, out, 10.0);
    const WavStats s = measureWav(out);
    expectNonSparseSignal(s);
    EXPECT_NEAR(s.peakR, s.peakL, s.peakL * 0.05f);
    EXPECT_NEAR(s.rmsR, s.rmsL, s.rmsL * 0.05);

    source.deleteFile();
    out.deleteFile();
}
