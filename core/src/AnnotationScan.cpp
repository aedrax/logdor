#include "logdor/AnnotationScan.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QtConcurrentRun>

#include <algorithm>

namespace logdor {

namespace {

constexpr qsizetype kAnchorBytes = 256;
constexpr int kSnippetChars = 80;

QByteArray hashLine(const FileSource& source, const LineIndex& index, qint64 line)
{
    const qsizetype length = qMin<qsizetype>(index.lengthOf(line), kAnchorBytes);
    const QByteArray bytes = source.read(index.offsetOf(line), length);
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

bool anchorMatches(const FileSource& source, const LineIndex& index,
                   qint64 line, const QByteArray& anchorHash)
{
    return line >= 0 && line < index.lineCount()
        && hashLine(source, index, line) == anchorHash;
}

// Bounded search around the expected line: alternate outward (nearest match
// to the expectation wins) until the line window or byte budget is spent.
qint64 searchWindow(const FileSource& source, const LineIndex& index,
                    qint64 expected, const QByteArray& anchorHash,
                    qint64 windowLines, const QPromise<ReanchorResult>& promise)
{
    qint64 bytesScanned = 0;
    for (qint64 distance = 0; distance <= windowLines; ++distance) {
        if ((distance & 1023) == 0 && promise.isCanceled())
            return -1;
        for (const qint64 candidate : { expected - distance, expected + distance }) {
            if (candidate < 0 || candidate >= index.lineCount())
                continue;
            bytesScanned += qMin<qsizetype>(index.lengthOf(candidate), kAnchorBytes);
            if (anchorMatches(source, index, candidate, anchorHash))
                return candidate;
            if (candidate == expected)
                break; // don't test the expected line twice at distance 0
        }
        if (bytesScanned > kReanchorWindowByteCap)
            return -1;
    }
    return -1;
}

} // namespace

LineAnchor makeAnchor(const FileSource& source, const LineIndex& index,
                      qint64 line)
{
    LineAnchor anchor;
    anchor.anchorHash = hashLine(source, index, line);
    const qsizetype previewLength = qMin<qsizetype>(index.lengthOf(line), 512);
    anchor.snippet = QString::fromUtf8(
                         source.read(index.offsetOf(line), previewLength))
                         .left(kSnippetChars);
    return anchor;
}

QFuture<ReanchorResult> reanchorAnnotations(
    AnnotationSet set, std::shared_ptr<FileSource> source,
    std::shared_ptr<const LineIndex> index, qint64 windowLines)
{
    Q_ASSERT(source && index);

    return QtConcurrent::run([set = std::move(set), source, index,
                              windowLines](QPromise<ReanchorResult>& promise) {
        QElapsedTimer timer;
        timer.start();

        ReanchorResult result;
        bool haveDelta = false;
        qint64 lastDelta = 0;

        for (Annotation annotation : set.annotations()) {
            if (promise.isCanceled())
                return;

            const qint64 span = annotation.endLine - annotation.startLine;
            qint64 found = -1;

            if (anchorMatches(*source, *index, annotation.startLine,
                              annotation.anchorHash)) {
                found = annotation.startLine;
                ++result.verified;
            } else {
                // Rotation shifts every anchor by the same delta; try the
                // last known shift first, then the bounded window.
                if (haveDelta
                    && anchorMatches(*source, *index,
                                     annotation.startLine + lastDelta,
                                     annotation.anchorHash)) {
                    found = annotation.startLine + lastDelta;
                } else {
                    found = searchWindow(*source, *index, annotation.startLine,
                                         annotation.anchorHash, windowLines,
                                         promise);
                    if (promise.isCanceled())
                        return;
                }
                if (found >= 0) {
                    ++result.reanchored;
                    lastDelta = found - annotation.startLine;
                    haveDelta = true;
                }
            }

            if (found >= 0) {
                annotation.startLine = found;
                annotation.endLine =
                    qMin(found + span, index->lineCount() - 1);
                annotation.orphaned = false;
            } else {
                annotation.orphaned = true;
                ++result.orphaned;
            }
            result.set.upsert(std::move(annotation));
        }

        result.set.clearDirty(); // re-anchoring isn't a user edit
        result.elapsedMs = timer.elapsed();
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
