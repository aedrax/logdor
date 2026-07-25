#pragma once

#include "logdor/FileSource.h"
#include "logdor/FormatParser.h"
#include "logdor/LineIndex.h"
#include "logdor/RowSet.h"

#include <QFuture>
#include <QString>

#include <memory>
#include <vector>

namespace logdor {

enum class ExportFormat : quint8 {
    Text, ///< raw line bytes, one line per row
    Csv,  ///< parsed schema fields, RFC 4180 quoting
};

struct ExportRequest {
    ExportFormat format = ExportFormat::Text;
    QString outPath;
    bool csvHeader = true; ///< Csv only: leading schema-name header row
};

struct ExportResult {
    qint64 rowsWritten = 0;
    QString error; ///< empty on success
    qint64 elapsedMs = 0;
};

/**
 * Write the visible rows to a file in VIEW order: @p order (when non-empty)
 * permutes the row set exactly like the model's display permutation, so the
 * export matches what the analyst sees - including a sort. Text mode copies
 * raw line bytes (lengthOf semantics: no terminator, no trailing '\r');
 * Csv parses each line and quotes per RFC 4180. Cancellation is honored
 * between 64k-row batches; a cancelled or failed export removes the
 * partial file.
 */
QFuture<ExportResult> exportRows(std::shared_ptr<FileSource> source,
                                 std::shared_ptr<const LineIndex> index,
                                 std::shared_ptr<const FormatParser> parser,
                                 RowSet rows, std::vector<qint32> order,
                                 ExportRequest request);

} // namespace logdor
