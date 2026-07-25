#pragma once

#include "logdor/FileSource.h"
#include "logdor/LineIndex.h"

#include <QFuture>

#include <memory>

namespace logdor {

struct IndexingResult {
    std::shared_ptr<const LineIndex> index;
    qint64 lineCount = 0;
    quint64 bytesScanned = 0;
    qint64 elapsedMs = 0;
};

constexpr qsizetype kDefaultIndexChunkSize = 16 * 1024 * 1024;

/**
 * Scan @p source for line boundaries on a worker thread.
 *
 * The scan is sequential in @p chunkSize chunks: memchr runs at multiple
 * GB/s on one core, cold files are disk-bound anyway, and sequential access
 * maximizes OS readahead. Cancellation (QFuture::cancel()) is acknowledged
 * between chunks; progress is reported in permille (0..1000).
 *
 * Deliver completion to the GUI thread with a QFutureWatcher and check
 * isCanceled() before calling result().
 */
QFuture<IndexingResult> buildLineIndex(std::shared_ptr<FileSource> source,
                                       qsizetype chunkSize = kDefaultIndexChunkSize);

/**
 * Incrementally index growth: rescan only [previous->resumeOffset(),
 * source->size()) and return a NEW index (copy-on-extend - @p previous stays
 * immutable for its concurrent readers; a cancelled extend leaves it the
 * only truth). @p source is the follow controller's REOPENED FileSource for
 * the same identity-matched file; a source smaller than the previous index
 * (rotation raced the reopen) returns @p previous unchanged - callers
 * resolve that through FileIdentity. bytesScanned counts only the rescanned
 * suffix.
 */
QFuture<IndexingResult> extendLineIndex(std::shared_ptr<FileSource> source,
                                        std::shared_ptr<const LineIndex> previous,
                                        qsizetype chunkSize = kDefaultIndexChunkSize);

} // namespace logdor
