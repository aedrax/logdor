#include "logdor/ExportScan.h"

#include <QElapsedTimer>
#include <QFile>
#include <QtConcurrentRun>

namespace logdor {

namespace {

constexpr qint64 kBatchRows = 64 * 1024;

// RFC 4180: quote when the value contains a comma, quote, or newline;
// double embedded quotes.
QByteArray csvField(const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    if (!utf8.contains(',') && !utf8.contains('"') && !utf8.contains('\n')
        && !utf8.contains('\r'))
        return utf8;
    QByteArray quoted = "\"";
    for (char c : utf8) {
        if (c == '"')
            quoted += '"';
        quoted += c;
    }
    quoted += '"';
    return quoted;
}

} // namespace

QFuture<ExportResult> exportRows(std::shared_ptr<FileSource> source,
                                 std::shared_ptr<const LineIndex> index,
                                 std::shared_ptr<const FormatParser> parser,
                                 RowSet rows, std::vector<qint32> order,
                                 ExportRequest request)
{
    Q_ASSERT(source && index && parser);
    Q_ASSERT(order.empty() || qint64(order.size()) == rows.size());

    return QtConcurrent::run([source, index, parser, rows = std::move(rows),
                              order = std::move(order),
                              request](QPromise<ExportResult>& promise) {
        QElapsedTimer timer;
        timer.start();
        promise.setProgressRange(0, 1000);

        ExportResult result;
        QFile out(request.outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.error = QStringLiteral("Cannot write %1: %2")
                               .arg(request.outPath, out.errorString());
            result.elapsedMs = timer.elapsed();
            promise.addResult(std::move(result));
            return;
        }

        const auto schema = parser->schema();
        QByteArray buffer;
        if (request.format == ExportFormat::Csv && request.csvHeader) {
            for (int i = 0; i < schema.size(); ++i) {
                if (i > 0)
                    buffer += ',';
                buffer += csvField(schema[i].name);
            }
            buffer += '\n';
        }

        const qint64 total = std::max<qint64>(rows.size(), 1);
        ParsedRow parsed;
        bool failed = false;
        for (qint64 row = 0; row < rows.size() && !failed; ++row) {
            if ((row % kBatchRows) == 0) {
                if (promise.isCanceled()) {
                    out.close();
                    out.remove(); // no half-truths on disk
                    return;
                }
                if (!buffer.isEmpty()) {
                    failed = out.write(buffer) != buffer.size();
                    buffer.clear();
                }
                promise.setProgressValue(int(row * 1000 / total));
            }
            const qint64 line
                = rows.sourceLine(order.empty() ? row : order[size_t(row)]);
            if (request.format == ExportFormat::Text) {
                buffer += source->read(index->offsetOf(line),
                                       index->lengthOf(line));
                buffer += '\n';
            } else {
                const QByteArray raw = source->read(index->offsetOf(line),
                                                    index->lengthOf(line));
                parser->parseLine(QByteArrayView(raw), parsed);
                for (int i = 0; i < schema.size(); ++i) {
                    if (i > 0)
                        buffer += ',';
                    if (i < parsed.fields.size())
                        buffer += csvField(parsed.fields[i]);
                }
                buffer += '\n';
            }
            ++result.rowsWritten;
        }
        if (!failed && !buffer.isEmpty())
            failed = out.write(buffer) != buffer.size();

        if (failed) {
            result.error = QStringLiteral("Write failed: %1")
                               .arg(out.errorString());
            out.close();
            out.remove();
            result.rowsWritten = 0;
            result.elapsedMs = timer.elapsed();
            promise.addResult(std::move(result));
            return;
        }
        out.close();
        result.elapsedMs = timer.elapsed();
        promise.setProgressValue(1000);
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
