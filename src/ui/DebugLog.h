#pragma once
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QMutex>
#include <QString>

namespace DebugLog {
    inline QFile& logFile() {
        static QFile f;
        static bool initialized = false;
        if (!initialized) {
            QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            QDir().mkpath(path);
            f.setFileName(path + "/hdaw_debug.log");
            if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
                fputs("HDAW: Failed to open debug log file\n", stderr);
            initialized = true;
        }
        return f;
    }

    inline QMutex& logMutex() {
        static QMutex m;
        return m;
    }

    inline void log(const QString& tag, const QString& message) {
        QMutexLocker locker(&logMutex());
        QFile& f = logFile();
        if (f.isOpen()) {
            QTextStream ts(&f);
            ts << QDateTime::currentDateTimeUtc().toString("hh:mm:ss.zzz")
               << " [" << tag << "] " << message << "\n";
            (void)ts.flush();
        }
    }
}

#define HDAW_LOG(tag, msg) DebugLog::log(QString(tag), QString(msg))
