#pragma once

#include "logdor/FormatParser.h"

namespace logdor {

/**
 * Apache Common/Combined Log Format parser. Ports the legacy CLFEntry regex
 * and timestamp handling (including its timezone shift quirk) so output is
 * display-identical. Field order: Remote Host, Identity, User ID, Timestamp,
 * Method, Path, Protocol, Status, Bytes.
 */
class ClfParser : public FormatParser {
public:
    enum Field { RemoteHost = 0, Identity, UserId, Timestamp, Method, Path,
                 Protocol, Status, Bytes, FieldCount };

    QString id() const override { return QStringLiteral("clf"); }
    QString displayName() const override { return QStringLiteral("Apache Access Log"); }
    QList<FieldSchema> schema() const override;
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return 1.0; }
};

} // namespace logdor
