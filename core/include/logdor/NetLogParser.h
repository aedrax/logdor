#pragma once

#include "logdor/FormatParser.h"

#include <QHash>

#include <memory>

namespace logdor {

class FileSource;
class LineIndex;

/**
 * Chrome NetLog (chrome://net-export) parser. The capture is one JSON
 * document - a huge "constants" object on the first line, then one event
 * object per line inside "events": [...] - so decoding a line needs the
 * constants tables (event/source type names, phase ids, timeTickOffset)
 * loaded up front: a parser instance is constructed per file via fromFile
 * (CsvParser precedent - immutable after construction, parseLine is
 * thread-safe) and it is deliberately absent from builtinParsers().
 *
 * Event "time" values are tick milliseconds; timeTickOffset converts them
 * to epoch UTC, rendered as ISO 8601. Wrapper lines (the constants line,
 * '"events": [', the closing brackets/polledData tail) and the half-written
 * last line of a crashed-browser capture fall back as ok=false rows with
 * the raw text in Params. Event types newer than the capture's constants
 * render as their bare number.
 *
 * Limitation: no source-id -> description enrichment (URLs etc.) - that
 * would need an unbounded prescan of every event's params; the Source
 * column shows "<SOURCE_TYPE> <id>" instead. Field order: Time, Source,
 * Phase, Event, Params.
 */
class NetLogParser : public FormatParser {
public:
    enum Field { Time = 0, Source, Phase, Event, Params, FieldCount };

    /**
     * Build from the file's constants line (capped at 8 MiB; tolerates the
     * trailing comma Chrome writes and a missing closing brace). nullptr
     * when the file is not a net-export capture - callers fall back to
     * plaintext.
     */
    static std::shared_ptr<const NetLogParser> fromFile(const FileSource& source,
                                                        const LineIndex& index);

    QString id() const override { return QStringLiteral("netlog"); }
    QString displayName() const override { return QStringLiteral("Chrome NetLog (net-export)"); }
    QList<FieldSchema> schema() const override;
    void parseLine(QByteArrayView raw, ParsedRow& out) const override;
    bool matchesStructure(QByteArrayView raw) const override;
    double specificity() const override { return 0.97; }

    /**
     * Only event lines carry data: hide the multi-MB constants line (line 0)
     * and the '"events": ['/closing-bracket wrapper lines, keeping every
     * '{'-led row - including the ok=false truncated tail of a
     * crashed-browser capture.
     */
    bool hasMetaLines() const override { return true; }
    bool isDataLine(qint64 lineNumber, QByteArrayView raw) const override
    {
        return lineNumber != 0 && raw.trimmed().startsWith('{');
    }

private:
    NetLogParser() = default;

    QHash<int, QString> m_eventTypeNames;  // inverted constants.logEventTypes
    QHash<int, QString> m_sourceTypeNames; // inverted constants.logSourceType
    int m_phaseBegin = 1;                  // constants.logEventPhase
    int m_phaseEnd = 2;
    qint64 m_timeTickOffsetMs = 0;         // constants.timeTickOffset
};

} // namespace logdor
