#include "logdor/RowSet.h"

#include <algorithm>

namespace logdor {

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
