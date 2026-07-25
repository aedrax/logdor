#include "logdor/FilterScan.h"

#include "TextMatch_p.h"

#include <QElapsedTimer>
#include <QThread>
#include <QtConcurrentMap>
#include <QtConcurrentRun>

#include <algorithm>

namespace logdor {

using detail::Matcher;

namespace {

std::vector<qint32> scanRange(const FileSource& source, const LineIndex& index,
                              const Matcher& matcher, const LineFilter& filter,
                              qint64 first, qint64 end)
{
    std::vector<qint32> matches;
    const bool queryEmpty = matcher.mode == Matcher::Mode::Empty;

    // Buffered sources: one bulk read per chunk instead of a lock per line.
    QByteArray scratch;
    const char* base = nullptr;
    quint64 baseOffset = 0;
    if (source.isContiguous()) {
        base = source.data();
    } else {
        baseOffset = index.offsetOf(first);
        const quint64 endOffset = end < index.lineCount()
            ? index.offsetOf(end) : index.fileSize();
        scratch.resize(qsizetype(endOffset - baseOffset));
        source.readInto(baseOffset, scratch.data(), scratch.size());
        base = scratch.constData();
    }

    for (qint64 line = first; line < end; ++line) {
        const QByteArrayView raw(base + (index.offsetOf(line) - baseOffset),
                                 index.lengthOf(line));
        bool match;
        if (filter.fieldQuery) {
            // Query mode: the compiled query replaces the plain text match.
            match = filter.fieldQuery->evaluate(line, raw, filter.columns)
                != filter.invert;
        } else {
            // Empty query ignores invert (legacy); with a query, XOR applies.
            match = queryEmpty || (matcher.textMatches(raw) != filter.invert);
        }
        if (match && filter.extraPredicate)
            match = filter.extraPredicate(line, raw);
        if (match)
            matches.push_back(qint32(line));
    }
    return matches;
}

// Union of [m - before, m + after] over sorted matches, clamped to
// [0, total). Linear interval merge; no per-line hashing.
std::vector<qint32> expandContext(const std::vector<qint32>& matches,
                                  int before, int after, qint64 total)
{
    if (before == 0 && after == 0)
        return matches;

    std::vector<qint32> visible;
    visible.reserve(matches.size());
    qint64 nextUnemitted = 0;
    for (qint32 m : matches) {
        const qint64 lo = std::max<qint64>(qint64(m) - before, nextUnemitted);
        const qint64 hi = std::min<qint64>(qint64(m) + after, total - 1);
        for (qint64 line = lo; line <= hi; ++line)
            visible.push_back(qint32(line));
        nextUnemitted = std::max(nextUnemitted, hi + 1);
    }
    return visible;
}

} // namespace

QFuture<FilterScanResult> scanFilter(std::shared_ptr<FileSource> source,
                                     std::shared_ptr<const LineIndex> index,
                                     LineFilter filter, qint64 linesPerChunk,
                                     qint64 firstLine)
{
    Q_ASSERT(source && index);
    Q_ASSERT(linesPerChunk > 0);
    Q_ASSERT(firstLine >= 0);
    Q_ASSERT(!filter.fieldQuery
             || filter.columns.covers(filter.fieldQuery->referencedColumns(),
                                      filter.fieldQuery->needsSeverity()));

    return QtConcurrent::run([source, index, filter = std::move(filter),
                              linesPerChunk,
                              firstLine](QPromise<FilterScanResult>& promise) {
        QElapsedTimer timer;
        timer.start();
        promise.setProgressRange(0, 1000);

        const qint64 total = index->lineCount();
        const qint64 first = std::min(firstLine, total);
        FilterScanResult result;

        if (filter.isPassthrough() || total == first) {
            if (first == 0) {
                result.rows = RowSet::all(total);
            } else {
                std::vector<qint32> tail;
                tail.reserve(size_t(total - first));
                for (qint64 line = first; line < total; ++line)
                    tail.push_back(qint32(line));
                result.rows = RowSet::fromLines(std::move(tail), total);
            }
            result.matchCount = total - first;
            result.elapsedMs = timer.elapsed();
            promise.setProgressValue(1000);
            promise.addResult(std::move(result));
            return;
        }

        const Matcher matcher(filter.query, filter.caseSensitive,
                              filter.regexMode);
        const int threads = qMax(1, QThread::idealThreadCount());
        const qint64 superChunk = linesPerChunk * threads;
        std::vector<qint32> matches;

        struct Range { qint64 first, end; };
        for (qint64 base = first; base < total; base += superChunk) {
            if (promise.isCanceled())
                return;
            const qint64 superEnd = std::min(total, base + superChunk);
            QList<Range> ranges;
            for (qint64 s = base; s < superEnd; s += linesPerChunk)
                ranges.append({ s, std::min(superEnd, s + linesPerChunk) });

            // blockingMapped also executes on this (calling) thread, so
            // running it from inside a pool thread cannot deadlock the pool.
            const auto chunkMatches = QtConcurrent::blockingMapped(ranges,
                std::function<std::vector<qint32>(const Range&)>(
                    [&](const Range& r) {
                        return scanRange(*source, *index, matcher, filter,
                                         r.first, r.end);
                    }));
            for (const auto& v : chunkMatches)
                matches.insert(matches.end(), v.begin(), v.end());
            promise.setProgressValue(
                int((superEnd - first) * 1000 / (total - first)));
        }

        result.matchCount = qint64(matches.size());
        result.rows = RowSet::fromLines(
            expandContext(matches, filter.contextBefore, filter.contextAfter, total),
            total);
        result.elapsedMs = timer.elapsed();
        promise.setProgressValue(1000);
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
