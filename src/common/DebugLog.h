#pragma once
#include <mutex>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

    inline int currentPid() {
#ifdef _WIN32
        return static_cast<int>(::GetCurrentProcessId());
#else
        return 0;
#endif
    }

    inline std::string toLogString(const char* s) { return s ? s : ""; }
    inline std::string toLogString(const std::string& s) { return s; }

    template<typename T>
    auto toLogString(const T& s) -> decltype(s.toStdString()) { return s.toStdString(); }

    inline bool tagAllowed(const std::string& tag) {
        static std::vector<std::string> allowed = [] {
            std::vector<std::string> v;
            if (const char* envVar = std::getenv("HDAW_LOG_TAGS"))
            {
                std::string s(envVar);
                size_t pos = 0;
                while (pos < s.size())
                {
                    size_t comma = s.find(',', pos);
                    if (comma == std::string::npos)
                    {
                        v.push_back(s.substr(pos));
                        break;
                    }
                    v.push_back(s.substr(pos, comma - pos));
                    pos = comma + 1;
                }
            }
            return v;
        }();
        if (allowed.empty()) return true;
        for (const auto& a : allowed)
            if (tag.compare(0, a.size(), a) == 0) return true;
        return false;
    }

    inline void log(const std::string& tag, const std::string& message) {
        if (!tagAllowed(tag)) return;
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
            char dateBuf[16];
            std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
            f << "{\"ts\":\"" << buf << "\",\"date\":\"" << dateBuf
              << "\",\"pid\":" << currentPid()
              << ",\"tag\":\"" << tag
              << "\",\"msg\":\"" << message << "\"}\n";
            f.flush();
        }
    }

    inline void logAlways(const std::string& tag, const std::string& message) {
        // Instrumentation output must NEVER be filtered by HDAW_LOG_TAGS —
        // a silent tripwire is no tripwire (lesson 17: log somewhere visible).
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
            char dateBuf[16];
            std::snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
                utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
            f << "{\"ts\":\"" << buf << "\",\"date\":\"" << dateBuf
              << "\",\"pid\":" << currentPid()
              << ",\"tag\":\"" << tag
              << "\",\"msg\":\"" << message << "\"}\n";
            f.flush();
        }
    }
}

#define HDAW_LOG(tag, msg) DebugLog::log(DebugLog::toLogString(tag), DebugLog::toLogString(msg))

// Instrumentation output must NEVER be filtered by HDAW_LOG_TAGS — a silent
// tripwire is no tripwire (lesson 17: log somewhere visible).
#define HDAW_LOG_ALWAYS(msg) DebugLog::logAlways("RT", DebugLog::toLogString(msg))
