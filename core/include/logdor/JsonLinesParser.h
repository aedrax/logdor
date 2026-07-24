#pragma once

#include "logdor/FormatParser.h"

namespace logdor {

/**
 * JSON Lines parser: one JSON object per line (journalctl -o json, pino,
 * bunyan, structured app logs). Key aliases - timestamp: timestamp | time |
 * ts | @timestamp | __REALTIME_TIMESTAMP (journal, µs since epoch); level:
 * level | severity | lvl | PRIORITY (journal, syslog numeric 0-7); source:
 * SYSLOG_IDENTIFIER | _SYSTEMD_UNIT | logger | name | tag; message:
 * message | msg | MESSAGE. Lines that are not JSON objects fall back to
 * raw-in-Message with ok = false. matchesStructure runs a full JSON parse
 * per line, which is fine at detection's sample size (<= 200 lines).
 * Field order: Timestamp, Level, Source, Message.
 */
class JsonLinesParser : public FormatParser {
public:
    enum Field { Timestamp = 0, Level, Source, Message, FieldCount };

    QString id() const override { return QStringLiteral("jsonlines"); }
    QString displayName() const override { return QStringLiteral("JSON Lines (journalctl -o json)"); }
    QList<FieldSchema> schema() const override;
    bool colorsBySeverity() const override { return true; }
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return 0.95; }
};

} // namespace logdor
