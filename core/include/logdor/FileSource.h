#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QCache>
#include <QFile>
#include <QFuture>
#include <QMutex>
#include <QString>

#include <memory>

QT_BEGIN_NAMESPACE
template <typename T> class QPromise;
QT_END_NAMESPACE

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
    /// Decompressed = a gzip stream inflated to the heap at open time; the
    /// source then behaves exactly like a contiguous plain file whose bytes
    /// are the decompressed content (indexing, scans, identity - unchanged).
    enum class Mode { Mapped, Buffered, Decompressed };

    static constexpr qsizetype kBlockSize = 4 * 1024 * 1024;
    static constexpr int kMaxCachedBlocks = 32; // <=128 MiB resident

    /**
     * Open @p path read-only. Returns nullptr (and sets @p error) only when
     * the file itself cannot be opened; a failed mmap falls back to Buffered.
     * Gzip content (magic bytes win over suffix) is inflated whole - bounded
     * by a 4 GiB gzip-bomb cap (LOGDOR_MAX_DECOMPRESSED_MB overrides).
     * Set LOGDOR_FORCE_BUFFERED=1 to skip mapping (used by tests).
     */
    static std::shared_ptr<FileSource> open(const QString& path,
                                            QString* error = nullptr);

    /// Cheap sniff: does the file start with the gzip magic (and is gzip
    /// support compiled in)?
    static bool isCompressedFile(const QString& path);

    struct AsyncOpenResult {
        std::shared_ptr<FileSource> source; // null on failure/cancel
        QString error;
    };

    /**
     * open() off-thread: plain files resolve almost instantly, compressed
     * ones inflate with permille progress over the COMPRESSED bytes and
     * cancellation between 4 MiB steps - the shell's "nothing blocks on
     * file size" contract for .gz. Failure is reported in the result, not
     * by an empty future.
     */
    static QFuture<AsyncOpenResult> openAsync(const QString& path);

    QString filePath() const { return m_file.fileName(); }
    quint64 size() const { return m_size; }
    Mode mode() const { return m_mode; }

    /// True when the whole file is addressable as one contiguous range.
    bool isContiguous() const { return m_mode != Mode::Buffered; }

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
    static std::shared_ptr<FileSource> openImpl(
        const QString& path, QString* error,
        QPromise<AsyncOpenResult>* promise);

    mutable QFile m_file;
    quint64 m_size = 0;
    const uchar* m_map = nullptr;
    QByteArray m_decompressed; // Decompressed mode's whole content
    Mode m_mode = Mode::Mapped;

    mutable QMutex m_ioMutex; // guards m_file and m_blockCache in Buffered mode
    mutable QCache<quint64, QByteArray> m_blockCache { kMaxCachedBlocks };
};

} // namespace logdor
