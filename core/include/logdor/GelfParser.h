#pragma once

#include "logdor/FormatParser.h"

namespace logdor {

/**
 * GELF (Graylog Extended Log Format) parser: one JSON message per line
 * (NDJSON export / GELF file output). "version", "host", and
 * "short_message" are required per the GELF 1.1 spec - a JSON line missing
 * any of them is a structural mismatch, so generic structured logs stay
 * with JsonLinesParser and Docker json-file records (disjoint keys) never
 * collide. "timestamp" (epoch seconds, fraction allowed) and "level"
 * (syslog 0-7 numeric, or a level name) are optional; the level maps to
 * severity coloring and the Level column shows the canonical name.
 * "full_message" and the "_*" additional fields (except the deprecated
 * "_id") are gathered into the Extra column as compact JSON. Field order:
 * Timestamp, Level, Host, Message, Extra.
 */
class GelfParser : public FormatParser {
public:
    enum Field { Timestamp = 0, Level, Host, Message, Extra, FieldCount };

    QString id() const override { return QStringLiteral("gelf"); }
    QString displayName() const override { return QStringLiteral("GELF (Graylog Extended Log Format)"); }
    QList<FieldSchema> schema() const override;
    bool colorsBySeverity() const override { return true; }
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return 0.97; }
};

} // namespace logdor
