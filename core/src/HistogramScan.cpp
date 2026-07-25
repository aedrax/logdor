#include "logdor/HistogramScan.h"

#include <QElapsedTimer>
#include <QtConcurrentRun>

#include <algorithm>

namespace logdor {

namespace {

constexpr qint64 kSliceRows = 1'000'000;

// floor(span/N)+1 keeps every value in [from, to] inside the last bucket
// while each bucket stays half-open.
qint64 widthFor(qint64 fromMs, qint64 toMs, int bucketCount)
{
    return std::max<qint64>(1, (toMs - fromMs) / bucketCount + 1);
}

} // namespace

QFuture<HistogramResult> scanHistogram(
    RowSet rows, std::shared_ptr<const ColumnData> timeColumn,
    std::shared_ptr<const std::vector<quint8>> severity,
    HistogramRequest request)
{
    Q_ASSERT(timeColumn);
    Q_ASSERT(request.bucketCount > 0);

    return QtConcurrent::run([rows = std::move(rows),
                              timeColumn = std::move(timeColumn),
                              severity = std::move(severity),
                              request](QPromise<HistogramResult>& promise) {
        QElapsedTimer timer;
        timer.start();
        promise.setProgressRange(0, 1000);

        const ColumnData& time = *timeColumn;
        const qint64 total = std::max<qint64>(rows.size(), 1);
        const bool autoRange = request.fromMs == 0 && request.toMs == 0;

        HistogramResult result;
        result.buckets.assign(size_t(request.bucketCount), {});
        if (!autoRange) {
            result.fromMs = request.fromMs;
            result.toMs = request.toMs;
            result.bucketWidthMs
                = widthFor(result.fromMs, result.toMs, request.bucketCount);
        }

        // Pass 1: observed span + invalid count; with an explicit range this
        // is the only pass and bins as it goes.
        for (qint64 base = 0; base < rows.size(); base += kSliceRows) {
            if (promise.isCanceled())
                return;
            const qint64 end = std::min(rows.size(), base + kSliceRows);
            for (qint64 row = base; row < end; ++row) {
                const qint64 line = rows.sourceLine(row);
                qint64 ms = 0;
                if (!time.intAt(line, &ms)) {
                    ++result.invalidRows;
                    continue;
                }
                if (result.minMs > result.maxMs) {
                    result.minMs = result.maxMs = ms;
                } else {
                    result.minMs = std::min(result.minMs, ms);
                    result.maxMs = std::max(result.maxMs, ms);
                }
                if (!autoRange) {
                    if (ms < result.fromMs || ms > result.toMs)
                        continue; // outside the brushed range: ignored
                    const size_t bucket
                        = size_t((ms - result.fromMs) / result.bucketWidthMs);
                    const size_t lane
                        = severity && size_t(line) < severity->size()
                        ? std::min<size_t>((*severity)[size_t(line)], 6)
                        : 0;
                    ++result.buckets[bucket][lane];
                }
            }
            promise.setProgressValue(int(end * (autoRange ? 500 : 1000) / total));
        }

        if (autoRange && result.minMs <= result.maxMs) {
            result.fromMs = result.minMs;
            result.toMs = result.maxMs;
            result.bucketWidthMs
                = widthFor(result.fromMs, result.toMs, request.bucketCount);

            for (qint64 base = 0; base < rows.size(); base += kSliceRows) {
                if (promise.isCanceled())
                    return;
                const qint64 end = std::min(rows.size(), base + kSliceRows);
                for (qint64 row = base; row < end; ++row) {
                    const qint64 line = rows.sourceLine(row);
                    qint64 ms = 0;
                    if (!time.intAt(line, &ms))
                        continue; // counted in pass 1
                    const size_t bucket
                        = size_t((ms - result.fromMs) / result.bucketWidthMs);
                    const size_t lane
                        = severity && size_t(line) < severity->size()
                        ? std::min<size_t>((*severity)[size_t(line)], 6)
                        : 0;
                    ++result.buckets[bucket][lane];
                }
                promise.setProgressValue(int(500 + end * 500 / total));
            }
        }

        result.elapsedMs = timer.elapsed();
        promise.setProgressValue(1000);
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
