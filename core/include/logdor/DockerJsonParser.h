#pragma once

#include "logdor/FormatParser.h"

namespace logdor {

/**
 * Docker json-file logging-driver parser: one JSON object per line with
 * string values at "log" (the captured output, trailing newline included),
 * "stream" (stdout|stderr), and "time" (RFC3339Nano). All three keys are
 * required - a JSON line missing any of them is a structural mismatch, so
 * generic structured logs stay with JsonLinesParser. Extra keys ("attrs",
 * ...) are tolerated and ignored. The trailing newline (and any '\r') is
 * chopped from the message; no severity is derived - stderr is not an
 * error level. Field order: Time, Stream, Message.
 */
class DockerJsonParser : public FormatParser {
public:
    enum Field { Time = 0, Stream, Message, FieldCount };

    QString id() const override { return QStringLiteral("docker-json"); }
    QString displayName() const override { return QStringLiteral("Docker container log (json-file)"); }
    QList<FieldSchema> schema() const override;
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return 0.97; }
};

} // namespace logdor
