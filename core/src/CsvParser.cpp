#include "logdor/CsvParser.h"

#include "logdor/FileSource.h"
#include "logdor/LineIndex.h"

#include <QSet>

namespace logdor {

QStringList parseCsvRecord(QByteArrayView raw)
{
    const QString line = QString::fromUtf8(raw);
    QStringList fields;
    QString field;
    qsizetype i = 0;
    const qsizetype n = line.size();

    while (i <= n) {
        // Skip leading spaces to find a potential opening quote.
        qsizetype start = i;
        while (start < n && line[start] == u' ')
            ++start;

        if (start < n && line[start] == u'"') {
            // Quoted field: runs to the closing quote; "" is a literal quote.
            i = start + 1;
            while (i < n) {
                if (line[i] == u'"') {
                    if (i + 1 < n && line[i + 1] == u'"') {
                        field += u'"';
                        i += 2;
                        continue;
                    }
                    ++i; // closing quote
                    break;
                }
                field += line[i];
                ++i;
            }
            // Lenient tail: verbatim text up to the delimiter.
            while (i < n && line[i] != u',') {
                field += line[i];
                ++i;
            }
        } else {
            while (i < n && line[i] != u',') {
                field += line[i];
                ++i;
            }
            field = field.trimmed();
        }

        fields.append(field);
        field.clear();
        if (i >= n)
            break;
        ++i; // consume the comma; a trailing comma yields a final empty field
        if (i == n) {
            fields.append(QString());
            break;
        }
    }
    return fields;
}

CsvParser::CsvParser(QStringList headers, QList<FieldType> types)
{
    m_schema.reserve(headers.size());
    for (int i = 0; i < headers.size(); ++i) {
        FieldSchema field;
        field.name = headers[i];
        field.type = i < types.size() ? types[i] : FieldType::String;
        if (field.type == FieldType::Integer)
            field.hint = FieldHint::Numeric;
        m_schema.append(field);
    }
    // The message (stretch/fallback) column: one literally named so, else last.
    m_messageColumn = m_schema.size() - 1;
    for (int i = 0; i < m_schema.size(); ++i) {
        if (m_schema[i].name.compare(QStringLiteral("message"),
                                     Qt::CaseInsensitive) == 0) {
            m_messageColumn = i;
            break;
        }
    }
    if (m_messageColumn >= 0)
        m_schema[m_messageColumn].hint = FieldHint::Message;
}

std::shared_ptr<const CsvParser> CsvParser::fromFile(const FileSource& source,
                                                     const LineIndex& index)
{
    if (index.lineCount() == 0)
        return nullptr;

    const QByteArray headerLine =
        source.read(index.offsetOf(0), index.lengthOf(0));
    QStringList headers = parseCsvRecord(QByteArrayView(headerLine));
    if (headers.isEmpty() || (headers.size() == 1 && headers[0].isEmpty()))
        return nullptr;

    // Fix empty and duplicate names.
    QSet<QString> seen;
    for (int i = 0; i < headers.size(); ++i) {
        if (headers[i].isEmpty())
            headers[i] = QStringLiteral("Column %1").arg(i + 1);
        QString candidate = headers[i];
        int suffix = 2;
        while (seen.contains(candidate.toLower()))
            candidate = headers[i] + QStringLiteral(" (%1)").arg(suffix++);
        headers[i] = candidate;
        seen.insert(candidate.toLower());
    }

    // Sniff column types over up to 64 data lines.
    QList<bool> allInteger(headers.size(), true);
    QList<bool> sawValue(headers.size(), false);
    const qint64 sampleEnd = qMin<qint64>(index.lineCount(), 65);
    for (qint64 line = 1; line < sampleEnd; ++line) {
        const QByteArray raw = source.read(index.offsetOf(line),
                                           index.lengthOf(line));
        const QStringList fields = parseCsvRecord(QByteArrayView(raw));
        for (int i = 0; i < headers.size() && i < fields.size(); ++i) {
            if (fields[i].isEmpty())
                continue;
            sawValue[i] = true;
            bool ok = false;
            fields[i].toLongLong(&ok);
            if (!ok)
                allInteger[i] = false;
        }
    }
    QList<FieldType> types;
    types.reserve(headers.size());
    for (int i = 0; i < headers.size(); ++i)
        types.append(sawValue[i] && allInteger[i] ? FieldType::Integer
                                                  : FieldType::String);

    return std::make_shared<const CsvParser>(headers, types);
}

void CsvParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    const QStringList fields = parseCsvRecord(raw);
    out.fields.clear();
    out.fields.resize(m_schema.size());
    out.severity = Severity::None;
    out.ok = fields.size() == m_schema.size();

    for (int i = 0; i < m_schema.size() && i < fields.size(); ++i)
        out.fields[i] = fields[i];
    // Overflow: keep the extra content visible in the last column.
    if (fields.size() > m_schema.size()) {
        QString& last = out.fields[m_schema.size() - 1];
        for (int i = m_schema.size(); i < fields.size(); ++i)
            last += QStringLiteral(",") + fields[i];
    }
}

bool CsvParser::matchesStructure(QByteArrayView raw) const
{
    return m_schema.size() >= 2
        && parseCsvRecord(raw).size() == m_schema.size();
}

} // namespace logdor
