#pragma once

#include "logdor/FormatParser.h"
#include "logdor/FormatSpec.h"

#include <QRegularExpression>

namespace logdor {

/// A FormatParser driven by a (pre-validated) FormatSpec.
class DeclarativeParser : public FormatParser {
public:
    explicit DeclarativeParser(FormatSpec spec);

    QString id() const override { return m_spec.id; }
    QString displayName() const override { return m_spec.displayName; }
    QList<FieldSchema> schema() const override;
    bool colorsBySeverity() const override { return !m_spec.severityCapture.isEmpty(); }
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return m_spec.specificity; }

private:
    FormatSpec m_spec;
    QRegularExpression m_regex; // compiled once; match() is thread-safe
    QList<int> m_captureIndexForField;
    int m_severityCaptureIndex = -1;
    int m_messageFieldIndex = 0; // fallback column
};

} // namespace logdor
