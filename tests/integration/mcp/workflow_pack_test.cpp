#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {

bool isError(const QJsonObject& r) { return r.value("isError").toBool(false); }

QString textOf(const QJsonObject& r)
{
    auto content = r.value("content").toArray();
    if (content.isEmpty())
        return {};
    return content[0].toObject().value("text").toString();
}

QJsonObject resultObj(const QJsonObject& r)
{
    return QJsonDocument::fromJson(textOf(r).toUtf8()).object();
}

QJsonObject call(mcp::McpServer& server, const char* name, const QJsonObject& args = {})
{
    auto r = server.handleRequestOnTestThread(1, "tools/call",
        QJsonObject{{"name", name}, {"arguments", args}});
    return r.toObject();
}

QString writeTestWav(const QString& stem)
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile(stem.toStdString(), ".wav", false);
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    if (!stream)
        return {};
    std::unique_ptr<juce::AudioFormatWriter> writer(
        fmt.createWriterFor(stream.get(), 44100.0, 2, 16, {}, 0));
    if (!writer)
        return {};
    stream.release();
    juce::AudioBuffer<float> buffer(2, 44100);
    buffer.clear();
    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();
    return QString::fromStdString(file.getFullPathName().toStdString());
}

class WorkflowPackTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
};

} // namespace

TEST_F(WorkflowPackTest, ValidateSampleAndSnapshotProject)
{
    const QString wavPath = writeTestWav("hdaw_workflow_validate");
    ASSERT_FALSE(wavPath.isEmpty());

    auto validate = call(*server, "validate_sample", {{"path", wavPath}});
    ASSERT_FALSE(isError(validate)) << textOf(validate).toStdString();
    auto validateJson = resultObj(validate);
    EXPECT_TRUE(validateJson.value("readable").toBool());
    EXPECT_TRUE(validateJson.value("headerOk").toBool());
    EXPECT_GT(validateJson.value("sampleRate").toDouble(), 0.0);
    EXPECT_GT(validateJson.value("channels").toInt(), 0);

    auto snapshot = call(*server, "snapshot_project");
    ASSERT_FALSE(isError(snapshot)) << textOf(snapshot).toStdString();
    auto snapJson = resultObj(snapshot);
    EXPECT_GE(snapJson.value("trackCount").toInt(), 1);
    EXPECT_TRUE(snapJson.contains("transport"));
    EXPECT_TRUE(snapJson.contains("clips"));
}

TEST_F(WorkflowPackTest, BatchImportSamplesImportsMultipleClips)
{
    const QString wavA = writeTestWav("hdaw_workflow_batch_a");
    const QString wavB = writeTestWav("hdaw_workflow_batch_b");
    ASSERT_FALSE(wavA.isEmpty());
    ASSERT_FALSE(wavB.isEmpty());

    QJsonArray samples;
    samples.append(QJsonObject{{"path", wavA}, {"trackIndex", 0}, {"startBeat", 0.0}});
    samples.append(QJsonObject{{"path", wavB}, {"trackIndex", 0}, {"startBeat", 4.0}, {"alignToGrid", false}});

    auto r = call(*server, "batch_import_samples", {{"samples", samples}});
    ASSERT_FALSE(isError(r)) << textOf(r).toStdString();
    auto out = resultObj(r);
    EXPECT_EQ(out.value("imported").toInt(), 2);
    EXPECT_EQ(out.value("results").toArray().size(), 2);

    auto snap = engine->getReadModel().snapshot();
    EXPECT_GE(static_cast<int>(snap.clips.size()), 2);
}

TEST_F(WorkflowPackTest, SetupRemixAndCreateSection)
{
    const QString wavPath = writeTestWav("hdaw_workflow_section");
    ASSERT_FALSE(wavPath.isEmpty());

    auto setup = call(*server, "setup_remix",
        {{"bpm", 147.0}, {"key", "G minor"}, {"structure", QJsonArray{"intro", "drop"}}, {"sectionLengthBeats", 16.0}});
    ASSERT_FALSE(isError(setup)) << textOf(setup).toStdString();
    auto setupJson = resultObj(setup);
    EXPECT_EQ(setupJson.value("regionsCreated").toInt(), 2);
    EXPECT_NEAR(engine->getReadModel().getTransport().bpm, 147.0, 0.001);
    EXPECT_EQ(engine->getReadModel().getScaleRoot(), 7);
    EXPECT_GE(engine->getReadModel().getScaleMode(), 0);

    auto section = call(*server, "create_section", {
        {"sectionName", "drop1"},
        {"startBeat", 32.0},
        {"endBeat", 48.0},
        {"tracks", QJsonArray{
            QJsonObject{{"trackIndex", 0}, {"clips", QJsonArray{
                QJsonObject{{"path", wavPath}, {"startBeat", 32.0}, {"durationBeats", 16.0}, {"name", "bass_loop"}}
            }}},
            QJsonObject{{"trackIndex", 1}, {"clips", QJsonArray{
                QJsonObject{{"path", wavPath}, {"startBeat", 32.0}, {"durationBeats", 8.0}, {"name", "lead"}}
            }}}
        }}
    });
    ASSERT_FALSE(isError(section)) << textOf(section).toStdString();
    auto sectionJson = resultObj(section);
    EXPECT_FALSE(sectionJson.value("regionID").toString().isEmpty());
    EXPECT_EQ(sectionJson.value("placedClips").toArray().size(), 2);

    auto regions = engine->getReadModel().getArrangerRegions();
    bool foundDrop1 = false;
    for (const auto& region : regions)
        if (region.name == "drop1")
            foundDrop1 = true;
    EXPECT_TRUE(foundDrop1);
}

TEST_F(WorkflowPackTest, DebugAudioReturnsStructuredJson)
{
    auto r = call(*server, "debug_audio", {{"trackId", 0}});
    ASSERT_FALSE(isError(r)) << textOf(r).toStdString();
    auto out = resultObj(r);
    EXPECT_TRUE(out.contains("transport"));
    EXPECT_TRUE(out.contains("masterMeter"));
    EXPECT_TRUE(out.contains("tracks"));
    EXPECT_FALSE(out.value("tracks").toArray().isEmpty());
}
