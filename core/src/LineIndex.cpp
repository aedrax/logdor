#include "logdor/LineIndex.h"

#include <limits>

namespace logdor {

void LineIndex::reserveLines(qint64 approx)
{
    if (approx <= 0)
        return;
    // +1: addTerminator provisionally starts a next line that finalize()
    // may pop again; without headroom that one push doubles the capacity.
    if (m_wideMode) {
        m_wide.reserve(size_t(approx) + 1);
    } else {
        m_delta.reserve(size_t(approx) + 1);
        m_blockBase.reserve(size_t((approx >> kBlockShift) + 2));
    }
    m_crlf.reserve(size_t(approx) + 1);
}

void LineIndex::addTerminator(quint64 newlinePos, bool precededByCr)
{
    Q_ASSERT(!m_finalized);
    if (m_count == 0)
        appendStart(0);

    // The terminator belongs to the current last line.
    const size_t terminated = size_t(m_count - 1);
    if (m_crlf.size() <= terminated)
        m_crlf.resize(terminated + 1, false);
    m_crlf[terminated] = precededByCr;

    // Provisionally start the next line; finalize() drops it if the file
    // ends exactly here (a trailing '\n' creates no empty final line).
    appendStart(newlinePos + 1);
}

void LineIndex::finalize(quint64 fileSizeBytes)
{
    Q_ASSERT(!m_finalized);
    m_fileSize = fileSizeBytes;

    if (m_count == 0) {
        // No terminators seen: zero lines for an empty stream, one otherwise.
        if (fileSizeBytes > 0)
            appendStart(0);
        m_lastLineTerminated = false;
    } else if (offsetOf(m_count - 1) == fileSizeBytes) {
        // Provisional line after the final '\n' is phantom; the real last
        // line was terminated.
        popLastStart();
        m_lastLineTerminated = true;
    } else {
        m_lastLineTerminated = false;
    }

    // Immutable from here on: drop growth slack (an unhinted build can carry
    // up to 2x capacity from geometric growth).
    m_crlf.resize(size_t(m_count), false);
    m_crlf.shrink_to_fit();
    m_blockBase.shrink_to_fit();
    m_delta.shrink_to_fit();
    m_wide.shrink_to_fit();
    m_finalized = true;
}

void LineIndex::appendStart(quint64 offset)
{
    if (m_wideMode) {
        m_wide.push_back(offset);
        ++m_count;
        return;
    }
    if ((m_count & kBlockMask) == 0) {
        m_blockBase.push_back(offset);
        m_delta.push_back(0);
        ++m_count;
        return;
    }
    const quint64 delta = offset - m_blockBase.back();
    if (delta > std::numeric_limits<quint32>::max()) {
        migrateToWide();
        m_wide.push_back(offset);
        ++m_count;
        return;
    }
    m_delta.push_back(quint32(delta));
    ++m_count;
}

void LineIndex::popLastStart()
{
    Q_ASSERT(m_count > 0);
    --m_count;
    if (m_wideMode) {
        m_wide.pop_back();
        return;
    }
    m_delta.pop_back();
    if ((m_count & kBlockMask) == 0)
        m_blockBase.pop_back();
}

void LineIndex::migrateToWide()
{
    m_wide.reserve(size_t(m_count) + 1);
    for (qint64 i = 0; i < m_count; ++i)
        m_wide.push_back(m_blockBase[size_t(i >> kBlockShift)]
                         + m_delta[size_t(i)]);
    m_blockBase.clear();
    m_blockBase.shrink_to_fit();
    m_delta.clear();
    m_delta.shrink_to_fit();
    m_wideMode = true;
}

size_t LineIndex::memoryUsage() const noexcept
{
    return m_blockBase.capacity() * sizeof(quint64)
        + m_delta.capacity() * sizeof(quint32)
        + m_wide.capacity() * sizeof(quint64)
        + m_crlf.capacity() / 8;
}

} // namespace logdor
