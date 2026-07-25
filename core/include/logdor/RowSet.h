#pragma once

#include <QtGlobal>

#include <vector>

namespace logdor {

/**
 * Sorted set of visible source lines, mapping model rows to source lines.
 *
 * Two representations: "all" (the identity mapping - O(1) memory, the empty
 * filter on a 10M-line file allocates nothing) and "explicit" (a sorted
 * vector of line numbers). fromLines() collapses to "all" when every line is
 * present. Line numbers are qint32 to match the existing selection-event
 * payloads; files beyond 2^31 lines were already unsupported by the shell.
 */
class RowSet {
public:
    RowSet() = default; // empty: size() == 0

    static RowSet all(qint64 lineCount)
    {
        RowSet set;
        set.m_lineCount = lineCount;
        set.m_all = true;
        return set;
    }

    /// @p sortedLines must be ascending and unique; collapses to all() when complete.
    static RowSet fromLines(std::vector<qint32> sortedLines, qint64 lineCount)
    {
        if (qint64(sortedLines.size()) == lineCount)
            return all(lineCount);
        RowSet set;
        set.m_rows = std::move(sortedLines);
        set.m_rows.shrink_to_fit();
        set.m_lineCount = lineCount;
        return set;
    }

    /**
     * Follow-mode splice: @p head's rows below @p spliceLine plus
     * @p tailLines (ascending, all >= spliceLine) over @p newLineCount total
     * lines. Collapses to all() when the result covers every line, so a
     * passthrough view stays allocation-free while it grows.
     */
    static RowSet appended(const RowSet& head, qint64 spliceLine,
                           std::vector<qint32> tailLines, qint64 newLineCount);

    bool isAll() const noexcept { return m_all; }
    qint64 size() const noexcept { return m_all ? m_lineCount : qint64(m_rows.size()); }
    qint64 lineCount() const noexcept { return m_lineCount; }

    qint64 sourceLine(qint64 row) const noexcept
    {
        return m_all ? row : qint64(m_rows[size_t(row)]);
    }

    /// -1 when the line is hidden.
    qint64 rowForSourceLine(qint64 line) const noexcept;

    size_t memoryUsage() const noexcept
    {
        return m_rows.capacity() * sizeof(qint32);
    }

private:
    std::vector<qint32> m_rows; // explicit mode only
    qint64 m_lineCount = 0;
    bool m_all = false;
};

} // namespace logdor
