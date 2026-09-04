#include "McpTransportStdio.h"
#include "McpServer.h"
#include "McpJsonRpc.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

namespace mcp {

class TransportStdio::Reader {
public:
    Reader(TransportStdio* p) : parent_(p) {}
    void run() {
        QFile in;
        in.open(STDIN_FILENO, QIODevice::ReadOnly);
#ifdef _WIN32
        // Windows: poll stdin with PeekNamedPipe so stop() can terminate this
        // thread promptly even when the client keeps the pipe open
        // (engine_restart). A blocking readLine can NEVER be interrupted
        // externally: Qt dups the fd inside QFile, and closing the fd/handle
        // from another thread deadlocks or races (UCRT fd-table lock / handle
        // duplication — verified under cdb). Polling avoids touching stdin
        // from stop() altogether.
        const HANDLE stdIn = GetStdHandle(STD_INPUT_HANDLE);
        const bool isPipe = (stdIn != INVALID_HANDLE_VALUE
                             && GetFileType(stdIn) == FILE_TYPE_PIPE);
        QByteArray buf;
        while (!parent_->stopped_.load(std::memory_order_relaxed)) {
            if (isPipe) {
                const HANDLE h = (HANDLE)_get_osfhandle(STDIN_FILENO);
                DWORD avail = 0;
                if (h == INVALID_HANDLE_VALUE
                    || !PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr))
                    break;   // pipe closed or no longer a pipe
                if (avail == 0) {
                    ::Sleep(5);   // bounded poll interval; stopped_ is re-checked
                    continue;
                }
                // Read the EXACT available bytes directly on the pipe handle.
                // Going through Qt's QFile is a trap: its internal buffer makes
                // a small read request a 16 KB ReadFile, which BLOCKS waiting
                // for a full buffer on a byte-mode pipe (verified under cdb).
                // ReadFile with a request <= the peeked count returns at once.
                QByteArray chunk(static_cast<int>(avail), 0);
                DWORD got = 0;
                if (!ReadFile(h, chunk.data(), avail, &got, nullptr) || got == 0)
                    break;
                chunk.truncate(static_cast<int>(got));
                buf.append(chunk);
                int nl = 0;
                while ((nl = buf.indexOf('\n')) >= 0) {
                    const QByteArray raw = buf.left(nl);
                    buf.remove(0, nl + 1);
                    handleLine(QString::fromUtf8(raw));
                }
            } else {
                // Non-pipe stdin (e.g. interactive console): blocking line read
                // that returns on EOF. stop() cannot interrupt this while a
                // console is attached with no input — an acceptable dev-only
                // edge; the MCP stdio path is always a pipe.
                const QByteArray raw = in.readLine();
                if (raw.isNull())
                    break;
                handleLine(QString::fromUtf8(raw));
            }
        }
#else
        QTextStream ts(&in);
        while (!parent_->stopped_.load(std::memory_order_relaxed)) {
            const QString line = ts.readLine();
            if (line.isNull())
                break;
            handleLine(line);
        }
#endif
        if (QCoreApplication::instance()) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), "quit",
                                      Qt::QueuedConnection);
        }
    }
private:
    void handleLine(const QString& line) {
        auto trimmed = line.trimmed();
        if (trimmed.isEmpty())
            return;
        QJsonParseError pe;
        auto doc = QJsonDocument::fromJson(trimmed.toUtf8(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            parent_->send(serializeResponse(
                McpResponse::failure({}, err::ParseError, "invalid JSON")).toUtf8());
            return;
        }
        auto v = validateRequest(doc.object());
        if (std::holds_alternative<McpResponse>(v)) {
            parent_->send(serializeResponse(std::get<McpResponse>(v)).toUtf8());
            return;
        }
        auto& req = std::get<McpRequest>(v);
        QMetaObject::invokeMethod(parent_->server_, "handleRequest",
            Qt::QueuedConnection,
            Q_ARG(QJsonValue, req.id),
            Q_ARG(QString, req.method),
            Q_ARG(QJsonValue, req.params));
    }
    TransportStdio* parent_;
};

void TransportStdio::ReaderThread::run() {
    if (reader_) reader_->run();
}

TransportStdio::TransportStdio() = default;
TransportStdio::~TransportStdio() { stop(); }

bool TransportStdio::start(McpServer* s) {
    server_ = s; stopped_ = false;
    reader_ = new Reader(this);
    readerThread_.setReader(reader_);
    readerThread_.start();
    return true;
}

void TransportStdio::stop() {
    stopped_ = true;
#ifndef _WIN32
    // POSIX fallback: a blocking read() is safely unblocked by close(0)
    // (returns EBADF — POSIX has no fd-table-lock hazard). The Windows reader
    // polls stdin (PeekNamedPipe) and observes stopped_ itself, so no close is
    // needed there. Guard against stop() being called twice (McpServer::stop
    // then ~TransportStdio).
    if (!stdinClosed_) {
        stdinClosed_ = true;
        (void)::close(STDIN_FILENO);
    }
#endif
    readerThread_.wait(2000);   // the reader observes stopped_ within one poll interval
    delete reader_; reader_ = nullptr;
}

void TransportStdio::send(const QByteArray& line) {
    QMutexLocker lk(&stdoutMtx_);
    QFile out;
    out.open(STDOUT_FILENO, QIODevice::WriteOnly);
    out.write(line);
    out.putChar('\n');
    out.flush();
}

void TransportStdio::notify(const QByteArray& line) { send(line); }

} // namespace mcp
