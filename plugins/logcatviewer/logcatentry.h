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

    enum class Format {
        Brief,
        Long,
        Process,
        Raw,
        Tag,
        Thread,
        ThreadTime,
        Time,
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
        parseEntry(entry.getMessage());
    }

private:
    void parseEntry(const QString& text) {
        // Try each format until one matches
        // the order matters because some formats are more specific than others
        if (parseThreadTime(text)) return; // Date, time, pid, tid, level, tag
        if (parseLong(text)) return;       // Date, time, pid, tid, level, tag
        if (parseTime(text)) return;       // Date, time, pid,    , level, tag
        if (parseBrief(text)) return;      //             pid,    , level, tag
        if (parseProcess(text)) return;    //             pid,    , level, tag
        if (parseThread(text)) return;     //             pid, tid, level,
        if (parseTag(text)) return;        //                     , level, tag
        if (parseRaw(text)) return;        // just the message
    }

    bool parseBrief(const QString& text) {
        static QRegularExpression re(R"(([VDIWEF])/([^(]+)\(\s*(\d+)\):\s+(.*))");
        auto match = re.match(text);
        if (match.hasMatch()) {
            level = parseLevel(match.captured(1)[0]);
            tag = match.captured(2).trimmed();
            pid = match.captured(3);
            message = match.captured(4);
            return true;
        }
        return false;
    }

    bool parseLong(const QString& text) {
        static QRegularExpression re(R"(\[\s*(\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+(\d+):\s*(\d+)\s+([VDIWEF])/([^]]+)\s*\]\s*(.*))");
        auto match = re.match(text);
        if (match.hasMatch()) {
            timestamp = match.captured(1);
            pid = match.captured(2);
            tid = match.captured(3);
            level = parseLevel(match.captured(4)[0]);
            tag = match.captured(5).trimmed();
            message = match.captured(6);
            return true;
        }
        return false;
    }

    bool parseProcess(const QString& text) {
        static QRegularExpression re(R"(([VDIWEF])\(\s*(\d+)\)\s+(.*?)\s+\(([^)]+)\))");
        auto match = re.match(text);
        if (match.hasMatch()) {
            level = parseLevel(match.captured(1)[0]);
            pid = match.captured(2);
            message = match.captured(3);
            tag = match.captured(4).trimmed();
            return true;
        }
        return false;
    }

    bool parseRaw(const QString& text) {
        // Raw format has no metadata, just the message
        message = text;
        level = Level::Unknown;
        return true;
    }

    bool parseTag(const QString& text) {
        static QRegularExpression re(R"(([VDIWEF])/([^/:]+):\s*(.*))");
        auto match = re.match(text);
        if (match.hasMatch()) {
            level = parseLevel(match.captured(1)[0]);
            tag = match.captured(2).trimmed();
            message = match.captured(3);
            return true;
        }
        return false;
    }

    bool parseThread(const QString& text) {
        static QRegularExpression re(R"(([VDIWEF])\(\s*(\d+):\s*(\d+)\)\s+(.*))");
        auto match = re.match(text);
        if (match.hasMatch()) {
            level = parseLevel(match.captured(1)[0]);
            pid = match.captured(2);
            tid = match.captured(3);
            message = match.captured(4);
            return true;
        }
        return false;
    }

    bool parseThreadTime(const QString& text) {
        static QRegularExpression re(R"((\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+(\d+)\s+(\d+)\s+([VDIWEF])\s+([^:\s]+):\s*(.*))");
        auto match = re.match(text);
        if (match.hasMatch()) {
            timestamp = match.captured(1);
            pid = match.captured(2);
            tid = match.captured(3);
            level = parseLevel(match.captured(4)[0]);
            tag = match.captured(5).trimmed();
            message = match.captured(6);
            return true;
        }
        return false;
    }

    bool parseTime(const QString& text) {
        static QRegularExpression re(R"((\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+([VDIWEF])/([^(]+?)\s*\(\s*(\d+)\):\s*(.*))");
        auto match = re.match(text);
        if (match.hasMatch()) {
            timestamp = match.captured(1);
            level = parseLevel(match.captured(2)[0]);
            tag = match.captured(3).trimmed();
            pid = match.captured(4);
            message = match.captured(5);
            return true;
        }
        return false;
    }

public:
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
