#pragma once

#include "logdor/FormatParser.h"

#include <memory>

namespace logdor {

class FileSource;
class LineIndex;

/**
 * W3C Extended Log File Format (IIS, Exchange). The schema comes from the
 * file's "#Fields:" directive, so a parser instance is constructed per file
 * (CsvParser precedent - immutable after construction, parseLine is
 * thread-safe) and it is deliberately absent from builtinParsers().
 *
 * Adjacent "date time" fields merge into one synthetic ISO 8601 Timestamp
 * column (a lone HH:mm:ss has no parseable wall-clock shape); well-known
 * names map to Integer/Identifier hints; "-" placeholders stay literal.
 * Times are GMT per the spec and follow the app's assumed-zone setting like
 * every other zone-less format. Directive (#...) and blank lines fall back
 * as ok=false rows.
 *
 * Limitation: a mid-file "#Fields:" re-declaration (IIS service restart) is
 * ignored; if the field list changed, those data lines render ok=false.
 */
class W3CExtendedParser : public FormatParser {
public:
    /// Build directly from a #Fields name list (testing / known layouts).
    explicit W3CExtendedParser(QStringList fieldNames);

    /**
     * Scans the leading directive block (first 32 lines) for "#Fields:".
     * nullptr when absent or when data precedes every directive - callers
     * fall back to plaintext.
     */
    static std::shared_ptr<const W3CExtendedParser> fromFile(
        const FileSource& source, const LineIndex& index);

    QString id() const override { return QStringLiteral("w3c"); }
    QString displayName() const override { return QStringLiteral("W3C Extended Log (IIS)"); }
    QList<FieldSchema> schema() const override { return m_schema; }
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return 0.9; }

    /// Directive lines (#...) carry no data, wherever they recur.
    bool hasMetaLines() const override { return true; }
    bool isDataLine(qint64 lineNumber, QByteArrayView raw) const override
    {
        Q_UNUSED(lineNumber);
        return !raw.trimmed().startsWith('#');
    }

private:
    QList<FieldSchema> m_schema;
    int m_messageColumn = 0;
    int m_tokenCount = 0;      // tokens per data line (schema size + 1 when merged)
    int m_dateTokenIndex = -1; // >=0: tokens [i, i+1] merge into column i
};

} // namespace logdor
