#pragma once

#include "logdor/FormatParser.h"

namespace logdor {

// The universal fallback format: one "Log" column containing the raw line.
class PlainTextParser : public FormatParser {
public:
    QString id() const override { return QStringLiteral("plaintext"); }
    QString displayName() const override { return QStringLiteral("Plain Text"); }
    QList<FieldSchema> schema() const override;
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return 0.05; }
};

} // namespace logdor
