#pragma once

#include "logdor/FileIdentity.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <optional>

namespace logdor {

/**
 * A user note on a line or contiguous range of lines.
 *
 * Anchoring: `anchorHash` is the SHA-256 (hex) of the first min(len, 256)
 * bytes of the START line's content (LineIndex::lengthOf semantics — no
 * terminator, no trailing CR, so anchors survive CRLF<->LF conversion).
 * `snippet` is a lossy UTF-8 preview for display only, never matching.
 * `orphaned` is derived at load/re-anchor time and never serialized.
 */
struct Annotation {
    QUuid id;
    qint64 startLine = -1; // 0-based, inclusive
    qint64 endLine = -1;   // == startLine for single-line notes
    QString note;
    QString color;  // "#RRGGBB" or empty (core stays GUI-free; shell converts)
    QString tag;
    QString author;
    QDateTime createdAt;  // UTC
    QDateTime modifiedAt; // UTC
    QByteArray anchorHash;
    QString snippet;
    bool orphaned = false;

    bool coversLine(qint64 line) const
    {
        return line >= startLine && line <= endLine;
    }

    bool operator==(const Annotation& other) const;
};

/// Value container, kept sorted by (startLine, id). Single-threaded use.
class AnnotationSet {
public:
    /// Insert or replace by id. Requires a non-null id. Marks dirty.
    bool upsert(Annotation annotation);
    bool remove(const QUuid& id);
    const Annotation* find(const QUuid& id) const;

    const QList<Annotation>& annotations() const { return m_annotations; }
    QList<Annotation> annotationsAtLine(qint64 line) const;
    bool hasAnnotationAtLine(qint64 line) const; // indicator hot path

    qsizetype size() const { return m_annotations.size(); }
    bool isEmpty() const { return m_annotations.isEmpty(); }

    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }
    void markDirty() { m_dirty = true; }

private:
    QList<Annotation> m_annotations; // sorted by (startLine, id)
    bool m_dirty = false;
};

struct AnnotationFileError {
    QString message;
};

struct AnnotationFile {
    FileIdentity identity;
    AnnotationSet set;
    QStringList warnings; // per-annotation degradation reports
};

/**
 * Parse a sidecar document. Returns nullopt only for unreadable JSON or an
 * unknown "version"; malformed individual annotations are skipped into
 * warnings so one bad entry never loses the rest.
 */
std::optional<AnnotationFile> loadAnnotations(const QByteArray& json,
                                              AnnotationFileError* error = nullptr);

/// Byte-stable for a given set (sorted entries, sorted JSON keys).
QByteArray saveAnnotations(const AnnotationSet& set, const FileIdentity& identity,
                           const QString& fileName = QString());

/**
 * Union by id; conflicts resolved last-write-wins by modifiedAt, ties by the
 * lexicographically larger id (deterministic). Pure — also serves
 * "import a colleague's sidecar". v1 has no delete tombstones: merging an
 * older sidecar can resurrect deleted notes (documented limitation).
 */
AnnotationSet mergeAnnotations(const AnnotationSet& a, const AnnotationSet& b);

} // namespace logdor
