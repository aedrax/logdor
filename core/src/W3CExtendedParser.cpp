#include "logdor/W3CExtendedParser.h"

#include "logdor/FileSource.h"
#include "logdor/LineIndex.h"

#include <QSet>

namespace logdor {

namespace {

// The W3C/IIS field vocabulary is standardized - a deterministic mapping
// beats sampling.
bool isIntegerField(const QString& key)
{
    static const QSet<QString> kIntegers{
        QStringLiteral("sc-status"), QStringLiteral("sc-substatus"),
        QStringLiteral("sc-win32-status"), QStringLiteral("sc-bytes"),
        QStringLiteral("cs-bytes"), QStringLiteral("time-taken"),
        QStringLiteral("s-port"),
    };
    return kIntegers.contains(key);
}

bool isIdentifierField(const QString& key)
{
    static const QSet<QString> kIdentifiers{
        QStringLiteral("c-ip"), QStringLiteral("s-ip"),
        QStringLiteral("cs-username"), QStringLiteral("s-computername"),
        QStringLiteral("s-sitename"),
    };
    return kIdentifiers.contains(key);
}

QStringList tokenize(QByteArrayView raw)
{
    // Values with embedded spaces are +/%20-encoded per the spec, so plain
    // whitespace tokenization is correct (simplified() also folds tabs).
    return QString::fromUtf8(raw).simplified().split(u' ', Qt::SkipEmptyParts);
}

} // namespace

W3CExtendedParser::W3CExtendedParser(QStringList fieldNames)
{
    m_tokenCount = fieldNames.size();

    for (int i = 0; i + 1 < fieldNames.size(); ++i) {
        if (fieldNames[i].compare(QStringLiteral("date"), Qt::CaseInsensitive) == 0
            && fieldNames[i + 1].compare(QStringLiteral("time"), Qt::CaseInsensitive) == 0) {
            m_dateTokenIndex = i;
            break;
        }
    }

    // Column display names plus the lowercased W3C name driving the mapping;
    // the merged synthetic column carries an empty key.
    QStringList names;
    QStringList keys;
    for (int t = 0; t < fieldNames.size(); ++t) {
        if (m_dateTokenIndex >= 0 && t == m_dateTokenIndex + 1)
            continue; // merged into the preceding synthetic Time column
        if (t == m_dateTokenIndex) {
            names.append(QStringLiteral("Time"));
            keys.append(QString());
        } else {
            names.append(fieldNames[t]);
            keys.append(fieldNames[t].toLower());
        }
    }

    // Fix empty and duplicate names (CsvParser precedent).
    QSet<QString> seen;
    for (int i = 0; i < names.size(); ++i) {
        if (names[i].isEmpty())
            names[i] = QStringLiteral("Column %1").arg(i + 1);
        QString candidate = names[i];
        int suffix = 2;
        while (seen.contains(candidate.toLower()))
            candidate = names[i] + QStringLiteral(" (%1)").arg(suffix++);
        names[i] = candidate;
        seen.insert(candidate.toLower());
    }

    m_schema.reserve(names.size());
    for (int i = 0; i < names.size(); ++i) {
        FieldSchema field;
        field.name = names[i];
        if (m_dateTokenIndex >= 0 && i == m_dateTokenIndex) {
            // "2026-07-25 14:32:01" is the space-separated ISO shape the
            // timestamp codec accepts, so the merged value parses directly.
            field.type = FieldType::DateTime;
            field.hint = FieldHint::Timestamp;
            field.timeFormat = QStringLiteral("iso8601");
        } else if (isIntegerField(keys[i])) {
            field.type = FieldType::Integer;
            field.hint = FieldHint::Numeric;
        } else if (isIdentifierField(keys[i])) {
            field.hint = FieldHint::Identifier;
        }
        m_schema.append(field);
    }

    // The message (stretch/fallback) column: cs-uri-stem, else cs-uri, else
    // the last column that is not the merged timestamp.
    m_messageColumn = m_schema.size() - 1;
    if (m_dateTokenIndex >= 0 && m_messageColumn == m_dateTokenIndex && m_messageColumn > 0)
        --m_messageColumn;
    for (const QString& wanted : { QStringLiteral("cs-uri-stem"), QStringLiteral("cs-uri") }) {
        const int at = keys.indexOf(wanted);
        if (at >= 0) {
            m_messageColumn = at;
            break;
        }
    }
    m_schema[m_messageColumn].hint = FieldHint::Message;
}

std::shared_ptr<const W3CExtendedParser> W3CExtendedParser::fromFile(
    const FileSource& source, const LineIndex& index)
{
    const qint64 scanEnd = qMin<qint64>(index.lineCount(), 32);
    for (qint64 line = 0; line < scanEnd; ++line) {
        const QByteArray raw = source.read(index.offsetOf(line), index.lengthOf(line));
        const QString text = QString::fromUtf8(raw).trimmed();
        if (text.isEmpty())
            continue;
        if (!text.startsWith(u'#'))
            return nullptr; // data before any directive - not W3C extended
        if (text.startsWith(QStringLiteral("#Fields:"), Qt::CaseInsensitive)) {
            const QStringList names =
                text.mid(8).simplified().split(u' ', Qt::SkipEmptyParts);
            if (names.size() < 2)
                return nullptr;
            return std::make_shared<const W3CExtendedParser>(names);
        }
    }
    return nullptr;
}

void W3CExtendedParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    out.fields.clear();
    out.fields.resize(m_schema.size());
    out.severity = Severity::None;

    const QString text = QString::fromUtf8(raw);
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(u'#')) {
        out.fields[m_messageColumn] = text;
        out.ok = false;
        return;
    }

    const QStringList tokens = tokenize(raw);
    for (int t = 0; t < tokens.size(); ++t) {
        if (m_dateTokenIndex >= 0 && t == m_dateTokenIndex + 1) {
            out.fields[m_dateTokenIndex] += u' ';
            out.fields[m_dateTokenIndex] += tokens[t];
            continue;
        }
        const int col = (m_dateTokenIndex >= 0 && t > m_dateTokenIndex) ? t - 1 : t;
        if (col < m_schema.size()) {
            out.fields[col] = tokens[t];
        } else {
            // Overflow: keep the extra content visible in the last column.
            out.fields[m_schema.size() - 1] += u' ';
            out.fields[m_schema.size() - 1] += tokens[t];
        }
    }
    out.ok = tokens.size() == m_tokenCount;
}

bool W3CExtendedParser::matchesStructure(QByteArrayView raw) const
{
    const QString trimmed = QString::fromUtf8(raw).trimmed();
    if (trimmed.startsWith(u'#'))
        return true; // directives are format-defining structure
    return m_tokenCount >= 2 && tokenize(raw).size() == m_tokenCount;
}

} // namespace logdor
