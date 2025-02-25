#ifndef LOGCATENTRY_H
#define LOGCATENTRY_H

#include <QChar>
#include <QColor>
#include <QString>
#include <QRegularExpression>
#include "../../app/src/plugininterface.h"

class LogcatEntry {
public:
    enum class Level {
        Verbose,
        Debug,
        Info,
        Warning,
        Error,
        Fatal,
        Unknown
    };

    QString timestamp;
    QString pid;
    QString tid;  // Thread ID
    Level level;
    QString tag;
    QString message;

    LogcatEntry(const LogEntry& entry)
    {
        level = Level::Unknown;
        static QRegularExpression re(R"((\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+(\d+)\s+(\d+)\s+([VDIWEF])\s+([^:]+)\s*:\s*(.*))");
        auto match = re.match(entry.getMessage());
        if (match.hasMatch()) {
            timestamp = match.captured(1);
            pid = match.captured(2);
            tid = match.captured(3);
            level = parseLevel(match.captured(4)[0]);
            tag = match.captured(5).trimmed();
            message = match.captured(6);
        }
    }

    static Level parseLevel(const QChar& levelChar) {
        switch (levelChar.toLatin1()) {
            case 'V': return Level::Verbose;
            case 'D': return Level::Debug;
            case 'I': return Level::Info;
            case 'W': return Level::Warning;
            case 'E': return Level::Error;
            case 'F': return Level::Fatal;
            default: return Level::Unknown;
        }
    }

    static QString levelToString(Level level) {
        switch (level) {
            case Level::Verbose: return "Verbose";
            case Level::Debug: return "Debug";
            case Level::Info: return "Info";
            case Level::Warning: return "Warning";
            case Level::Error: return "Error";
            case Level::Fatal: return "Fatal";
            default: return "Unknown";
        }
    }

    static QColor levelColor(Level level) {
        switch (level) {
            case Level::Verbose: return QColor(150, 150, 150);  // Light Gray
            case Level::Debug: return QColor(144, 238, 144);    // Light Green
            case Level::Info: return QColor(135, 206, 250);     // Light Blue
            case Level::Warning: return QColor(255, 165, 0);    // Orange
            case Level::Error: return QColor(255, 99, 71);      // Tomato Red
            case Level::Fatal: return QColor(186, 85, 211);     // Medium Purple
            case Level::Unknown: return QColor(255, 255, 255);  // White
            default: return QColor(255, 255, 255);             // White
        }
    }
};

#endif // LOGCATENTRY_H