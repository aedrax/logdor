#pragma once

#include "logdor/FormatParser.h"

#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QString>

#include <memory>
#include <vector>

namespace logdor {

struct QueryError {
    qsizetype position = -1; // offset into the query text
    qsizetype length = 0;
    QString message; // e.g. "unknown field 'foo'"

    bool isError() const { return position >= 0; }
};

enum class QueryOption : quint8 {
    None = 0,
    // Unknown field terms compile to always-true instead of erroring —
    // used for schema-agnostic syntax validation (the filter-bar tint).
    AllowUnknownFields = 1,
};
Q_DECLARE_FLAGS(QueryOptions, QueryOption)
Q_DECLARE_OPERATORS_FOR_FLAGS(QueryOptions)

/**
 * Immutable per-line values of one schema column across ALL source lines.
 * String/DateTime columns store UTF-8 bytes in one flat blob; Integer
 * columns store qint64 plus a validity bit (fallback/unparseable rows).
 * Built single-threaded via Builder (or merged from shards), then immutable;
 * const access is thread-safe.
 */
class ColumnData {
public:
    struct Builder {
        explicit Builder(FieldType type);
        void appendString(QByteArrayView utf8);
        void appendInt(const QString& text); // parses; invalid => validity bit off
        void append(const QString& fieldText, FieldType type);
        qint64 count() const;
        ColumnData build() &&;

        FieldType type;
        QByteArray blob;
        std::vector<quint64> offsets; // size n+1, starts at 0
        std::vector<qint64> ints;
        std::vector<bool> intValid;
    };

    ColumnData() = default;

    FieldType type() const { return m_type; }
    qint64 lineCount() const { return m_count; }

    /// String/DateTime columns only.
    QByteArrayView stringAt(qint64 line) const
    {
        const quint64 start = m_offsets[size_t(line)];
        return QByteArrayView(m_blob.constData() + start,
                              qsizetype(m_offsets[size_t(line) + 1] - start));
    }

    /// Integer columns only; false when the row's value was unparseable.
    bool intAt(qint64 line, qint64* out) const
    {
        if (!m_intValid[size_t(line)])
            return false;
        *out = m_ints[size_t(line)];
        return true;
    }

    size_t memoryUsage() const;

private:
    FieldType m_type = FieldType::String;
    qint64 m_count = 0;
    QByteArray m_blob;
    std::vector<quint64> m_offsets;
    std::vector<qint64> m_ints;
    std::vector<bool> m_intValid;
};

/// Immutable snapshot handed to worker threads.
struct ColumnSnapshot {
    QHash<int, std::shared_ptr<const ColumnData>> columns; // key = schema column index
    std::shared_ptr<const std::vector<quint8>> severity;   // may be null

    bool covers(const QList<int>& cols, bool needsSeverity) const
    {
        for (int col : cols) {
            if (!columns.contains(col))
                return false;
        }
        return !needsSeverity || severity != nullptr;
    }
};

/**
 * A compiled field query. Grammar:
 *
 *   query := or ; or := and (OR and)* ; and := unary (AND? unary)*
 *   unary := NOT unary | '(' or ')' | term
 *   term  := FIELD op value | text
 *   op    := ':' | '=' | '!=' | '<' | '<=' | '>' | '>='
 *
 * Free text terms substring-match the whole raw line. Field names resolve
 * against the schema case-insensitively with spaces stripped. String fields:
 * ':' = contains, '*'/'?' in the value = anchored wildcard, '=' exact;
 * ordering ops compare bytes lexicographically. Integer fields: numeric,
 * unparseable rows never match. SeverityName-hinted fields compare severity
 * enum order for named levels.
 *
 * Compile once (GUI thread); evaluate() is const and thread-safe.
 */
class CompiledQuery {
public:
    static std::shared_ptr<const CompiledQuery> compile(
        const QString& text, const QList<FieldSchema>& schema,
        Qt::CaseSensitivity cs, QueryOptions options = {},
        QueryError* error = nullptr);

    ~CompiledQuery();

    /// Schema column indices read by field terms (severity terms excluded).
    const QList<int>& referencedColumns() const { return m_referencedColumns; }
    bool needsSeverity() const { return m_needsSeverity; }

    /// @p raw = lengthOf() bytes of the line; @p columns must cover
    /// referencedColumns() and needsSeverity().
    bool evaluate(qint64 line, QByteArrayView raw,
                  const ColumnSnapshot& columns) const;

    QString text() const { return m_text; }

private:
    CompiledQuery();

    struct Node;
    std::unique_ptr<Node> m_root;
    QString m_text;
    QList<int> m_referencedColumns;
    bool m_needsSeverity = false;

    friend struct QueryParser;
};

} // namespace logdor
