// bench_query: performance gate for field queries.
//
// Usage: bench_query <logfile> --query "level:error tag:Wifi*"
//        [--min-extract-mbps N] [--max-warm-ms N] [--check-cancel-ms N]
//
// Cold = one-pass column extraction (parse-bound). Warm = query evaluation
// over the cached columns. Both gated separately.
//
// Temporal terms work too - e.g. --query "time>=\"01-01 10:00:00.000\"" or
// --query "time<12:30" on a logcat file. DateTimeCmp is an integer compare
// (IntCmp-class throughput); TimeOfDayCmp adds a binary search per row.

#include <logdor/ColumnScan.h>
#include <logdor/FilterScan.h>
#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <cstdio>

using namespace logdor;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("logfile", "File to query");
    parser.addOptions({
        { "query", "Field query", "text", "level:error" },
        { "format", "Parser id", "id", "logcat" },
        { "min-extract-mbps", "Fail below this extraction throughput", "n", "0" },
        { "max-warm-ms", "Fail if the cached query is slower", "n", "1000000" },
        { "check-cancel-ms", "Fail if extraction cancel takes longer (0 = skip)", "n", "0" },
    });
    parser.process(app);
    if (parser.positionalArguments().isEmpty()) {
        std::fprintf(stderr, "bench_query: missing logfile argument\n");
        return 2;
    }

    const QString path = parser.positionalArguments().first();
    const double minExtractMbps = parser.value("min-extract-mbps").toDouble();
    const qint64 maxWarmMs = parser.value("max-warm-ms").toLongLong();
    const qint64 maxCancelMs = parser.value("check-cancel-ms").toLongLong();

    auto source = FileSource::open(path);
    if (!source) {
        std::fprintf(stderr, "bench_query: cannot open %s\n", qPrintable(path));
        return 1;
    }
    auto indexFuture = buildLineIndex(source);
    indexFuture.waitForFinished();
    const auto index = indexFuture.result().index;
    auto format = parserById(parser.value("format"));
    if (!format) {
        std::fprintf(stderr, "bench_query: unknown format\n");
        return 2;
    }

    QueryError error;
    auto query = CompiledQuery::compile(parser.value("query"), format->schema(),
                                        Qt::CaseInsensitive, {}, &error);
    if (!query) {
        std::fprintf(stderr, "bench_query: bad query: %s\n",
                     qPrintable(error.message));
        return 2;
    }

    // Cold: extraction.
    auto extract = extractColumns(source, index, format,
                                  query->referencedColumns(),
                                  query->needsSeverity());
    extract.waitForFinished();
    const ColumnScanResult cols = extract.result();

    const double mb = double(index->fileSize()) / (1000.0 * 1000.0);
    const double extractMbps = cols.elapsedMs > 0
        ? mb / (double(cols.elapsedMs) / 1000.0) : 1e9;

    size_t columnBytes = cols.severity ? cols.severity->capacity() : 0;
    for (const auto& c : cols.columns)
        columnBytes += c->memoryUsage();

    LineFilter filter;
    filter.fieldQuery = query;
    filter.columns.columns = cols.columns;
    filter.columns.severity = cols.severity;

    const auto runQuery = [&]() {
        auto scan = scanFilter(source, index, filter);
        scan.waitForFinished();
        return scan.result();
    };
    runQuery(); // warm the path once
    const FilterScanResult warm = runQuery();

    std::printf("file:            %s (%.1f MB, %lld lines)\n", qPrintable(path),
                mb, (long long)index->lineCount());
    std::printf("query:           \"%s\" -> %lld matches\n",
                qPrintable(query->text()), (long long)warm.matchCount);
    std::printf("extraction:      %lld ms  (%.0f MB/s), %zu column bytes\n",
                (long long)cols.elapsedMs, extractMbps, columnBytes);
    std::printf("warm query:      %lld ms\n", (long long)warm.rows.size() >= 0
                    ? (long long)warm.elapsedMs : 0);

    bool ok = true;
    if (extractMbps < minExtractMbps) {
        std::fprintf(stderr, "FAIL: extraction %.0f MB/s < gate %.0f MB/s\n",
                     extractMbps, minExtractMbps);
        ok = false;
    }
    if (warm.elapsedMs > maxWarmMs) {
        std::fprintf(stderr, "FAIL: warm query %lld ms > gate %lld ms\n",
                     (long long)warm.elapsedMs, (long long)maxWarmMs);
        ok = false;
    }

    if (maxCancelMs > 0) {
        auto future = extractColumns(source, index, format,
                                     query->referencedColumns(),
                                     query->needsSeverity());
        QThread::msleep(20);
        QElapsedTimer cancelTimer;
        cancelTimer.start();
        future.cancel();
        future.waitForFinished();
        const qint64 cancelMs = cancelTimer.elapsed();
        std::printf("cancel latency:  %lld ms\n", (long long)cancelMs);
        if (cancelMs > maxCancelMs) {
            std::fprintf(stderr, "FAIL: cancel latency %lld ms > gate %lld ms\n",
                         (long long)cancelMs, (long long)maxCancelMs);
            ok = false;
        }
    }

    return ok ? 0 : 1;
}
