#pragma once

#include "logdor/FormatParser.h"

namespace logdor {

/**
 * Android logcat parser covering every `logcat -v` variant: threadtime,
 * long (entry headers; the message lines that follow fall through to the
 * fallback), time, brief, process, thread, tag - tried in that order, most
 * specific first. Unmatched lines (raw format, `-v long` message lines,
 * "--------- beginning of" markers) keep the full text in Message with an
 * empty Level and ok=false. Field order: Time, PID, TID, Level, Tag,
 * Message.
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
