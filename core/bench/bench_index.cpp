// bench_index: performance gate for line indexing.
//
// Usage: bench_index <logfile> --min-mbps N --max-bytes-per-line X --check-cancel-ms N
//
// Runs buildLineIndex twice and scores the warm run (the cold run is
// disk-bound and machine-dependent). Exit code != 0 when a gate is missed,
// so CTest treats threshold regressions as failures.

#include <logdor/FileSource.h>
#include <logdor/LineIndexer.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>

#include <cstdio>

namespace {

qint64 vmHwmKb()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly))
        return -1;
    for (const QByteArray& line : status.readAll().split('\n')) {
        if (line.startsWith("VmHWM:"))
            return line.mid(6).trimmed().split(' ').first().toLongLong();
    }
    return -1;
}

logdor::IndexingResult runOnce(const QString& path)
{
    auto src = logdor::FileSource::open(path);
    if (!src) {
        std::fprintf(stderr, "bench_index: cannot open %s\n", qPrintable(path));
        std::exit(1);
    }
    auto future = logdor::buildLineIndex(std::move(src));
    future.waitForFinished();
    return future.result();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("logfile", "File to index");
    parser.addOptions({
        { "min-mbps", "Fail below this warm throughput (MB/s)", "n", "0" },
        { "max-bytes-per-line", "Fail above this index memory per line", "x", "1e9" },
        { "check-cancel-ms", "Fail if cancellation takes longer (0 = skip)", "n", "0" },
    });
    parser.process(app);
    if (parser.positionalArguments().isEmpty()) {
        std::fprintf(stderr, "bench_index: missing logfile argument\n");
        return 2;
    }
    const QString path = parser.positionalArguments().first();
    const double minMbps = parser.value("min-mbps").toDouble();
    const double maxBytesPerLine = parser.value("max-bytes-per-line").toDouble();
    const qint64 maxCancelMs = parser.value("check-cancel-ms").toLongLong();

    const qint64 hwmBefore = vmHwmKb();

    // Cold run warms the page cache; warm run is the scored one.
    const auto cold = runOnce(path);
    const auto warm = runOnce(path);
    if (!warm.index || warm.lineCount == 0) {
        std::fprintf(stderr, "bench_index: indexing produced no lines\n");
        return 1;
    }

    const double mb = double(warm.bytesScanned) / (1000.0 * 1000.0);
    const double mbps = warm.elapsedMs > 0 ? mb / (double(warm.elapsedMs) / 1000.0)
                                           : 1e9;
    const double bytesPerLine =
        double(warm.index->memoryUsage()) / double(warm.lineCount);
    const qint64 hwmAfter = vmHwmKb();

    std::printf("file:            %s (%.1f MB, %lld lines%s)\n", qPrintable(path),
                mb, (long long)warm.lineCount,
                warm.index->isWide() ? ", wide index" : "");
    std::printf("cold run:        %lld ms\n", (long long)cold.elapsedMs);
    std::printf("warm run:        %lld ms  (%.0f MB/s)\n",
                (long long)warm.elapsedMs, mbps);
    std::printf("index memory:    %.2f bytes/line (%lld KiB total)\n", bytesPerLine,
                (long long)(warm.index->memoryUsage() / 1024));
    if (hwmBefore > 0 && hwmAfter > 0)
        std::printf("peak RSS delta:  %lld KiB\n", (long long)(hwmAfter - hwmBefore));

    bool ok = true;
    if (mbps < minMbps) {
        std::fprintf(stderr, "FAIL: throughput %.0f MB/s < gate %.0f MB/s\n",
                     mbps, minMbps);
        ok = false;
    }
    if (bytesPerLine > maxBytesPerLine) {
        std::fprintf(stderr, "FAIL: index memory %.2f B/line > gate %.2f B/line\n",
                     bytesPerLine, maxBytesPerLine);
        ok = false;
    }

    if (maxCancelMs > 0) {
        auto src = logdor::FileSource::open(path);
        auto future = logdor::buildLineIndex(std::move(src));
        QThread::msleep(20); // let the scan get going
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
