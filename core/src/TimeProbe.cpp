#include "logdor/TimeProbe.h"

#include <algorithm>

namespace logdor {

namespace {

int timeColumnOf(const QList<FieldSchema>& schema)
{
    for (int i = 0; i < schema.size(); ++i) {
        if (schema[i].hint == FieldHint::Timestamp)
            return i;
    }
    for (int i = 0; i < schema.size(); ++i) {
        if (schema[i].type == FieldType::DateTime)
            return i;
    }
    return -1;
}

QString fieldValue(const FileSource& source, const LineIndex& index,
                   const FormatParser& parser, qint64 line, int column,
                   ParsedRow& row)
{
    const QByteArray raw
        = source.read(index.offsetOf(line), index.lengthOf(line));
    parser.parseLine(QByteArrayView(raw), row);
    return column < row.fields.size() ? row.fields[column] : QString();
}

} // namespace

TimeRangeProbe probeTimeRange(const FileSource& source, const LineIndex& index,
                              const FormatParser& parser,
                              const TimeParseContext& context,
                              qint64 sampleLines)
{
    TimeRangeProbe probe;
    const auto schema = parser.schema();
    const int column = timeColumnOf(schema);
    const qint64 total = index.lineCount();
    if (column < 0 || total == 0)
        return probe;

    const qint64 headEnd = std::min(total, sampleLines);
    const qint64 tailBegin = std::max(headEnd, total - sampleLines);
    ParsedRow row;

    // Codec: declared timeFormat wins, else detect from the head sample -
    // the extractColumns resolution rules.
    TimestampCodec codec
        = TimestampCodec::fromFormatString(schema[column].timeFormat, context);
    if (!codec.isValid()) {
        QStringList samples;
        for (qint64 line = 0; line < headEnd; ++line) {
            const QString value
                = fieldValue(source, index, parser, line, column, row);
            if (!value.isEmpty())
                samples.append(value);
        }
        codec = TimestampCodec::detect(samples, context);
        if (!codec.isValid())
            return probe;
    }
    probe.monotonic = codec.isMonotonic();

    const auto scan = [&](qint64 begin, qint64 end) {
        for (qint64 line = begin; line < end; ++line) {
            const QString value
                = fieldValue(source, index, parser, line, column, row);
            qint64 ms = 0;
            if (value.isEmpty() || !codec.parse(value, &ms))
                continue;
            if (!probe.valid) {
                probe.valid = true;
                probe.firstMs = probe.lastMs = ms;
            } else {
                probe.firstMs = std::min(probe.firstMs, ms);
                probe.lastMs = std::max(probe.lastMs, ms);
            }
        }
    };
    scan(0, headEnd);
    scan(tailBegin, total);
    return probe;
}

} // namespace logdor
