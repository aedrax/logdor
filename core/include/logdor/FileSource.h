#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QCache>
#include <QFile>
#include <QMutex>
#include <QString>

#include <memory>

namespace logdor {

/**
 * Read-only owner of a log file's bytes.
 *
 * Primary mode memory-maps the whole file (the OS page cache holds the data;
 * nothing is duplicated on the heap). When mapping fails - exotic
 * filesystems, exhausted address space - the source degrades to Buffered
 * mode: pread-style 4 MiB block reads behind a small LRU cache, instead of
 * refusing to open the file.
 *
 * Held by std::shared_ptr so a background consumer (e.g. a cancelled index
 * build) can keep the mapping alive after the owner has moved on to another
 * file; views into the map stay valid for the shared_ptr's lifetime.
 *
 * Thread safety: all const accessors are safe from any thread. Buffered-mode
 * reads serialize on an internal mutex.
 */
class FileSource {
public:
    enum class Mode { Mapped, Buffered };

    static constexpr qsizetype kBlockSize = 4 * 1024 * 1024;
    static constexpr int kMaxCachedBlocks = 32; // <=128 MiB resident

    /**
     * Open @p path read-only. Returns nullptr (and sets @p error) only when
     * the file itself cannot be opened; a failed mmap falls back to Buffered.
     * Set LOGDOR_FORCE_BUFFERED=1 to skip mapping (used by tests).
     */
    static std::shared_ptr<FileSource> open(const QString& path,
                                            QString* error = nullptr);

    QString filePath() const { return m_file.fileName(); }
    quint64 size() const { return m_size; }
    Mode mode() const { return m_mode; }

    /// True when the whole file is addressable as one contiguous range.
    bool isContiguous() const { return m_mode == Mode::Mapped; }

    /// Base pointer of the contiguous range; nullptr unless isContiguous().
    const char* data() const;

    /// Zero-copy view into the contiguous range. Contiguous mode only.
    QByteArrayView view(quint64 offset, qsizetype length) const;

    /// Copy out a range. Works in both modes; clamps to the file end.
    QByteArray read(quint64 offset, qsizetype length) const;

    /**
     * Copy a range into @p dst without touching the block cache - for large
     * sequential scans (the indexer) that would otherwise evict it.
     * Returns the number of bytes copied (clamped to the file end).
     */
    qsizetype readInto(quint64 offset, char* dst, qsizetype length) const;

    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

private:
    FileSource() = default;

    QByteArray readUncached(quint64 offset, qsizetype length) const;

    mutable QFile m_file;
    quint64 m_size = 0;
    const uchar* m_map = nullptr;
    Mode m_mode = Mode::Mapped;

    mutable QMutex m_ioMutex; // guards m_file and m_blockCache in Buffered mode
    mutable QCache<quint64, QByteArray> m_blockCache { kMaxCachedBlocks };
};

} // namespace logdor
