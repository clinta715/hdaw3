#pragma once
// ============================================================================
// HDAW proxy param-delivery trace (Phase C2a) - env-gated, logging-only.
//
// Enable with HDAW_TRACE_PARAM=1 -> writes %TEMP%\hdaw_paramtrace_<pid>.log
// (TEMP, fallback TMP, fallback current dir; appended). Every line carries a
// "[<pid> +<steady-ms>]" prefix, where steady-ms is the delta from the
// logger's first use (steady_clock). PARAM_TRACE(...) compiles to a single
// disabled branch when the env gate is off: no behavior changes when
// HDAW_TRACE_PARAM is unset.
//
// Plain standard C++ - no JUCE, no Qt. For diagnostic use only.
// ============================================================================

#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace paramtrace {

// True iff HDAW_TRACE_PARAM == "1" (read once at first call, then cached).
inline bool paramTraceEnabled()
{
    static const bool enabled = []() {
        const char* v = std::getenv("HDAW_TRACE_PARAM");
        return v != nullptr && std::strcmp(v, "1") == 0;
    }();
    return enabled;
}

// RAII logger singleton: lazily opens the trace file on first use, closes it
// in the destructor (process exit). Mutex-serialized; safe for multiple
// threads (message thread + audio thread).
class Logger
{
public:
    static Logger& instance()
    {
        static Logger logger;
        return logger;
    }

    ~Logger()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_ != nullptr)
        {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    void write(const char* fmt, va_list args)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_ == nullptr)
            openLocked();
        if (file_ == nullptr)
            return;

        const auto now = std::chrono::steady_clock::now();
        const long ms = firstUseSteady_.time_since_epoch().count() == 0
            ? 0L
            : static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    now - firstUseSteady_).count());
        std::fprintf(file_, "[%ld +%ld] ", pid_, ms);
        std::vfprintf(file_, fmt, args);
        std::fprintf(file_, "\n");
        std::fflush(file_);
    }

private:
    static long processId()
    {
#ifdef _WIN32
        return static_cast<long>(GetCurrentProcessId());
#else
        return static_cast<long>(getpid());
#endif
    }

    Logger() : pid_(processId()) {}

    void openLocked()
    {
        char path[1024] = { 0 };
        const char* env = std::getenv("TEMP");
        if (env == nullptr || env[0] == '\0')
            env = std::getenv("TMP");
        if (env != nullptr && env[0] != '\0')
        {
            std::snprintf(path, sizeof(path), "%s", env);
            const size_t len = std::strlen(path);
            if (len > 0 && path[len - 1] != '\\' && path[len - 1] != '/')
                std::snprintf(path + len, sizeof(path) - len, "\\");
            std::snprintf(path + std::strlen(path), sizeof(path) - std::strlen(path),
                          "hdaw_paramtrace_%ld.log", pid_);
        }
        else
        {
            std::snprintf(path, sizeof(path), "hdaw_paramtrace_%ld.log", pid_);
        }
        file_ = std::fopen(path, "ab");
        firstUseSteady_ = std::chrono::steady_clock::now();
    }

    std::FILE* file_ = nullptr;
    long pid_ = 0;
    std::chrono::steady_clock::time_point firstUseSteady_{};
    std::mutex mutex_;
};

inline void paramTrace(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    Logger::instance().write(fmt, args);
    va_end(args);
}

} // namespace paramtrace

#define PARAM_TRACE(...)                                        \
    do {                                                        \
        if (paramtrace::paramTraceEnabled())                    \
            paramtrace::paramTrace(__VA_ARGS__);                \
    } while (0)
