#pragma once

#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QVarLengthArray>

namespace logdor {

// Core stays GUI-free: severity is semantic; the shell maps it to colors.
enum class Severity : quint8 { None, Verbose, Debug, Info, Warning, Error, Fatal };

enum class FieldType : quint8 { String, Integer, DateTime };

// Display hints the shell may honor; core attaches no behavior to them.
enum class FieldHint : quint8 {
    None,
    Numeric,      // right-align
    Timestamp,
    SeverityName,
    Identifier,
    Message,      // the stretch column
};

struct FieldSchema {
    QString name; // becomes the column header
    FieldType type = FieldType::String;
    FieldHint hint = FieldHint::None;
    /**
     * DateTime fields only: how to parse values into UTC epoch milliseconds.
     * Reserved words "iso8601", "epoch-s", "epoch-ms", "epoch-us", "uptime";
     * anything else is a Qt date/time pattern (see TimestampParse.h). Empty =
     * auto-detect from sample values.
     */
    QString timeFormat;
};

struct ParsedRow {
    QVarLengthArray<QString, 8> fields; // one per FieldSchema entry, in order
    Severity severity = Severity::None;
    bool ok = false; // false => the structured parse failed (fallback fields)
};

/**
 * A log format: a display schema plus a pure per-line parse.
 *
 * Implementations must be stateless and thread-safe - parseLine runs
 * concurrently from filter scans and model fills. @p raw excludes the line
 * terminator and any trailing '\r' (LineIndex::lengthOf semantics).
 *
 * parseLine must fill out.fields to schema().size() even when the line does
 * not match the format (fallback: message field = raw, ok = false), so a
 * mixed-content file always renders every line.
 */
class FormatParser {
public:
    virtual ~FormatParser() = default;

    virtual QString id() const = 0;          // "plaintext" | "logcat" | "clf"
    virtual QString displayName() const = 0;
    virtual QList<FieldSchema> schema() const = 0;

    /// True when rows should be colored by ParsedRow::severity.
    virtual bool colorsBySeverity() const { return false; }

    virtual void parseLine(QByteArrayView raw, ParsedRow& out) const = 0;

    /**
     * True when some lines are format scaffolding rather than data (a CSV
     * header row, W3C directives, NetLog wrapper lines). Shells consult
     * isDataLine to hide them; false (the default) promises every line is
     * data, letting shells skip the per-line check entirely.
     */
    virtual bool hasMetaLines() const { return false; }

    /**
     * False for scaffolding lines a shell should hide. Only called when
     * hasMetaLines() is true; must be pure and thread-safe like parseLine.
     */
    virtual bool isDataLine(qint64 lineNumber, QByteArrayView raw) const
    {
        Q_UNUSED(lineNumber);
        Q_UNUSED(raw);
        return true;
    }

    /**
     * True when the line structurally matches this format. Used by detection
     * scoring; must NOT count unconditional fallbacks (a parser that accepts
     * anything belongs at low specificity instead).
     */
    virtual bool matchesStructure(QByteArrayView raw) const = 0;

    /// Detection tiebreak weight in (0, 1]; higher = more specific format.
    virtual double specificity() const { return 1.0; }
};

} // namespace logdor
