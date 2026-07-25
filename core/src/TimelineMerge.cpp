#include "logdor/TimelineMerge.h"

#include <QElapsedTimer>
#include <QtConcurrentRun>

#include <algorithm>

namespace logdor {

namespace {

// Decorate-sort-strip: carrying the epoch beside the row keeps the sort's
// comparisons on one cache line instead of chasing per-file column lookups.
struct Keyed {
    qint64 ms;
    TimelineRow row;
};

} // namespace

QFuture<TimelineMergeResult> mergeTimeline(std::vector<TimelineInput> inputs)
{
    return QtConcurrent::run([inputs = std::move(inputs)](
                                 QPromise<TimelineMergeResult>& promise) {
        QElapsedTimer timer;
        timer.start();

        TimelineMergeResult result;
        result.droppedPerInput.assign(inputs.size(), 0);

        qint64 totalRows = 0;
        for (const TimelineInput& input : inputs) {
            Q_ASSERT(input.timeColumn && !input.timeColumn->isMonotonicTime());
            totalRows += input.rows.size();
        }

        std::vector<Keyed> keyed;
        keyed.reserve(size_t(totalRows));
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (promise.isCanceled())
                return;
            const TimelineInput& input = inputs[i];
            const ColumnData& time = *input.timeColumn;
            const qint64 count = input.rows.size();
            for (qint64 row = 0; row < count; ++row) {
                const qint64 line = input.rows.sourceLine(row);
                qint64 ms = 0;
                if (!time.intAt(line, &ms)) {
                    ++result.droppedPerInput[i];
                    continue;
                }
                keyed.push_back({ ms, { input.fileId, qint32(line) } });
            }
        }

        if (promise.isCanceled())
            return;
        std::stable_sort(keyed.begin(), keyed.end(),
                         [](const Keyed& a, const Keyed& b) {
                             if (a.ms != b.ms)
                                 return a.ms < b.ms;
                             if (a.row.fileId != b.row.fileId)
                                 return a.row.fileId < b.row.fileId;
                             return a.row.line < b.row.line;
                         });
        if (promise.isCanceled())
            return;

        result.order.reserve(keyed.size());
        for (const Keyed& k : keyed)
            result.order.push_back(k.row);

        result.elapsedMs = timer.elapsed();
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
