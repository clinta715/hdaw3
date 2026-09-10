#include <gtest/gtest.h>
#include "engine/AudioEngine.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "mcp/McpTransportLoopback.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <memory>

namespace {

QJsonObject parseOne(const QByteArray& buf)
{
    int nl = buf.indexOf('\n');
    QByteArray line = nl >= 0 ? buf.left(nl) : buf;
    return QJsonDocument::fromJson(line).object();
}

QString writeFixtureWav(const char* tag)
{
    juce::File f = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile(tag, ".wav", false);
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> fos(f.createOutputStream());
    if (fos == nullptr)
        return {};
    std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(fos.get(), 48000.0, 1, 16, {}, 0));
    if (w == nullptr)
        return {};
    fos.release();

    constexpr double sr = 48000.0;
    const int total = static_cast<int>(sr * 2.0);
    juce::AudioBuffer<float> buf(1, total);
    buf.clear();
    for (int i = 0; i < total; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        const float sample = static_cast<float>(0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 110.0 * t)
            + 0.05 * std::sin(2.0 * juce::MathConstants<double>::pi * 1200.0 * t));
        buf.setSample(0, i, sample);
    }
    w->writeFromAudioSampleBuffer(buf, 0, total);
    w.reset();
    return QString::fromStdString(f.getFullPathName().toStdString());
}

class McpJobs : public ::testing::Test
{
protected:
    void SetUp() override
    {
        engine = std::make_unique<AudioEngine>();
        engine->initialize();
        server = std::make_unique<mcp::McpServer>();
        server->setEngine(engine.get());
        mcp::registerAllTools(*server);
        loopback = std::make_unique<mcp::TransportLoopback>();
        server->setTransport(loopback.get());
        server->start();
    }

    void TearDown() override
    {
        server->stop();
        server->setTransport(nullptr);
        loopback.reset();
        server.reset();
        engine.reset();
    }

    QJsonObject call(const char* method, const QJsonObject& args = {})
    {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = nextId_++;
        req["method"] = "tools/call";
        req["params"] = QJsonObject{{"name", method}, {"arguments", args}};
        loopback->drainOutgoing();
        loopback->pumpIncoming(QJsonDocument(req).toJson(QJsonDocument::Compact));
        QByteArray out;
        if (!loopback->waitForOutgoing(500, &out))
            return {};
        return parseOne(out).value("result").toObject();
    }

    bool isError(const QJsonObject& r) const { return r.value("isError").toBool(false); }

    QString text(const QJsonObject& r) const
    {
        const auto content = r.value("content").toArray();
        if (content.isEmpty())
            return {};
        return content[0].toObject().value("text").toString();
    }

    QJsonObject textObject(const QJsonObject& r) const
    {
        return QJsonDocument::fromJson(text(r).toUtf8()).object();
    }

    QJsonObject pollUntilFinished(int jobId)
    {
        QJsonObject status;
        for (int i = 0; i < 200; ++i)
        {
            auto r = call("poll_job", {{"jobId", jobId}});
            if (!isError(r))
                status = textObject(r);
            const QString state = status.value("state").toString();
            if (state == "finished" || state == "failed")
                return status;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(25);
        }
        return status;
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<mcp::McpServer> server;
    std::unique_ptr<mcp::TransportLoopback> loopback;
    int nextId_ = 1;
};

TEST_F(McpJobs, MixReportWaitFalsePollMatchesSynchronousResult)
{
    const QString wavPath = writeFixtureWav("hdaw_mcp_jobs_mix");
    ASSERT_FALSE(wavPath.isEmpty());

    const QJsonObject args{{"filePath", wavPath}, {"bpm", 120.0}};
    auto sync = call("mix_report", args);
    ASSERT_FALSE(isError(sync)) << text(sync).toStdString();
    const QJsonObject syncObj = textObject(sync);

    QElapsedTimer timer;
    timer.start();
    auto async = call("mix_report", QJsonObject{{"filePath", wavPath}, {"bpm", 120.0}, {"wait", false}});
    const qint64 elapsedMs = timer.elapsed();
    ASSERT_FALSE(isError(async)) << text(async).toStdString();
    EXPECT_LT(elapsedMs, 100);
    const QJsonObject started = textObject(async);
    const int jobId = started.value("jobId").toInt(0);
    ASSERT_GT(jobId, 0);
    EXPECT_EQ(started.value("state").toString().toStdString(), "running");
    EXPECT_EQ(started.value("pollWith").toString().toStdString(), "poll_job");

    const QJsonObject status = pollUntilFinished(jobId);
    ASSERT_EQ(status.value("state").toString().toStdString(), "finished")
        << QJsonDocument(status).toJson(QJsonDocument::Compact).constData();
    EXPECT_EQ(status.value("result").toObject(), syncObj);

    juce::File(wavPath.toStdString()).deleteFile();
}

TEST_F(McpJobs, AnalyzeTuningWaitFalsePollMatchesSynchronousResult)
{
    const QString wavPath = writeFixtureWav("hdaw_mcp_jobs_tuning");
    ASSERT_FALSE(wavPath.isEmpty());

    const QJsonObject args{{"wavPath", wavPath}, {"role", "bass"}};
    auto sync = call("analyze_tuning", args);
    ASSERT_FALSE(isError(sync)) << text(sync).toStdString();
    const QJsonObject syncObj = textObject(sync);

    QElapsedTimer timer;
    timer.start();
    auto async = call("analyze_tuning", QJsonObject{{"wavPath", wavPath}, {"role", "bass"}, {"wait", false}});
    const qint64 elapsedMs = timer.elapsed();
    ASSERT_FALSE(isError(async)) << text(async).toStdString();
    EXPECT_LT(elapsedMs, 100);
    const QJsonObject started = textObject(async);
    const int jobId = started.value("jobId").toInt(0);
    ASSERT_GT(jobId, 0);
    EXPECT_EQ(started.value("state").toString().toStdString(), "running");
    EXPECT_EQ(started.value("pollWith").toString().toStdString(), "poll_job");

    const QJsonObject status = pollUntilFinished(jobId);
    ASSERT_EQ(status.value("state").toString().toStdString(), "finished")
        << QJsonDocument(status).toJson(QJsonDocument::Compact).constData();
    EXPECT_EQ(status.value("result").toObject(), syncObj);

    juce::File(wavPath.toStdString()).deleteFile();
}

TEST_F(McpJobs, PollJobUnknownIdErrors)
{
    auto r = call("poll_job", {{"jobId", 987654321}});
    EXPECT_TRUE(isError(r));
    EXPECT_TRUE(text(r).contains("unknown jobId"));
}

} // namespace
