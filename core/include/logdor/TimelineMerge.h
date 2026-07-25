#pragma once

#include "logdor/Query.h"
#include "logdor/RowSet.h"

#include <QFuture>

#include <memory>
#include <vector>

namespace logdor {

/// One row of a merged multi-file timeline: which input file, which line.
struct TimelineRow {
    qint32 fileId = 0;
    qint32 line = 0;
};

/// One file's contribution: its visible rows and its extracted time column
/// (DateTime; only the integer epoch lane is read). Monotonic (uptime)
/// columns are a precondition violation - ms-since-boot is not comparable
/// across files; the shell rejects those files before merging.
struct TimelineInput {
    qint32 fileId = 0;
    RowSet rows;
    std::shared_ptr<const ColumnData> timeColumn;
};

struct TimelineMergeResult {
    /// Ascending (epochMs, fileId, line); rows with no valid epoch excluded.
    std::vector<TimelineRow> order;
    /// Rows dropped for lacking a valid epoch, by input position.
    std::vector<qint64> droppedPerInput;
    qint64 elapsedMs = 0;
};

/**
 * Merge N files' visible rows into one time-ascending order. Rows within a
 * file need not be time-ordered (out-of-order timestamps are common); the
 * result is one global stable sort. Runs on a single worker; cancellation
 * is honored between per-input gather passes and around the sort.
 */
QFuture<TimelineMergeResult> mergeTimeline(std::vector<TimelineInput> inputs);

} // namespace logdor
