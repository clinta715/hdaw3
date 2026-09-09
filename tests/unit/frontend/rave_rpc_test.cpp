#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QDir>

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

TEST(RaveRpc, ProbeModelDispatchesThroughRaveNamespace)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString model = dir.filePath("model.rave");
    writeFile(model);
    const QString script = dir.filePath("dummy_sidecar.py");
    writeFile(script);

#ifdef Q_OS_WIN
    const QString runner = dir.filePath("rave_probe_rpc.bat");
    QFile runnerFile(runner);
    ASSERT_TRUE(runnerFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    runnerFile.write("@echo off\r\necho {^\"ok^\":true,^\"methods^\":[^\"encode^\",^\"decode^\"],^\"sampleRate^\":44100,^\"latentDim^\":16,^\"latentFrames^\":22,^\"encodeShape^\":[1,16,22],^\"decodeShape^\":[1,2,45056],^\"error^\":^\"^\"}\r\nexit /b 0\r\n");
    runnerFile.close();
#else
    const QString runner = dir.filePath("rave_probe_rpc.sh");
    QFile runnerFile(runner);
    ASSERT_TRUE(runnerFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    runnerFile.write("#!/bin/sh\necho '{\"ok\":true,\"methods\":[\"encode\",\"decode\"],\"sampleRate\":44100,\"latentDim\":16,\"latentFrames\":22,\"encodeShape\":[1,16,22],\"decodeShape\":[1,2,45056],\"error\":\"\"}'\nexit 0\n");
    runnerFile.close();
    ASSERT_TRUE(QFile::setPermissions(runner, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
#endif

    AudioEngine engine;
    auto r = frontend::dispatch(engine, "rave.probeModel",
                                QJsonObject{{"modelPath", model},
                                            {"pythonPath", runner},
                                            {"scriptPath", script}});

    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const auto o = r.payload.toObject();
    EXPECT_TRUE(o.value("ok").toBool(false));
    EXPECT_EQ(o.value("latentDim").toInt(), 16);
    EXPECT_EQ(o.value("sampleRate").toInt(), 44100);
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

TEST(RaveRpc, ImportResultTimelineAlignedSetsSourceOffset)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString wav = writeSineWavFixture(dir.filePath("rave_timeline_aligned.wav"));
    ASSERT_FALSE(wav.isEmpty());

    AudioEngine engine;
    engine.initialize();
    auto r = frontend::dispatch(engine, "rave.importResult",
                                QJsonObject{{"outputPath", wav},
                                            {"trackIndex", 0},
                                            {"startBeats", 128.0},
                                            {"timelineAligned", true},
                                            {"alignToGrid", false}});

    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const auto payload = r.payload.toObject();
    const int clipId = payload.value("clipId").toInt(-1);
    ASSERT_GT(clipId, 0);

    juce::ValueTree clip;
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
    ASSERT_TRUE(clip.isValid());
    const double expected = 128.0 * 60.0 / engine.getTransportManager().getBPM();
    EXPECT_NEAR(static_cast<double>(clip.getProperty(IDs::offset, 0.0)), expected, 1.0e-9);
    EXPECT_NEAR(payload.value("sourceOffsetSeconds").toDouble(-1.0), expected, 1.0e-9);
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

namespace {
QJsonObject pollRaveTrainingJob(AudioEngine& engine, double jobId, QString& stateOut)
{
    QJsonObject last;
    stateOut.clear();
    for (int i = 0; i < 100; ++i)
    {
        auto r = frontend::dispatch(engine, "rave.trainingJobStatus", QJsonObject{{"jobId", jobId}});
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

TEST(RaveRpc, StartTrainingFailFastMissingDatasetReachesTerminalFailed)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString script = dir.filePath("train_sidecar.py");
    writeFile(script);

    AudioEngine engine;
    auto r = frontend::dispatch(engine, "rave.startTraining",
                                QJsonObject{{"datasetPath", dir.filePath("missing_dataset")},
                                            {"outputModelPath", dir.filePath("out.ts")},
                                            {"scriptPath", script}});
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const double jobId = r.payload.toObject().value("jobId").toDouble(0);
    ASSERT_GT(jobId, 0);

    QString state;
    const QJsonObject status = pollRaveTrainingJob(engine, jobId, state);
    ASSERT_EQ(state.toStdString(), "failed");
    EXPECT_TRUE(status.value("message").toString().contains("dataset directory not found"));
    EXPECT_FALSE(status.value("result").toObject().value("ok").toBool(true));
}

TEST(RaveRpc, StartTrainingRejectsInvalidNumericParams)
{
    AudioEngine engine;
    auto r = frontend::dispatch(engine, "rave.startTraining",
                                QJsonObject{{"datasetPath", "dataset"},
                                            {"outputModelPath", "out.ts"},
                                            {"epochs", 0},
                                            {"batchSize", 8},
                                            {"sampleRate", 44100}});
    ASSERT_TRUE(r.isError);
    EXPECT_TRUE(r.payload.toObject().value("message").toString().contains("must be > 0"));
}

TEST(RaveRpc, TrainingUnknownStatusAndCancelReturnNotFound)
{
    AudioEngine engine;
    auto s = frontend::dispatch(engine, "rave.trainingJobStatus", QJsonObject{{"jobId", 987654321.0}});
    ASSERT_TRUE(s.isError);
    EXPECT_TRUE(s.payload.toObject().value("message").toString().contains("not found"));

    auto c = frontend::dispatch(engine, "rave.cancelTrainingJob", QJsonObject{{"jobId", 987654321.0}});
    ASSERT_TRUE(c.isError);
    EXPECT_TRUE(c.payload.toObject().value("message").toString().contains("not found"));
}

TEST(RaveRpc, CancelRunningTrainingJobReachesCancelled)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString dataset = dir.filePath("dataset");
    ASSERT_TRUE(QDir().mkpath(dataset));
    writeFile(dataset + "/one.wav");
    const QString output = dir.filePath("trained.ts");

    QString pythonPath;
    QString script;
#ifdef Q_OS_WIN
    script = dir.filePath("sleep_train.bat");
    {
        QFile f(script);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("@echo off\r\nping 127.0.0.1 -n 30 >nul\r\nexit /b 0\r\n");
    }
    pythonPath = "cmd.exe";
#else
    script = dir.filePath("sleep_train.sh");
    {
        QFile f(script);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("#!/bin/sh\nsleep 25\n");
        f.close();
        QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    pythonPath = "sh";
#endif

    AudioEngine engine;
    auto r = frontend::dispatch(engine, "rave.startTraining",
                                QJsonObject{{"datasetPath", dataset},
                                            {"outputModelPath", output},
                                            {"name", "test_model"},
                                            {"epochs", 1},
                                            {"batchSize", 1},
                                            {"sampleRate", 44100},
                                            {"pythonPath", pythonPath},
                                            {"scriptPath", script}});
    ASSERT_FALSE(r.isError) << r.payload.toObject().value("message").toString().toStdString();
    const double jobId = r.payload.toObject().value("jobId").toDouble(0);
    ASSERT_GT(jobId, 0);

    auto c = frontend::dispatch(engine, "rave.cancelTrainingJob", QJsonObject{{"jobId", jobId}});
    ASSERT_FALSE(c.isError) << c.payload.toObject().value("message").toString().toStdString();

    QString state;
    pollRaveTrainingJob(engine, jobId, state);
    EXPECT_EQ(state.toStdString(), "cancelled");
}
