#ifndef LOG4JENTRY_H
#define LOG4JENTRY_H

#include <QChar>
#include <QColor>
#include <QString>
#include <QRegularExpression>
#include <QDateTime>
#include "../../app/src/plugininterface.h"

class Log4jEntry {
public:
    enum class Level {
        Trace,
        Debug,
        Info,
        Warn,
        Error,
        Fatal,
        Unknown
    };

    QString timestamp;
    QString logger;
    QString thread;
    Level level;
    QString message;
    QString throwable;
    QString mdc;  // Mapped Diagnostic Context
    QString file; // Source file
    QString lineNumber; // Line number

    Log4jEntry(const LogEntry& entry)
    {
        level = Level::Unknown;
        parseEntry(entry.getMessage());
    }

private:
    void parseEntry(const QString& text) {
        QStringList lines = text.split('\n');
        if (lines.isEmpty()) return;

        // Try both formats
        if (!parseDetailedFormat(lines[0]) && !parseSimpleFormat(lines[0])) {
            // If neither format matches, store as message
            message = text;
            return;
        }

        // Handle stack trace if present (starts from line 2)
        if (lines.size() > 1) {
            QStringList stackLines;
            bool isStackTrace = false;
            for (int i = 1; i < lines.size(); ++i) {
                QString line = lines[i].trimmed();
                if (line.startsWith("at ") || 
                    (i == 1 && (line.contains("Exception:") || line.contains("Error:")))) {
                    stackLines.append(line);
                    isStackTrace = true;
                } else if (!isStackTrace) {
                    // If we haven't seen a stack trace yet, this is part of the message
                    if (!line.isEmpty()) {
                        message += "\n" + line;
                    }
                } else if (isStackTrace && !line.isEmpty()) {
                    // If we're in a stack trace, keep adding lines
                    stackLines.append(line);
                }
            }
            if (!stackLines.isEmpty()) {
                throwable = stackLines.join("\n");
            }
        }
    }

    bool parseDetailedFormat(const QString& line) {
        // Format: 2017-11-29 19:22:31,580 [main] DEBUG (LoggingHelper.java:19) - This is debug log..
        static QRegularExpression re(R"((\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2},\d{3})\s+\[([^\]]+)\]\s+(TRACE|DEBUG|INFO|WARN|ERROR|FATAL)\s+\(([^:]+):(\d+)\)\s+-\s+(.*))");
        auto match = re.match(line);
        
        if (match.hasMatch()) {
            timestamp = match.captured(1);
            thread = match.captured(2);
            level = parseLevel(match.captured(3));
            file = match.captured(4);
            lineNumber = match.captured(5);
            message = match.captured(6);
            
            // Extract class name from file for logger
            if (!file.isEmpty()) {
                logger = file;
                logger.remove(".java");
            }
            
            return true;
        }
        return false;
    }

    bool parseSimpleFormat(const QString& line) {
        // Format: 2014-07-02 20:52:39 DEBUG className:200 - This is debug message
        static QRegularExpression re(R"((\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2})\s+(TRACE|DEBUG|INFO|WARN|ERROR|FATAL)\s+([^:]+):(\d+)\s+-\s+(.*))");
        auto match = re.match(line);
        
        if (match.hasMatch()) {
            timestamp = match.captured(1);
            level = parseLevel(match.captured(2));
            logger = match.captured(3);
            lineNumber = match.captured(4);
            message = match.captured(5);
            
            // For simple format, logger is the class name
            file = logger + ".java";
            
            return true;
        }
        return false;
    }

public:
    static Level parseLevel(const QString& levelStr) {
        QString level = levelStr.toUpper().trimmed();
        if (level == "TRACE") return Level::Trace;
        if (level == "DEBUG") return Level::Debug;
        if (level == "INFO") return Level::Info;
        if (level == "WARN") return Level::Warn;
        if (level == "ERROR") return Level::Error;
        if (level == "FATAL") return Level::Fatal;
        return Level::Unknown;
    }

    static QString levelToString(Level level) {
        switch (level) {
            case Level::Trace: return "TRACE";
            case Level::Debug: return "DEBUG";
            case Level::Info: return "INFO";
            case Level::Warn: return "WARN";
            case Level::Error: return "ERROR";
            case Level::Fatal: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    static QColor levelColor(Level level) {
        switch (level) {
            case Level::Trace: return QColor(150, 150, 150);  // Light Gray
            case Level::Debug: return QColor(144, 238, 144);  // Light Green
            case Level::Info: return QColor(135, 206, 250);   // Light Blue
            case Level::Warn: return QColor(255, 165, 0);     // Orange
            case Level::Error: return QColor(255, 99, 71);    // Tomato Red
            case Level::Fatal: return QColor(139, 0, 0);      // Dark Red
            case Level::Unknown: return QColor(255, 255, 255);// White
            default: return QColor(255, 255, 255);           // White
        }
    }
};

#endif // LOG4JENTRY_H
