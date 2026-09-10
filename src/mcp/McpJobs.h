#pragma once

#include <QJsonObject>
#include <QString>
#include <functional>
#include <mutex>
#include <map>

namespace mcp {
class McpServer;

class McpJobs
{
public:
    static McpJobs& instance();

    int submit(const QString& label, std::function<QJsonObject()> work);
    QJsonObject status(int id) const;

private:
    McpJobs() = default;
    McpJobs(const McpJobs&) = delete;
    McpJobs& operator=(const McpJobs&) = delete;

    struct Job
    {
        int id = 0;
        QString label;
        QString state = "running";
        QJsonObject result;
        QString error;
        quint64 sequence = 0;
    };

    void evictOldestCompletedLocked();

    mutable std::mutex mutex_;
    std::map<int, Job> jobs_;
    int nextId_ = 1;
    quint64 nextSequence_ = 1;
    static constexpr std::size_t kMaxRetainedJobs = 64;
};

void registerJobTools(McpServer& s);

} // namespace mcp
