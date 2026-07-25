#pragma once

#include "logdor/FileSource.h"
#include "logdor/FormatParser.h"
#include "logdor/LineIndex.h"
#include "logdor/TimestampParse.h"

#include <memory>

namespace logdor {

struct TimeRangeProbe {
    bool valid = false;     ///< a time column exists and epochs parsed
    bool monotonic = false; ///< uptime clock: values are ms since boot
    qint64 firstMs = 0;     ///< minimum epoch over the head sample
    qint64 lastMs = 0;      ///< maximum epoch over the tail sample
};

/**
 * Cheap synchronous estimate of a file's time span: parse @p sampleLines
 * lines from the head (minimum) and tail (maximum) with the schema's
 * declared codec - detected from the head sample when undeclared, the same
 * resolution rules as extractColumns. Near-ordered logs are bracketed
 * correctly; a pathologically shuffled file may under-report. Bounded work:
 * safe on the GUI thread right after indexing.
 */
TimeRangeProbe probeTimeRange(const FileSource& source, const LineIndex& index,
                              const FormatParser& parser,
                              const TimeParseContext& context = {},
                              qint64 sampleLines = 64);

} // namespace logdor
