// bench_filter: performance gate for the filter scan.
//
// Usage: bench_filter <logfile> --query TEXT [--regex] --min-mbps N
//        [--check-cancel-ms N] [--max-rowset-bytes-per-line X]
//
// Indexes the file, runs the scan twice and scores the warm run. Also
// asserts the passthrough (empty-filter) promise: ~0 ms and 0 bytes.

#include <logdor/FileSource.h>
#include <logdor/FilterScan.h>
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
    parser.addPositionalArgument("logfile", "File to scan");
    parser.addOptions({
        { "query", "Filter query text", "text", "error" },
        { "regex", "Treat query as a regular expression" },
        { "min-mbps", "Fail below this warm scan throughput (MB/s)", "n", "0" },
        { "check-cancel-ms", "Fail if cancellation takes longer (0 = skip)", "n", "0" },
        { "max-rowset-bytes-per-line", "Fail above this RowSet memory", "x", "1e9" },
    });
    parser.process(app);
    if (parser.positionalArguments().isEmpty()) {
        std::fprintf(stderr, "bench_filter: missing logfile argument\n");
        return 2;
    }

    const QString path = parser.positionalArguments().first();
    const double minMbps = parser.value("min-mbps").toDouble();
    const qint64 maxCancelMs = parser.value("check-cancel-ms").toLongLong();
    const double maxRowSetBpl = parser.value("max-rowset-bytes-per-line").toDouble();

    auto source = FileSource::open(path);
    if (!source) {
        std::fprintf(stderr, "bench_filter: cannot open %s\n", qPrintable(path));
        return 1;
    }
    auto indexFuture = buildLineIndex(source);
    indexFuture.waitForFinished();
    const auto index = indexFuture.result().index;

    LineFilter filter;
    filter.query = parser.value("query");
    filter.regexMode = parser.isSet("regex");

    const auto runOnce = [&](const LineFilter& f) {
        auto future = scanFilter(source, index, f);
        future.waitForFinished();
        return future.result();
    };

    const auto cold = runOnce(filter);
    const auto warm = runOnce(filter);

    const double mb = double(index->fileSize()) / (1000.0 * 1000.0);
    const double mbps = warm.elapsedMs > 0
        ? mb / (double(warm.elapsedMs) / 1000.0) : 1e9;
    const double rowSetBpl = warm.rows.size() > 0
        ? double(warm.rows.memoryUsage()) / double(warm.rows.size()) : 0.0;

    std::printf("file:            %s (%.1f MB, %lld lines)\n", qPrintable(path),
                mb, (long long)index->lineCount());
    std::printf("query:           \"%s\"%s -> %lld matches, %lld visible rows\n",
                qPrintable(filter.query), filter.regexMode ? " (regex)" : "",
                (long long)warm.matchCount, (long long)warm.rows.size());
    std::printf("cold scan:       %lld ms\n", (long long)cold.elapsedMs);
    std::printf("warm scan:       %lld ms  (%.0f MB/s)\n",
                (long long)warm.elapsedMs, mbps);
    std::printf("rowset memory:   %.2f bytes/visible-line\n", rowSetBpl);

    bool ok = true;
    if (mbps < minMbps) {
        std::fprintf(stderr, "FAIL: throughput %.0f MB/s < gate %.0f MB/s\n",
                     mbps, minMbps);
        ok = false;
    }
    if (rowSetBpl > maxRowSetBpl) {
        std::fprintf(stderr, "FAIL: rowset %.2f B/line > gate %.2f B/line\n",
                     rowSetBpl, maxRowSetBpl);
        ok = false;
    }

    // Passthrough promise: empty filter resolves instantly with no allocation.
    const auto passthrough = runOnce(LineFilter{});
    std::printf("passthrough:     %lld ms, %zu bytes, isAll=%d\n",
                (long long)passthrough.elapsedMs, passthrough.rows.memoryUsage(),
                passthrough.rows.isAll());
    if (passthrough.elapsedMs > 50 || passthrough.rows.memoryUsage() != 0
        || !passthrough.rows.isAll()) {
        std::fprintf(stderr, "FAIL: passthrough filter not O(1)\n");
        ok = false;
    }

    if (maxCancelMs > 0) {
        auto future = scanFilter(source, index, filter);
        QThread::msleep(10);
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
