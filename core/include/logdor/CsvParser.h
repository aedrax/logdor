#pragma once

#include "logdor/FormatParser.h"

#include <memory>

namespace logdor {

class FileSource;
class LineIndex;

/**
 * Split one CSV record (RFC 4180 within a single line): comma delimiter,
 * quoted fields may contain commas, "" inside quotes is a literal quote,
 * lenient about text after a closing quote, unquoted fields are trimmed.
 * @p raw excludes the terminator and trailing '\r' (LineIndex semantics).
 */
QStringList parseCsvRecord(QByteArrayView raw);

/**
 * CSV as a FormatParser: the schema comes from the file's header row, so a
 * parser instance is constructed per file (DeclarativeParser precedent —
 * immutable after construction, parseLine is thread-safe).
 *
 * Limitation (same as the legacy viewer): records with embedded newlines in
 * quoted fields span multiple index lines and render as ok=false rows.
 */
class CsvParser : public FormatParser {
public:
    explicit CsvParser(QStringList headers, QList<FieldType> types = {});

    /**
     * Build from the file's first line, sniffing column types over up to 64
     * data lines (a column is Integer when every non-empty sample parses).
     * nullptr for an empty file — callers fall back to plaintext.
     */
    static std::shared_ptr<const CsvParser> fromFile(const FileSource& source,
                                                     const LineIndex& index);

    QString id() const override { return QStringLiteral("csv"); }
    QString displayName() const override { return QStringLiteral("CSV"); }
    QList<FieldSchema> schema() const override { return m_schema; }
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return 0.6; }

private:
    QList<FieldSchema> m_schema;
    int m_messageColumn = 0;
};

} // namespace logdor
