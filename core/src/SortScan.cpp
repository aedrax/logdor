#include "logdor/SortScan.h"

#include <QElapsedTimer>
#include <QtConcurrentRun>

#include <algorithm>
#include <numeric>

namespace logdor {

QFuture<SortResult> sortRows(RowSet rows, SortKeyKind kind,
                             std::shared_ptr<const ColumnData> keys,
                             std::shared_ptr<const std::vector<quint8>> severity)
{
    Q_ASSERT(kind == SortKeyKind::Severity ? severity != nullptr
                                           : keys != nullptr);

    return QtConcurrent::run([rows = std::move(rows), kind,
                              keys = std::move(keys),
                              severity = std::move(severity)](
                                 QPromise<SortResult>& promise) {
        QElapsedTimer timer;
        timer.start();

        SortResult result;
        result.order.resize(size_t(rows.size()));
        std::iota(result.order.begin(), result.order.end(), 0);

        if (promise.isCanceled())
            return;

        switch (kind) {
        case SortKeyKind::Integer:
            std::stable_sort(result.order.begin(), result.order.end(),
                [&](qint32 a, qint32 b) {
                    qint64 va = 0, vb = 0;
                    // Unparseable rows sort first (valid=false < valid=true).
                    const bool okA = keys->intAt(rows.sourceLine(a), &va);
                    const bool okB = keys->intAt(rows.sourceLine(b), &vb);
                    if (okA != okB)
                        return !okA;
                    return va < vb;
                });
            break;
        case SortKeyKind::Text:
            std::stable_sort(result.order.begin(), result.order.end(),
                [&](qint32 a, qint32 b) {
                    return keys->stringAt(rows.sourceLine(a))
                        .compare(keys->stringAt(rows.sourceLine(b))) < 0;
                });
            break;
        case SortKeyKind::Severity:
            std::stable_sort(result.order.begin(), result.order.end(),
                [&](qint32 a, qint32 b) {
                    return (*severity)[size_t(rows.sourceLine(a))]
                        < (*severity)[size_t(rows.sourceLine(b))];
                });
            break;
        }

        if (promise.isCanceled())
            return;
        result.elapsedMs = timer.elapsed();
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
