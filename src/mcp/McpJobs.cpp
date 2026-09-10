#include "McpJobs.h"

#include "McpServer.h"
#include "McpToolDef.h"
#include "McpTools_Private.h"

#include <QJsonDocument>
#include <exception>
#include <thread>

namespace mcp {

McpJobs& McpJobs::instance()
{
    static auto* jobs = new McpJobs();
    return *jobs;
}

int McpJobs::submit(const QString& label, std::function<QJsonObject()> work)
{
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        evictOldestCompletedLocked();
        id = nextId_++;
        Job job;
        job.id = id;
        job.label = label;
        job.sequence = nextSequence_++;
        jobs_[id] = job;
    }

    std::thread([this, id, work = std::move(work)]() mutable {
        try
        {
            QJsonObject result = work();
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(id);
            if (it != jobs_.end())
            {
                it->second.state = "finished";
                it->second.result = std::move(result);
                it->second.error.clear();
                evictOldestCompletedLocked();
            }
        }
        catch (const std::exception& ex)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(id);
            if (it != jobs_.end())
            {
                it->second.state = "failed";
                it->second.error = QString::fromUtf8(ex.what());
                evictOldestCompletedLocked();
            }
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(id);
            if (it != jobs_.end())
            {
                it->second.state = "failed";
                it->second.error = "job threw unknown exception";
                evictOldestCompletedLocked();
            }
        }
    }).detach();

    return id;
}

QJsonObject McpJobs::status(int id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = jobs_.find(id);
    if (it == jobs_.end())
        return {};

    const auto& job = it->second;
    QJsonObject out{{"jobId", job.id}, {"label", job.label}, {"state", job.state}};
    if (job.state == "finished")
        out["result"] = job.result;
    else if (job.state == "failed")
        out["error"] = job.error;
    return out;
}

void McpJobs::evictOldestCompletedLocked()
{
    while (jobs_.size() > kMaxRetainedJobs)
    {
        auto oldest = jobs_.end();
        for (auto it = jobs_.begin(); it != jobs_.end(); ++it)
        {
            if (it->second.state == "running")
                continue;
            if (oldest == jobs_.end() || it->second.sequence < oldest->second.sequence)
                oldest = it;
        }
        if (oldest == jobs_.end())
            break;
        jobs_.erase(oldest);
    }
}

void registerJobTools(McpServer& s)
{
    s.registerTool({"poll_job",
        "Poll an asynchronous MCP analysis job created by analyze_tuning or mix_report with wait:false. "
        "Returns {jobId,label,state} while running and includes result on finished or error on failed.",
        objSchema({{"jobId", QJsonObject{{"type","integer"}}}}, {"jobId"}),
        "audio",
        [](const QJsonObject& a) -> McpToolResult {
            const int jobId = a.value("jobId").toInt(0);
            QJsonObject status = McpJobs::instance().status(jobId);
            if (status.isEmpty())
                return McpToolResult::text("unknown jobId", true);
            return McpToolResult::text(QString::fromUtf8(
                QJsonDocument(status).toJson(QJsonDocument::Compact)));
        }});
}

} // namespace mcp
