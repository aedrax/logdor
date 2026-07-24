#pragma once

#include "logdor/Query.h"
#include "logdor/RowSet.h"

#include <QFuture>

#include <memory>
#include <vector>

namespace logdor {

enum class SortKeyKind : quint8 { Integer, Text, Severity };

struct SortResult {
    // order[viewRow] = position within the RowSet (NOT a source line).
    std::vector<qint32> order;
    qint64 elapsedMs = 0;
};

/**
 * Stable ascending sort of the VISIBLE rows by cached column keys -
 * Integer/Text read @p keys, Severity reads @p severity (enum order). Ties
 * keep source order; unparseable Integer rows sort first. Descending is the
 * reversed ascending permutation (caller's choice - tie order reverses too,
 * the standard Qt tradeoff).
 *
 * Runs off-thread; cancellation is honored before and after the sort itself
 * (std::stable_sort cannot be interrupted mid-run; a cancelled result is
 * simply discarded).
 */
QFuture<SortResult> sortRows(RowSet rows, SortKeyKind kind,
                             std::shared_ptr<const ColumnData> keys,
                             std::shared_ptr<const std::vector<quint8>> severity);

} // namespace logdor
