// bench_tail: performance gate for follow mode's growth tick.
//
// Usage: bench_tail <logfile> [--append-bytes 64K] [--iterations 8]
//        [--query text] [--max-extend-ms N] [--max-scan-ms N]
//
// Copies the corpus to scratch, then repeatedly appends a burst, reopens,
// extends the index, and tail-scans the new lines - the exact per-tick work
// FollowController schedules. Gates the WORST iteration: a tick must sit
// well under the 1 s poll interval on a 10M-line file. A final full rebuild
// cross-checks the chained extends.

#include <logdor/FilterScan.h>
#include <logdor/LineIndexer.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

using namespace logdor;

namespace {

qint64 parseBytes(const QString& text, bool* ok)
{
    QString t = text.trimmed().toUpper();
    qint64 mult = 1;
    if (t.endsWith('K')) { mult = 1024; t.chop(1); }
    else if (t.endsWith('M')) { mult = 1024 * 1024; t.chop(1); }
    else if (t.endsWith('G')) { mult = 1024 * 1024 * 1024; t.chop(1); }
    return t.toLongLong(ok) * mult;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("logfile", "Corpus to copy and grow");
    parser.addOptions({
        { "append-bytes", "Burst size per iteration", "n", "64K" },
        { "iterations", "Number of grow-extend-scan ticks", "n", "8" },
        { "query", "Tail-scan filter text", "text", "transaction" },
        { "max-extend-ms", "Fail if the slowest extend exceeds this", "n",
          "1000000" },
        { "max-scan-ms", "Fail if the slowest tail scan exceeds this", "n",
          "1000000" },
    });
    parser.process(app);
    if (parser.positionalArguments().isEmpty()) {
        std::fprintf(stderr, "bench_tail: missing logfile argument\n");
        return 2;
    }

    const QString corpus = parser.positionalArguments().first();
    bool okBytes = false;
    const qint64 appendBytes = parseBytes(parser.value("append-bytes"), &okBytes);
    const int iterations = parser.value("iterations").toInt();
    const qint64 maxExtendMs = parser.value("max-extend-ms").toLongLong();
    const qint64 maxScanMs = parser.value("max-scan-ms").toLongLong();
    if (!okBytes || appendBytes <= 0 || iterations <= 0) {
        std::fprintf(stderr, "bench_tail: invalid sizes\n");
        return 2;
    }

    // Grow a scratch copy so the shared corpus stays byte-exact for the
    // other benches.
    const QString scratch = corpus + ".tail-scratch";
    QFile::remove(scratch);
    if (!QFile::copy(corpus, scratch)) {
        std::fprintf(stderr, "bench_tail: cannot copy corpus to %s\n",
                     qPrintable(scratch));
        return 1;
    }

    auto source = FileSource::open(scratch);
    if (!source) {
        std::fprintf(stderr, "bench_tail: cannot open %s\n", qPrintable(scratch));
        return 1;
    }
    auto indexFuture = buildLineIndex(source);
    indexFuture.waitForFinished();
    auto index = indexFuture.result().index;
    std::printf("file:            %s (%lld lines, %lld MB)\n",
                qPrintable(corpus), (long long)index->lineCount(),
                (long long)(index->fileSize() / (1024 * 1024)));

    QByteArray burst;
    while (burst.size() < appendBytes)
        burst += "appended transaction line for the follow-mode growth tick\n";

    LineFilter filter;
    filter.query = parser.value("query");

    qint64 worstExtendMs = 0, worstScanMs = 0, totalNewLines = 0;
    for (int i = 0; i < iterations; ++i) {
        {
            QFile grow(scratch);
            if (!grow.open(QIODevice::WriteOnly | QIODevice::Append)
                || grow.write(burst) != burst.size()) {
                std::fprintf(stderr, "bench_tail: append failed\n");
                return 1;
            }
        }
        const qint64 oldCount = index->lineCount();
        const qint64 firstNewLine
            = index->lastLineTerminated() ? oldCount : oldCount - 1;

        auto reopened = FileSource::open(scratch); // follow always reopens
        if (!reopened) {
            std::fprintf(stderr, "bench_tail: reopen failed\n");
            return 1;
        }
        auto extendFuture = extendLineIndex(reopened, index);
        extendFuture.waitForFinished();
        const IndexingResult extended = extendFuture.result();
        worstExtendMs = qMax(worstExtendMs, extended.elapsedMs);

        auto scanFuture = scanFilter(reopened, extended.index, filter,
                                     kDefaultFilterChunkLines, firstNewLine);
        scanFuture.waitForFinished();
        const FilterScanResult scan = scanFuture.result();
        worstScanMs = qMax(worstScanMs, scan.elapsedMs);

        totalNewLines += extended.index->lineCount() - oldCount;
        source = std::move(reopened);
        index = extended.index;
    }

    // Sanity: the chained extends must agree with a cold rebuild.
    auto rebuildFuture = buildLineIndex(source);
    rebuildFuture.waitForFinished();
    const auto rebuilt = rebuildFuture.result().index;
    QFile::remove(scratch);
    if (rebuilt->lineCount() != index->lineCount()
        || rebuilt->fileSize() != index->fileSize()) {
        std::fprintf(stderr, "FAIL: extended index diverged from rebuild "
                             "(%lld vs %lld lines)\n",
                     (long long)index->lineCount(),
                     (long long)rebuilt->lineCount());
        return 1;
    }

    std::printf("ticks:           %d x %lld KB appended (%lld new lines)\n",
                iterations, (long long)(appendBytes / 1024),
                (long long)totalNewLines);
    std::printf("worst extend:    %lld ms\n", (long long)worstExtendMs);
    std::printf("worst tail scan: %lld ms\n", (long long)worstScanMs);

    bool ok = true;
    if (worstExtendMs > maxExtendMs) {
        std::fprintf(stderr, "FAIL: extend %lld ms > gate %lld ms\n",
                     (long long)worstExtendMs, (long long)maxExtendMs);
        ok = false;
    }
    if (worstScanMs > maxScanMs) {
        std::fprintf(stderr, "FAIL: tail scan %lld ms > gate %lld ms\n",
                     (long long)worstScanMs, (long long)maxScanMs);
        ok = false;
    }
    return ok ? 0 : 1;
}
