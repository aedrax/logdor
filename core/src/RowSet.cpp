#include "logdor/RowSet.h"

#include <algorithm>

namespace logdor {

RowSet RowSet::appended(const RowSet& head, qint64 spliceLine,
                        std::vector<qint32> tailLines, qint64 newLineCount)
{
    const qint64 headKept = head.m_all
        ? std::min(spliceLine, head.m_lineCount)
        : qint64(std::lower_bound(head.m_rows.begin(), head.m_rows.end(),
                                  qint32(spliceLine))
                 - head.m_rows.begin());

    // The common passthrough growth path: everything visible before and
    // after - no vector is ever materialized.
    if (head.m_all && headKept == spliceLine
        && headKept + qint64(tailLines.size()) == newLineCount)
        return all(newLineCount);

    std::vector<qint32> lines;
    lines.reserve(size_t(headKept) + tailLines.size());
    if (head.m_all) {
        for (qint64 line = 0; line < headKept; ++line)
            lines.push_back(qint32(line));
    } else {
        lines.insert(lines.end(), head.m_rows.begin(),
                     head.m_rows.begin() + headKept);
    }
    lines.insert(lines.end(), tailLines.begin(), tailLines.end());
    return fromLines(std::move(lines), newLineCount);
}

qint64 RowSet::rowForSourceLine(qint64 line) const noexcept
{
    if (m_all)
        return line >= 0 && line < m_lineCount ? line : -1;
    const auto it = std::lower_bound(m_rows.begin(), m_rows.end(), qint32(line));
    if (it == m_rows.end() || *it != qint32(line))
        return -1;
    return it - m_rows.begin();
}

} // namespace logdor
