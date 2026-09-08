#include <gtest/gtest.h>

#include "engine/RaveService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <algorithm>
#include <vector>

namespace {

QString writeEmptyFile(QTemporaryDir& dir, const QString& name)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();
    return path;
}

std::vector<juce::String> namesOf(const std::vector<HDAW::RaveModelInfo>& models)
{
    std::vector<juce::String> names;
    for (const auto& model : models)
        names.push_back(model.name);
    return names;
}

} // namespace

TEST(RaveService, ListsSupportedModelFilesFromExplicitDirectory)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    writeEmptyFile(dir, "alpha.ts");
    writeEmptyFile(dir, "bravo.pt");
    writeEmptyFile(dir, "charlie.pth");
    writeEmptyFile(dir, "delta.rave");
    writeEmptyFile(dir, "echo.onnx");
    writeEmptyFile(dir, "ignored.wav");
    writeEmptyFile(dir, "ignored.txt");

    const HDAW::RaveService service;
    const auto models = service.listModels(juce::File(dir.path().toStdString()));

    ASSERT_EQ(models.size(), 5u);
    const auto names = namesOf(models);
    EXPECT_NE(std::find(names.begin(), names.end(), "alpha"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "bravo"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "charlie"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "delta"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "echo"), names.end());
    for (const auto& model : models)
    {
        EXPECT_TRUE(model.extension == ".ts" || model.extension == ".pt" ||
                    model.extension == ".pth" || model.extension == ".rave" ||
                    model.extension == ".onnx");
        EXPECT_GT(model.sizeBytes, 0);
        EXPECT_TRUE(juce::File(model.path).existsAsFile());
    }
}

TEST(RaveService, MissingExplicitDirectoryReturnsEmptyList)
{
    const HDAW::RaveService service;
    const auto models = service.listModels(juce::File(QDir::temp().filePath("hdaw_missing_rave_models").toStdString()));
    EXPECT_TRUE(models.empty());
}

TEST(RaveService, TransformValidatesInputBeforeSidecar)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto model = writeEmptyFile(dir, "model.rave");

    HDAW::RaveTransformRequest req;
    req.inputPath = dir.filePath("missing.wav").toStdString();
    req.modelPath = model.toStdString();
    req.outputPath = dir.filePath("out.wav").toStdString();
    req.scriptPath = dir.filePath("missing_script.py").toStdString();

    const HDAW::RaveService service;
    const auto result = service.transformFile(req);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.contains("input file not found"));
}

TEST(RaveService, TransformValidatesModelBeforeSidecar)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto input = writeEmptyFile(dir, "input.wav");

    HDAW::RaveTransformRequest req;
    req.inputPath = input.toStdString();
    req.modelPath = dir.filePath("missing.rave").toStdString();
    req.outputPath = dir.filePath("out.wav").toStdString();
    req.scriptPath = dir.filePath("missing_script.py").toStdString();

    const HDAW::RaveService service;
    const auto result = service.transformFile(req);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.contains("model file not found"));
}

TEST(RaveService, TransformRequiresConfiguredScript)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto input = writeEmptyFile(dir, "input.wav");
    const auto model = writeEmptyFile(dir, "model.onnx");

    HDAW::RaveTransformRequest req;
    req.inputPath = input.toStdString();
    req.modelPath = model.toStdString();
    req.outputPath = dir.filePath("out.wav").toStdString();
    req.scriptPath = dir.filePath("missing_script.py").toStdString();

    const HDAW::RaveService service;
    const auto result = service.transformFile(req);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.contains("script not found"));
}

TEST(RaveService, TransformFailsWhenSidecarExitsZeroWithoutOutput)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto input = writeEmptyFile(dir, "input.wav");
    const auto model = writeEmptyFile(dir, "model.rave");
    const auto script = writeEmptyFile(dir, "dummy_sidecar.py");

#ifdef Q_OS_WIN
    const QString runner = dir.filePath("rave_no_output.bat");
    QFile runnerFile(runner);
    ASSERT_TRUE(runnerFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    runnerFile.write("@echo off\r\necho sidecar completed without output\r\nexit /b 0\r\n");
    runnerFile.close();
#else
    const QString runner = dir.filePath("rave_no_output.sh");
    QFile runnerFile(runner);
    ASSERT_TRUE(runnerFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    runnerFile.write("#!/bin/sh\necho sidecar completed without output\nexit 0\n");
    runnerFile.close();
    ASSERT_TRUE(QFile::setPermissions(runner, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
#endif

    HDAW::RaveTransformRequest req;
    req.inputPath = input.toStdString();
    req.modelPath = model.toStdString();
    req.outputPath = dir.filePath("out.wav").toStdString();
    req.pythonPath = runner.toStdString();
    req.scriptPath = script.toStdString();

    const HDAW::RaveService service;
    const auto result = service.transformFile(req);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(juce::File(req.outputPath).existsAsFile());
    EXPECT_TRUE(result.error.contains("did not create output"));
}

TEST(RaveService, ProbeValidatesModelBeforeSidecar)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    HDAW::RaveProbeRequest req;
    req.modelPath = dir.filePath("missing.rave").toStdString();
    req.scriptPath = dir.filePath("missing_script.py").toStdString();

    const HDAW::RaveService service;
    const auto result = service.probeModel(req);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.error.contains("model file not found"));
}

TEST(RaveService, ProbeParsesJsonFromSidecar)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto model = writeEmptyFile(dir, "model.rave");
    const auto script = writeEmptyFile(dir, "dummy_sidecar.py");

#ifdef Q_OS_WIN
    const QString runner = dir.filePath("rave_probe.bat");
    QFile runnerFile(runner);
    ASSERT_TRUE(runnerFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    runnerFile.write("@echo off\r\necho {^\"ok^\":true,^\"methods^\":[^\"encode^\",^\"decode^\"],^\"sampleRate^\":null,^\"latentDim^\":16,^\"latentFrames^\":22,^\"encodeShape^\":[1,16,22],^\"decodeShape^\":[1,2,45056],^\"error^\":^\"^\"}\r\nexit /b 0\r\n");
    runnerFile.close();
#else
    const QString runner = dir.filePath("rave_probe.sh");
    QFile runnerFile(runner);
    ASSERT_TRUE(runnerFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    runnerFile.write("#!/bin/sh\necho '{\"ok\":true,\"methods\":[\"encode\",\"decode\"],\"sampleRate\":null,\"latentDim\":16,\"latentFrames\":22,\"encodeShape\":[1,16,22],\"decodeShape\":[1,2,45056],\"error\":\"\"}'\nexit 0\n");
    runnerFile.close();
    ASSERT_TRUE(QFile::setPermissions(runner, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
#endif

    HDAW::RaveProbeRequest req;
    req.modelPath = model.toStdString();
    req.pythonPath = runner.toStdString();
    req.scriptPath = script.toStdString();

    const HDAW::RaveService service;
    const auto result = service.probeModel(req);
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_TRUE(result.payload.value("ok").toBool(false));
    EXPECT_EQ(result.payload.value("latentDim").toInt(), 16);
    EXPECT_EQ(result.payload.value("latentFrames").toInt(), 22);
    EXPECT_EQ(result.payload.value("methods").toArray().size(), 2);
}
