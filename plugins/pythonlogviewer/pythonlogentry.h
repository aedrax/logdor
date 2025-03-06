#ifndef PYTHONLOGENTRY_H
#define PYTHONLOGENTRY_H

#include <QChar>
#include <QColor>
#include <QString>
#include <QRegularExpression>
#include "../../app/src/plugininterface.h"

class PythonLogEntry {
public:
    enum class Level {
        Debug,
        Info,
        Warning,
        Error,
        Critical,
        Unknown
    };

    QString module;
    Level level;
    QString message;

    PythonLogEntry(const LogEntry& entry)
    {
        level = Level::Unknown;
        parseEntry(entry.getMessage());
    }

private:
    void parseEntry(const QString& text) {
        static QRegularExpression re(R"((\w+):([^:]+):\s*(.*))");
        auto match = re.match(text);
        if (match.hasMatch()) {
            level = parseLevel(match.captured(1));
            module = match.captured(2).trimmed();
            message = match.captured(3);
        } else {
            // Fallback for non-standard format
            message = text;
        }
    }

public:
    static Level parseLevel(const QString& levelStr) {
        QString upper = levelStr.toUpper();
        if (upper == "DEBUG") return Level::Debug;
        if (upper == "INFO") return Level::Info;
        if (upper == "WARNING") return Level::Warning;
        if (upper == "ERROR") return Level::Error;
        if (upper == "CRITICAL") return Level::Critical;
        return Level::Unknown;
    }

    static QString levelToString(Level level) {
        switch (level) {
            case Level::Debug: return "Debug";
            case Level::Info: return "Info";
            case Level::Warning: return "Warning";
            case Level::Error: return "Error";
            case Level::Critical: return "Critical";
            default: return "Unknown";
        }
    }

    static QColor levelColor(Level level) {
        switch (level) {
            case Level::Debug: return QColor(144, 238, 144);    // Light Green
            case Level::Info: return QColor(135, 206, 250);     // Light Blue
            case Level::Warning: return QColor(255, 165, 0);    // Orange
            case Level::Error: return QColor(255, 99, 71);      // Tomato Red
            case Level::Critical: return QColor(186, 85, 211);  // Medium Purple
            case Level::Unknown: return QColor(255, 255, 255);  // White
            default: return QColor(255, 255, 255);              // White
        }
    }
};

#endif // PYTHONLOGENTRY_H
