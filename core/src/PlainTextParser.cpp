#include "logdor/PlainTextParser.h"

namespace logdor {

QList<FieldSchema> PlainTextParser::schema() const
{
    return { { QStringLiteral("Log"), FieldType::String, FieldHint::Message } };
}

void PlainTextParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    out.fields.clear();
    out.fields.append(QString::fromUtf8(raw));
    out.severity = Severity::None;
    out.ok = true;
}

bool PlainTextParser::matchesStructure(QByteArrayView raw) const
{
    Q_UNUSED(raw)
    return true;
}

} // namespace logdor
