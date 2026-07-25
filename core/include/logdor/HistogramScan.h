#pragma once

#include "logdor/Query.h"
#include "logdor/RowSet.h"

#include <QFuture>

#include <array>
#include <memory>
#include <vector>

namespace logdor {

struct HistogramRequest {
    qint64 fromMs = 0; ///< 0,0 => auto-range over the rows' valid epochs
    qint64 toMs = 0;
    int bucketCount = 512;
};

struct HistogramResult {
    qint64 fromMs = 0;
    qint64 toMs = 0;
    qint64 bucketWidthMs = 1;
    /// bucketCount entries; each is a per-severity count indexed by
    /// logdor::Severity (None..Fatal). Bucket i covers the half-open range
    /// [fromMs + i*width, fromMs + (i+1)*width); the chosen width guarantees
    /// toMs itself lands in the last bucket.
    std::vector<std::array<qint64, 7>> buckets;
    qint64 invalidRows = 0; ///< rows with no parseable epoch (never binned)
    qint64 minMs = 0;       ///< observed span of valid epochs; when there
    qint64 maxMs = -1;      ///< are none, minMs > maxMs
    qint64 elapsedMs = 0;
};

/**
 * Bucket the visible rows' timestamps for the timeline strip: one pure pass
 * over the extracted epoch lane (no file I/O; monotonic columns work too -
 * the axis is then ms since boot). Rows outside an explicit range are
 * ignored; unparseable rows are counted in invalidRows. @p severity may be
 * null - everything then lands in the Severity::None lane. Auto-ranging
 * makes two passes (min/max, then binning). Same QPromise contract as the
 * other scans: cancellable between 1M-row slices, permille progress.
 */
QFuture<HistogramResult> scanHistogram(
    RowSet rows, std::shared_ptr<const ColumnData> timeColumn,
    std::shared_ptr<const std::vector<quint8>> severity,
    HistogramRequest request = {});

} // namespace logdor
