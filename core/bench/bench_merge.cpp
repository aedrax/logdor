// bench_merge: performance gate for the merged-timeline sort.
//
// Usage: bench_merge <logfile> [--copies N] [--max-warm-ms N]
//        [--max-rss-mb N] [--check-cancel-ms N]
//
// The file's time column is extracted once (that cost is bench_query's
// concern), then presented as N independent timeline inputs - the same shape
// the Merged Timeline view produces for N open files. Warm = one full
// gather + stable_sort over all inputs. VmHWM is reported so column + merge
// memory stays visible; it includes the mmap'd corpus pages.
//
// Cancellation in mergeTimeline is per-phase (between input gathers, around
// the sort) - the gate bounds how long a cancel issued mid-gather can take.

#include <logdor/ColumnScan.h>
#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>
#include <logdor/TimelineMerge.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>

#include <cstdio>

using namespace logdor;

namespace {

qint64 vmHwmMb()
{
#ifdef Q_OS_LINUX
    QFile status(QStringLiteral("/proc/self/status"));
    if (status.open(QIODevice::ReadOnly)) {
        // /proc files report size 0, so atEnd() is useless; read until empty.
        for (QByteArray line = status.readLine(); !line.isEmpty();
             line = status.readLine()) {
            if (line.startsWith("VmHWM:"))
                return line.mid(6).trimmed().split(' ').first().toLongLong()
                    / 1024;
        }
    }
#endif
    return -1;
}

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

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("logfile", "File whose time column to merge");
    parser.addOptions({
        { "format", "Parser id", "id", "logcat" },
        { "copies", "Present the file as this many timeline inputs", "n", "2" },
        { "max-warm-ms", "Fail if the warm merge is slower", "n", "1000000" },
        { "max-rss-mb", "Fail above this VmHWM (0 = skip)", "n", "0" },
        { "check-cancel-ms", "Fail if a merge cancel takes longer (0 = skip)",
          "n", "0" },
    });
    parser.process(app);
    if (parser.positionalArguments().isEmpty()) {
        std::fprintf(stderr, "bench_merge: missing logfile argument\n");
        return 2;
    }

    const QString path = parser.positionalArguments().first();
    const int copies = qMax(1, parser.value("copies").toInt());
    const qint64 maxWarmMs = parser.value("max-warm-ms").toLongLong();
    const qint64 maxRssMb = parser.value("max-rss-mb").toLongLong();
    const qint64 maxCancelMs = parser.value("check-cancel-ms").toLongLong();

    auto source = FileSource::open(path);
    if (!source) {
        std::fprintf(stderr, "bench_merge: cannot open %s\n", qPrintable(path));
        return 1;
    }
    auto indexFuture = buildLineIndex(source);
    indexFuture.waitForFinished();
    const auto index = indexFuture.result().index;
    auto format = parserById(parser.value("format"));
    if (!format) {
        std::fprintf(stderr, "bench_merge: unknown format\n");
        return 2;
    }
    const int timeCol = timeColumnOf(format->schema());
    if (timeCol < 0) {
        std::fprintf(stderr, "bench_merge: format has no time column\n");
        return 2;
    }

    auto extract = extractColumns(source, index, format, { timeCol }, false);
    extract.waitForFinished();
    const auto timeColumn = extract.result().columns.value(timeCol);
    if (!timeColumn || timeColumn->isMonotonicTime()
        || timeColumn->validIntCount() == 0) {
        std::fprintf(stderr, "bench_merge: no mergeable epochs in column\n");
        return 1;
    }

    const auto makeInputs = [&]() {
        std::vector<TimelineInput> inputs;
        for (int i = 0; i < copies; ++i)
            inputs.push_back({ i, RowSet::all(index->lineCount()), timeColumn });
        return inputs;
    };
    const auto runMerge = [&]() {
        auto future = mergeTimeline(makeInputs());
        future.waitForFinished();
        return future.result();
    };
    runMerge(); // warm the path once
    const TimelineMergeResult warm = runMerge();

    qint64 dropped = 0;
    for (qint64 d : warm.droppedPerInput)
        dropped += d;
    const double mrows = double(warm.order.size()) / 1e6;
    const double mrowsPerS = warm.elapsedMs > 0
        ? mrows / (double(warm.elapsedMs) / 1000.0) : 1e9;
    const qint64 rssMb = vmHwmMb();

    std::printf("file:            %s (%lld lines x %d inputs)\n",
                qPrintable(path), (long long)index->lineCount(), copies);
    std::printf("warm merge:      %lld ms  (%.1f Mrows, %.1f Mrows/s, "
                "%lld dropped)\n",
                (long long)warm.elapsedMs, mrows, mrowsPerS, (long long)dropped);
    std::printf("peak rss:        %lld MB\n", (long long)rssMb);

    bool ok = true;
    if (warm.elapsedMs > maxWarmMs) {
        std::fprintf(stderr, "FAIL: warm merge %lld ms > gate %lld ms\n",
                     (long long)warm.elapsedMs, (long long)maxWarmMs);
        ok = false;
    }
    if (maxRssMb > 0 && rssMb > maxRssMb) {
        std::fprintf(stderr, "FAIL: peak rss %lld MB > gate %lld MB\n",
                     (long long)rssMb, (long long)maxRssMb);
        ok = false;
    }

    if (maxCancelMs > 0) {
        auto future = mergeTimeline(makeInputs());
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
