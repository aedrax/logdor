#include "logdor/DeclarativeParser.h"

namespace logdor {

DeclarativeParser::DeclarativeParser(FormatSpec spec)
    : m_spec(std::move(spec))
    , m_regex(m_spec.pattern)
{
    Q_ASSERT(m_regex.isValid()); // parseFormatSpec validated it
    const QStringList captures = m_regex.namedCaptureGroups();
    m_captureIndexForField.reserve(m_spec.fields.size());
    for (int i = 0; i < m_spec.fields.size(); ++i) {
        m_captureIndexForField.append(captures.indexOf(m_spec.fields[i].capture));
        if (m_spec.fields[i].hint == FieldHint::Message)
            m_messageFieldIndex = i;
    }
    if (!m_spec.severityCapture.isEmpty())
        m_severityCaptureIndex = captures.indexOf(m_spec.severityCapture);
}

QList<FieldSchema> DeclarativeParser::schema() const
{
    QList<FieldSchema> out;
    out.reserve(m_spec.fields.size());
    for (const FormatSpecField& field : m_spec.fields)
        out.append({ field.name, field.type, field.hint, field.timeFormat });
    return out;
}

void DeclarativeParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    const QString text = QString::fromUtf8(raw);
    out.fields.clear();
    out.fields.resize(m_spec.fields.size());
    out.severity = Severity::None;

    const auto match = m_regex.match(text);
    if (!match.hasMatch()) {
        // Fallback: raw line in the message column so every line renders.
        out.fields[m_messageFieldIndex] = text;
        out.ok = false;
        return;
    }

    for (int i = 0; i < m_spec.fields.size(); ++i)
        out.fields[i] = match.captured(m_captureIndexForField[i]);

    if (m_severityCaptureIndex >= 0) {
        const QString value = match.captured(m_severityCaptureIndex).toLower();
        out.severity = m_spec.severityMap.value(value, m_spec.severityDefault);
    }
    out.ok = true;
}

bool DeclarativeParser::matchesStructure(QByteArrayView raw) const
{
    return m_regex.match(QString::fromUtf8(raw)).hasMatch();
}

} // namespace logdor
