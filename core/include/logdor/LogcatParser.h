#pragma once

#include "logdor/FormatParser.h"

namespace logdor {

/**
 * Android logcat parser. Ports the legacy LogcatEntry regexes verbatim, tried
 * in the same order (threadtime, long, time, brief, process, thread, tag),
 * with the raw-message fallback last. Field order: Time, PID, TID, Level,
 * Tag, Message.
 */
class LogcatParser : public FormatParser {
public:
    enum Field { Time = 0, Pid, Tid, Level, Tag, Message, FieldCount };

    QString id() const override { return QStringLiteral("logcat"); }
    QString displayName() const override { return QStringLiteral("Android Logcat"); }
    QList<FieldSchema> schema() const override;
    bool colorsBySeverity() const override { return true; }
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return 0.9; }
};

} // namespace logdor
