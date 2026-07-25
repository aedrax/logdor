#pragma once

#include <QtGlobal>

#include <cstdint>
#include <vector>

namespace logdor {

/**
 * Compact index of line boundaries in a byte stream.
 *
 * Pure offset structure: no file I/O, no Qt containers on hot paths. Byte
 * access composes at the FileSource level.
 *
 * Line semantics (legacy-exact, matching the pre-core scanner):
 *  - Lines are separated by '\n' only. A lone '\r' is content, not a separator.
 *  - A line's raw content excludes its terminating '\n' but INCLUDES a
 *    trailing '\r' (rawLengthOf). lengthOf additionally trims the '\r'.
 *  - A terminating '\n' at end-of-file does not create an empty final line.
 *  - A non-empty file with no trailing '\n' still ends in a (final) line.
 *
 * Storage: block-delta encoding. Line starts are grouped in blocks of 1024;
 * each block stores one 64-bit base offset, each line a 32-bit delta from its
 * block base (~4.13 bytes/line). If any delta would overflow 32 bits (a >4 GiB
 * span inside one block), the whole index migrates to a plain 64-bit array
 * ("wide mode") - correctness preserved, memory sacrificed only for
 * pathological inputs.
 *
 * Build single-threaded via addTerminator()+finalize(), then treat as
 * immutable; const access is safe from any thread afterwards.
 */
class LineIndex {
public:
    static constexpr int kBlockShift = 10; // 1024 lines per block
    static constexpr qint64 kBlockMask = (qint64(1) << kBlockShift) - 1;

    //=== Builder API (single writer, then immutable) =========================

    /// Hint the expected number of lines to avoid reallocation churn.
    void reserveLines(qint64 approx);

    /**
     * Record a line terminator: a '\n' at byte position @p newlinePos.
     * @p precededByCr is true when the byte before the '\n' is '\r' (the
     * terminated line ends with "\r\n"). Positions must be strictly
     * increasing across calls.
     */
    void addTerminator(quint64 newlinePos, bool precededByCr);

    /// Complete the index. @p fileSizeBytes is the total stream length.
    void finalize(quint64 fileSizeBytes);

    //=== Query API (valid after finalize) =====================================

    qint64 lineCount() const noexcept { return m_count; }
    quint64 fileSize() const noexcept { return m_fileSize; }
    bool isWide() const noexcept { return m_wideMode; }

    /// Byte offset of the first byte of @p line.
    quint64 offsetOf(qint64 line) const noexcept
    {
        return m_wideMode ? m_wide[size_t(line)]
                          : m_blockBase[size_t(line >> kBlockShift)]
                                + m_delta[size_t(line)];
    }

    /// One past the last byte of @p line including its terminator ('\n').
    quint64 endOffsetOf(qint64 line) const noexcept
    {
        return line + 1 < m_count ? offsetOf(line + 1) : m_fileSize;
    }

    /// Content length excluding '\n' but INCLUDING a trailing '\r' (legacy).
    qsizetype rawLengthOf(qint64 line) const noexcept
    {
        const quint64 start = offsetOf(line);
        if (line + 1 < m_count)
            return qsizetype(offsetOf(line + 1) - start - 1);
        return qsizetype(m_fileSize - start - (m_lastLineTerminated ? 1 : 0));
    }

    /// Content length excluding the "\r\n" or "\n" terminator.
    qsizetype lengthOf(qint64 line) const noexcept
    {
        return rawLengthOf(line) - (endsWithCrLf(line) ? 1 : 0);
    }

    /// True when @p line is terminated by "\r\n".
    bool endsWithCrLf(qint64 line) const noexcept
    {
        return size_t(line) < m_crlf.size() && m_crlf[size_t(line)];
    }

    /// True when the final line ends with '\n' (follow mode: an appended
    /// byte then belongs to a NEW line, not to the final one).
    bool lastLineTerminated() const noexcept { return m_lastLineTerminated; }

    /**
     * Where an incremental rescan resumes: fileSize() when the last line is
     * terminated, else the START of the unterminated last line. Those bytes
     * contain no '\n' by construction, so rescanning them is free of double
     * counting - and it puts any trailing '\r' back in scan range, keeping
     * the CRLF check whole across the old end-of-file.
     */
    quint64 resumeOffset() const noexcept
    {
        return m_count == 0 || m_lastLineTerminated ? m_fileSize
                                                    : offsetOf(m_count - 1);
    }

    /**
     * A copy of @p index reopened for building: finalize() is undone so
     * addTerminator()/finalize() may append what lies past resumeOffset().
     * The source index is untouched - concurrent readers keep their
     * immutable snapshot (copy-on-extend, never in-place growth).
     */
    static LineIndex resumedFrom(const LineIndex& index);

    /// Bytes of heap memory held by the index structure itself.
    size_t memoryUsage() const noexcept;

private:
    void appendStart(quint64 offset);
    void popLastStart();
    void migrateToWide();

    std::vector<quint64> m_blockBase; // one per 1024 lines (narrow mode)
    std::vector<quint32> m_delta;     // one per line (narrow mode)
    std::vector<quint64> m_wide;      // one per line (wide mode)
    std::vector<bool> m_crlf;         // per terminated line: ends with "\r\n"

    qint64 m_count = 0;
    quint64 m_fileSize = 0;
    bool m_wideMode = false;
    bool m_lastLineTerminated = false;
    bool m_finalized = false;
};

} // namespace logdor
