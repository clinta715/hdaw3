// RPC-layer tests for loop grid alignment (Phase 1, Task B).
// Exercises the JSON-RPC dispatch path for project.importAudioFile and
// project.alignClipToGrid through frontend::dispatch(), asserting both the
// JSON result shapes and the ValueTree/ReadModel mutations they cause.
// Mirrors envelope_generation_rpc_test.cpp pattern.

#include <gtest/gtest.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "engine/AudioEngine.h"
#include "frontend/FrontendRouter.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <QString>
#include <cmath>

namespace {

// Writes a 4-bar 120 BPM 4-on-floor percussion loop (8 s total, 16 kicks at
// 0.5 s intervals) to a temporary mono WAV. Returns the temp file; the caller
// deletes it when done.
juce::File makePercussionLoopWav()
{
    auto tempDir = juce::File::getSpecialLocation(
        juce::File::SpecialLocationType::tempDirectory);
    auto f = tempDir.getNonexistentChildFile("hdaw_loop_align_rpc", ".wav", false);

    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> fos(f.createOutputStream());
    jassert(fos != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> w(
        fmt.createWriterFor(fos.get(), 44100, 1, 16, {}, 0));
    fos.release(); // writer owns it now

    const double sampleRate = 44100.0;
    const double beat = 0.5; // 120 BPM
    const int total = static_cast<int>(8.0 * sampleRate);
    juce::AudioBuffer<float> buf(1, total);
    buf.clear();
    const double amp = 0.5;
    const double freq = 55.0;
    const double decay = 0.012;
    const int burstSamples = static_cast<int>(0.04 * sampleRate);
    for (int b = 0; b < 16; ++b)
    {
        const int start = static_cast<int>(b * beat * sampleRate);
        for (int i = 0; i < burstSamples && start + i < total; ++i)
        {
            const double t = static_cast<double>(i) / sampleRate;
            buf.setSample(0, start + i, static_cast<float>(
                amp * std::sin(2.0 * juce::MathConstants<double>::pi * freq * t)
                    * std::exp(-t / decay)));
        }
    }
    w->writeFromAudioSampleBuffer(buf, 0, total);
    w.reset();
    return f;
}

QJsonValue rpc(AudioEngine& engine, const QString& method, const QJsonValue& params = {})
{
    auto r = frontend::dispatch(engine, method, params);
    EXPECT_FALSE(r.isError)
        << "dispatch(" << method.toStdString() << ") returned error: "
        << (r.payload.isObject() ? r.payload.toObject().value("message").toString().toStdString()
                                 : std::string("non-object error"));
    return r.payload;
}

} // namespace

// ─── project.alignClipToGrid ───────────────────────────────────────

TEST(LoopAlignRpc, AlignClipToGrid_HappyPath)
{
    auto wav = makePercussionLoopWav();
    AudioEngine engine;
    engine.initialize();

    // Create an audio clip via RPC (beats at this boundary; 16 beats at the
    // default 120 BPM = 8 s, the full loop length).
    auto clipResp = rpc(engine, "project.addAudioClip",
                        QJsonObject{ { "trackIndex", 0 }, { "start", 0.0 },
                                     { "duration", 16.0 },
                                     { "sourceFile", QString::fromStdString(wav.getFullPathName().toStdString()) },
                                     { "name", "loop" } });
    int clipId = static_cast<int>(clipResp.toDouble());
    ASSERT_GT(clipId, 0);

    auto resp = rpc(engine, "project.alignClipToGrid",
                    QJsonObject{ { "clipId", clipId } });
    ASSERT_TRUE(resp.isObject());
    auto obj = resp.toObject();
    EXPECT_TRUE(obj.value("ok").toBool());
    EXPECT_GE(obj.value("bars").toInt(), 1);
    EXPECT_GT(obj.value("ratio").toDouble(), 0.0);
    EXPECT_GT(obj.value("duration").toDouble(), 0.0);
    EXPECT_FALSE(obj.contains("error"));

    // The ValueTree/ReadModel must reflect stretchMode=2 + a positive ratio.
    auto snap = engine.getReadModel().getClip(clipId);
    EXPECT_EQ(snap.stretchMode, 2);          // ManualRatio
    EXPECT_GT(snap.stretchRatio, 0.0);

    wav.deleteFile();
}

TEST(LoopAlignRpc, AlignClipToGrid_MissingClipFails)
{
    AudioEngine engine;
    engine.initialize();

    auto resp = rpc(engine, "project.alignClipToGrid",
                    QJsonObject{ { "clipId", 99999 } });
    ASSERT_TRUE(resp.isObject());
    auto obj = resp.toObject();
    EXPECT_FALSE(obj.value("ok").toBool());
    EXPECT_TRUE(obj.contains("error"));
    EXPECT_FALSE(obj.value("error").toString().isEmpty());
}

// ─── project.importAudioFile ───────────────────────────────────────

TEST(LoopAlignRpc, ImportAudioFile_HappyPath)
{
    auto wav = makePercussionLoopWav();
    AudioEngine engine;
    engine.initialize();

    auto resp = rpc(engine, "project.importAudioFile",
                    QJsonObject{ { "path", QString::fromStdString(wav.getFullPathName().toStdString()) },
                                 { "trackIndex", 0 }, { "start", 0.0 },
                                 { "alignToGrid", true } });
    ASSERT_TRUE(resp.isObject());
    auto obj = resp.toObject();
    int clipId = obj.value("clipId").toInt();
    ASSERT_GE(clipId, 0);
    EXPECT_TRUE(obj.value("aligned").toBool());
    EXPECT_GT(obj.value("bpm").toDouble(), 0.0);
    EXPECT_GT(obj.value("ratio").toDouble(), 0.0);
    EXPECT_FALSE(obj.contains("error"));

    auto snap = engine.getReadModel().getClip(clipId);
    EXPECT_GT(snap.sourceBpm, 0.0);
    EXPECT_EQ(snap.stretchMode, 2);          // grid-aligned import
    EXPECT_GT(snap.stretchRatio, 0.0);

    wav.deleteFile();
}

TEST(LoopAlignRpc, ImportAudioFile_MissingParamsError)
{
    AudioEngine engine;
    engine.initialize();

    auto r = frontend::dispatch(engine, "project.importAudioFile",
                                QJsonObject{ { "trackIndex", 0 } });
    EXPECT_TRUE(r.isError);
    EXPECT_EQ(r.payload.toObject().value("code").toInt(), -32602);
}