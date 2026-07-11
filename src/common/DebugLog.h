#pragma once
#include <mutex>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <string>

namespace DebugLog {
    inline std::ofstream& logFile() {
        static std::ofstream f;
        static bool initialized = false;
        if (!initialized) {
            std::string path;
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
            if (const char* temp = std::getenv("TEMP"))
                path = temp;
            else if (const char* tmp = std::getenv("TMP"))
                path = tmp;
            else
                path = ".";
#ifdef _MSC_VER
#pragma warning(pop)
#endif

            path += "/hdaw_debug.log";
            f.open(path, std::ios::app);
            if (!f.is_open())
                fputs("HDAW: Failed to open debug log file\n", stderr);
            initialized = true;
        }
        return f;
    }

    inline std::mutex& logMutex() {
        static std::mutex m;
        return m;
    }

    inline std::string toLogString(const char* s) { return s ? s : ""; }
    inline std::string toLogString(const std::string& s) { return s; }

    template<typename T>
    auto toLogString(const T& s) -> decltype(s.toStdString()) { return s.toStdString(); }

    inline void log(const std::string& tag, const std::string& message) {
        std::lock_guard<std::mutex> locker(logMutex());
        std::ofstream& f = logFile();
        if (f.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm utc;
#ifdef _WIN32
            gmtime_s(&utc, &t);
#else
            gmtime_r(&t, &utc);
#endif
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                utc.tm_hour, utc.tm_min, utc.tm_sec, static_cast<int>(ms.count()));
            f << "{\"ts\":\"" << buf << "\",\"tag\":\"" << tag
              << "\",\"msg\":\"" << message << "\"}\n";
            f.flush();
        }
    }
}

#define HDAW_LOG(tag, msg) DebugLog::log(DebugLog::toLogString(tag), DebugLog::toLogString(msg))
