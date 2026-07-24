#pragma once

#include "logdor/FileSource.h"
#include "logdor/LineIndex.h"
#include "logdor/Query.h"
#include "logdor/RowSet.h"

#include <QFuture>
#include <QString>

#include <functional>
#include <memory>

namespace logdor {

/**
 * Core mirror of the shell's filter bar state (core cannot include app
 * headers). Semantics are legacy-exact: a line is a match when
 * (textMatch XOR invert) - an empty query counts as textMatch=true and
 * ignores invert - AND extraPredicate accepts it. The visible set is the
 * union of [match - contextBefore, match + contextAfter] over all matches.
 */
struct LineFilter {
    QString query;
    bool caseSensitive = false;
    bool regexMode = false;
    bool invert = false;
    int contextBefore = 0;
    int contextAfter = 0;

    /**
     * Optional structured predicate ANDed with the text decision before
     * context expansion (e.g. logcat level/tag chrome). Runs concurrently on
     * worker threads - must be thread-safe. @p raw = lengthOf() bytes.
     */
    std::function<bool(qint64 line, QByteArrayView raw)> extraPredicate;

    /**
     * Compiled field query (query mode). When set it REPLACES the plain
     * query/regexMode text match; `columns` must cover
     * fieldQuery->referencedColumns() and needsSeverity() (see ColumnCache).
     * invert and context expansion compose identically to text mode.
     */
    std::shared_ptr<const CompiledQuery> fieldQuery;
    ColumnSnapshot columns;

    bool isPassthrough() const
    {
        return query.isEmpty() && !fieldQuery && !extraPredicate;
    }
};

struct FilterScanResult {
    RowSet rows;
    qint64 matchCount = 0; // matches before context expansion
    qint64 elapsedMs = 0;
};

constexpr qint64 kDefaultFilterChunkLines = 256 * 1024;

/**
 * Chunk-parallel scan over all lines on worker threads. Same QPromise
 * contract as buildLineIndex: cancellable between super-chunks, permille
 * progress, deliver to the GUI thread with a QFutureWatcher and check
 * isCanceled() before result(). Passthrough filters resolve to RowSet::all()
 * without touching the file.
 */
QFuture<FilterScanResult> scanFilter(std::shared_ptr<FileSource> source,
                                     std::shared_ptr<const LineIndex> index,
                                     LineFilter filter,
                                     qint64 linesPerChunk = kDefaultFilterChunkLines);

} // namespace logdor
