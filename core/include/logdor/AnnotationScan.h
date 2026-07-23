#pragma once

#include "logdor/Annotation.h"
#include "logdor/FileSource.h"
#include "logdor/LineIndex.h"

#include <QFuture>

#include <memory>

namespace logdor {

struct LineAnchor {
    QByteArray anchorHash; // hex SHA-256 of the first min(len, 256) bytes
    QString snippet;       // lossy UTF-8, <= 80 chars, display only
};

/// Anchor for @p line, using lengthOf semantics (no terminator, no CR).
LineAnchor makeAnchor(const FileSource& source, const LineIndex& index,
                      qint64 line);

struct ReanchorResult {
    AnnotationSet set; // startLine/endLine updated; orphaned flags derived
    int verified = 0;   // anchor still valid at the saved line
    int reanchored = 0; // found at a different line
    int orphaned = 0;   // anchor line not found; annotation kept, flagged
    qint64 elapsedMs = 0;
};

constexpr qint64 kDefaultReanchorWindowLines = 100'000;
constexpr qint64 kReanchorWindowByteCap = 32 * 1024 * 1024;

/**
 * Verify or relocate every annotation's anchor against the current file.
 *
 * Per annotation: (1) O(1) hash check at the saved line; (2) if a previous
 * annotation re-anchored with line delta D, try savedLine+D first (log
 * rotation shifts all anchors uniformly); (3) bounded scan of up to
 * @p windowLines lines around the expected position, capped at 32 MiB of
 * bytes, nearest match to the expected line wins; (4) no match => orphaned
 * (never dropped). Ranges keep their span, clamped to the file end.
 *
 * Off-thread, cancellable between annotations; same QFutureWatcher contract
 * as the other scans.
 */
QFuture<ReanchorResult> reanchorAnnotations(
    AnnotationSet set, std::shared_ptr<FileSource> source,
    std::shared_ptr<const LineIndex> index,
    qint64 windowLines = kDefaultReanchorWindowLines);

} // namespace logdor
