#pragma once

#include "logdor/FileSource.h"
#include "logdor/FilterScan.h"
#include "logdor/FormatParser.h"
#include "logdor/LineIndex.h"
#include "logdor/Query.h"

#include <QFuture>

#include <memory>

namespace logdor {

struct ColumnScanResult {
    QHash<int, std::shared_ptr<const ColumnData>> columns; // the requested ones
    std::shared_ptr<const std::vector<quint8>> severity;   // when requested
    qint64 elapsedMs = 0;
};

/**
 * One chunk-parallel pass over ALL source lines: parseLine once per line,
 * filling every requested column (and the severity vector) in the same pass.
 * Same QPromise contract as scanFilter: cancellable between super-chunks,
 * permille progress. This is what field queries and column sorting pay once
 * per (file, column); results are immutable and shared.
 *
 * DateTime columns additionally get UTC epoch milliseconds: the codec comes
 * from the field's declared timeFormat, else is detected once from the first
 * parsed sample values (every chunk uses the same codec). @p timeContext
 * supplies the assumed zone and reference date for zone-less/year-less
 * formats; results are only valid for the context they were extracted with.
 *
 * @p firstLine > 0 extracts only [firstLine, lineCount) - follow mode's
 * tail extract, spliced with ColumnData::appended(). Row i of the result
 * corresponds to source line firstLine + i. Codec detection still samples
 * from the FILE START so the tail's codec matches the head column it will
 * be appended to.
 */
QFuture<ColumnScanResult> extractColumns(
    std::shared_ptr<FileSource> source,
    std::shared_ptr<const LineIndex> index,
    std::shared_ptr<const FormatParser> parser,
    QList<int> columns, bool wantSeverity,
    TimeParseContext timeContext = {},
    qint64 linesPerChunk = kDefaultFilterChunkLines,
    qint64 firstLine = 0);

/**
 * Per-widget cache of extracted columns for the CURRENT file. Mutated on the
 * GUI thread only; payloads are immutable shared_ptrs, safe to snapshot into
 * worker threads. Owned by the shell - deliberately not a global.
 */
class ColumnCache {
public:
    void clear()
    {
        m_columns.clear();
        m_severity.reset();
    }

    std::shared_ptr<const ColumnData> column(int col) const
    {
        return m_columns.value(col);
    }

    void insert(int col, std::shared_ptr<const ColumnData> data)
    {
        m_columns.insert(col, std::move(data));
    }

    std::shared_ptr<const std::vector<quint8>> severity() const { return m_severity; }
    void setSeverity(std::shared_ptr<const std::vector<quint8>> severity)
    {
        m_severity = std::move(severity);
    }

    /// Merge an extraction result.
    void insert(const ColumnScanResult& result)
    {
        for (auto it = result.columns.begin(); it != result.columns.end(); ++it)
            m_columns.insert(it.key(), it.value());
        if (result.severity)
            m_severity = result.severity;
    }

    ColumnSnapshot snapshot(const QList<int>& cols, bool needsSeverity) const
    {
        ColumnSnapshot snap;
        for (int col : cols) {
            if (auto data = m_columns.value(col))
                snap.columns.insert(col, std::move(data));
        }
        if (needsSeverity)
            snap.severity = m_severity;
        return snap;
    }

    /// Subset of @p cols not yet cached.
    QList<int> missing(const QList<int>& cols) const
    {
        QList<int> out;
        for (int col : cols) {
            if (!m_columns.contains(col))
                out.append(col);
        }
        return out;
    }

    bool hasSeverity() const { return m_severity != nullptr; }

    size_t memoryUsage() const
    {
        size_t total = m_severity ? m_severity->capacity() : 0;
        for (const auto& data : m_columns)
            total += data->memoryUsage();
        return total;
    }

private:
    QHash<int, std::shared_ptr<const ColumnData>> m_columns;
    std::shared_ptr<const std::vector<quint8>> m_severity;
};

} // namespace logdor
