#pragma once

#include <QFuture>
#include <QList>
#include <QString>
#include <QStringList>

namespace logdor {

struct GrepQuery {
    QString pattern;
    bool regexMode = false;
    Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
    int maxMatchesPerFile = 1000;
    qsizetype maxExcerptBytes = 400;
};

struct GrepMatch {
    qint32 line = 0;    ///< 0-based line number within the file
    quint64 offset = 0; ///< byte offset of the line start
    QString excerpt;    ///< the matched line, trimmed to maxExcerptBytes
};

struct GrepFileResult {
    QString path;
    QList<GrepMatch> matches;
    bool truncated = false;     ///< hit maxMatchesPerFile
    bool skippedBinary = false; ///< NUL byte in the first chunk
    QString error;              ///< unreadable file
};

/**
 * Folder-wide search: a dedicated streaming grep - no per-file LineIndex,
 * just a sequential chunked line walk per file (gzip works through
 * FileSource::open). Files are searched in order and each REPORTABLE file
 * (matches, error, or binary skip; clean no-match files stay silent)
 * arrives as its own future result - consume with resultReadyAt to stream
 * into the UI. An empty pattern is a no-op. Cancellation is honored
 * between 16 MiB chunks.
 */
QFuture<GrepFileResult> grepFolder(QStringList files, GrepQuery query);

} // namespace logdor
