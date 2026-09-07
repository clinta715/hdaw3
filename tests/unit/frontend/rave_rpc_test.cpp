#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include <QThread>

#include "engine/AudioEngine.h"
#include "engine/AudioEngineCommands.h"
#include "frontend/FrontendRouter.h"
#include "model/ProjectModel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace {

void writeFile(const QString& path)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("x");
}

// Small deterministic WAV fixture (no Python/torch needed): 0.1 s 440 Hz
// sine written via JUCE so importAudioFile reads a real audio file.
QString writeSineWavFixture(const QString& path)
{
    juce::WavAudioFormat format;
    juce::File file(path.toStdString());
    file.deleteFile();
    auto stream = file.createOutputStream();
    EXPECT_TRUE(stream != nullptr);
    if (stream == nullptr)
        return {};
    constexpr double sampleRate = 44100.0;
    constexpr int numSamples = 4410;
    juce::AudioBuffer<float> buffer(1, numSamples);
    for (int i = 0; i < numSamples; ++i)
        buffer.setSample(0, i, 0.5f * std::sin(2.0 * 3.141592653589793 * 440.0 * i / sampleRate));
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.release(), sampleRate, 1, 16, {}, 0));
    EXPECT_TRUE(writer != nullptr);
    if (writer == nullptr)
        return {};
    EXPECT_TRUE(writer->writeFromAudioSampleBuffer(buffer, 0, numSamples));
    writer.reset();
    return path;
}

} // namespace

TEST(RaveRpc, ListModelsDispatchesThroughRaveNamespace)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeFile(dir.filePath("model.rave"));
    writeFile(dir.filePath("ignore.wav"));

    AudioEngine engine;
    auto r = frontend::dispatch(engine, "rave.listModels",
                                QJsonObject{{"directory", dir.path()}});

    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    ASSERT_TRUE(r.payload.isObject());
    const auto models = r.payload.toObject().value("models").toArray();
    ASSERT_EQ(models.size(), 1);
    EXPECT_EQ(models.at(0).toObject().value("name").toString(), "model");
    EXPECT_EQ(models.at(0).toObject().value("extension").toString(), ".rave");
}


TEST(RaveRpc, ImportResultImportsWavAsClip)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString wav = writeSineWavFixture(dir.filePath("rave_out.wav"));
    ASSERT_FALSE(wav.isEmpty());

    AudioEngine engine;
    engine.initialize();
    auto r = frontend::dispatch(engine, "rave.importResult",
                                QJsonObject{{"outputPath", wav},
                                            {"alignToGrid", false}});

    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const int clipId = r.payload.toObject().value("clipId").toInt(-1);
    ASSERT_GT(clipId, 0);

    // Verify via the project ValueTree (findClipById is private to the
    // command layer): walk the track list for the new clip id.
    juce::ValueTree clip;
    {
        auto trackList = engine.getProjectModel().getTrackListTree();
        for (int t = 0; t < trackList.getNumChildren() && !clip.isValid(); ++t)
        {
            auto clipList = trackList.getChild(t).getChildWithName(IDs::CLIP_LIST);
            for (int c = 0; c < clipList.getNumChildren(); ++c)
            {
                auto cand = clipList.getChild(c);
                if (static_cast<int>(cand.getProperty(IDs::clipID, 0)) == clipId)
                {
                    clip = cand;
                    break;
                }
            }
        }
    }
    ASSERT_TRUE(clip.isValid());
    EXPECT_EQ(clip.getProperty(IDs::sourceFile).toString().toStdString(), wav.toStdString());
}

TEST(RaveRpc, TransformClipUnknownClipIdReturnsError)
{
    AudioEngine engine;
    auto r = frontend::dispatch(engine, "rave.transformClip",
                                QJsonObject{{"clipId", 999999},
                                            {"modelPath", "model.rave"},
                                            {"outputPath", "out.wav"}});

    EXPECT_TRUE(r.isError);
}

// RAVE #3 async jobs: bounded poll of jobStatus (up to ~5 s). This is the
// only waiting allowed — no sleeps for settling anywhere else.
namespace {
QJsonObject pollRaveJob(AudioEngine& engine, double jobId, QString& stateOut)
{
    QJsonObject last;
    stateOut.clear();
    for (int i = 0; i < 100; ++i)
    {
        auto r = frontend::dispatch(engine, "rave.jobStatus", QJsonObject{{"jobId", jobId}});
        if (r.isError)
            return {};
        last = r.payload.toObject();
        stateOut = last.value("state").toString();
        if (stateOut == "finished" || stateOut == "failed" || stateOut == "cancelled")
            return last;
        QThread::msleep(50);
    }
    return last;
}
} // namespace

TEST(RaveRpc, StartTransformFailFastReachesTerminalFailed)
{
    AudioEngine engine;
    auto r = frontend::dispatch(engine, "rave.startTransform",
                                QJsonObject{{"inputPath", "definitely/missing/input.wav"},
                                            {"modelPath", "model.rave"},
                                            {"outputPath", "out.wav"}});
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const double jobId = r.payload.toObject().value("jobId").toDouble(0);
    ASSERT_GT(jobId, 0);

    // Fail-fast validation completes without spawning a process, so the job
    // is already terminal — the poll returns on its first iteration.
    QString state;
    const QJsonObject status = pollRaveJob(engine, jobId, state);
    ASSERT_EQ(state.toStdString(), "failed");
    EXPECT_TRUE(status.value("message").toString().contains("input file not found"));
    EXPECT_FALSE(status.value("result").toObject().value("ok").toBool(true));
}

TEST(RaveRpc, CancelUnknownJobIdReturnsNotFound)
{
    AudioEngine engine;
    auto r = frontend::dispatch(engine, "rave.cancelJob", QJsonObject{{"jobId", 987654321.0}});
    ASSERT_TRUE(r.isError);
    EXPECT_TRUE(r.payload.toObject().value("message").toString().contains("not found"));

    auto s = frontend::dispatch(engine, "rave.jobStatus", QJsonObject{{"jobId", 987654321.0}});
    EXPECT_TRUE(s.isError);
}

TEST(RaveRpc, CancelRunningJobReachesCancelled)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString input = dir.filePath("in.wav");
    writeFile(input);
    const QString model = dir.filePath("model.rave");
    writeFile(model);
    const QString output = dir.filePath("out.wav");

    // Long-running stand-in sidecar that ignores its args: the launch passes
    // scriptPath first, so any interpreter that runs a script file works.
    QString pythonPath;
    QString scriptPath;
#ifdef Q_OS_WIN
    scriptPath = dir.filePath("sleep_rave.bat");
    {
        QFile f(scriptPath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("@echo off\r\nping 127.0.0.1 -n 30 >nul\r\nexit /b 0\r\n");
    }
    pythonPath = "cmd.exe";
#else
    scriptPath = dir.filePath("sleep_rave.sh");
    {
        QFile f(scriptPath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("#!/bin/sh\nsleep 25\n");
        f.close();
        QFile::setPermissions(scriptPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    pythonPath = "sh";
#endif

    AudioEngine engine;
    auto r = frontend::dispatch(engine, "rave.startTransform",
                                QJsonObject{{"inputPath", input},
                                            {"modelPath", model},
                                            {"outputPath", output},
                                            {"pythonPath", pythonPath},
                                            {"scriptPath", scriptPath}});
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const double jobId = r.payload.toObject().value("jobId").toDouble(0);
    ASSERT_GT(jobId, 0);

    auto c = frontend::dispatch(engine, "rave.cancelJob", QJsonObject{{"jobId", jobId}});
    ASSERT_FALSE(c.isError) << c.payload.toObject().value("message").toString().toStdString();

    QString state;
    pollRaveJob(engine, jobId, state);
    EXPECT_EQ(state.toStdString(), "cancelled");
}