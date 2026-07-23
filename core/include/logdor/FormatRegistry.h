#pragma once

#include "logdor/FormatParser.h"

#include <memory>

namespace logdor {

class FileSource;
class LineIndex;

// HARD RULE: no mutable globals here. logdor-core is a static library linked
// into the app AND into plugins; a singleton registry would exist once per
// module with divergent state. Parsers are stateless, so fresh instances per
// call are correct and cheap; cross-module identity is the QString id().

/// All built-in parsers. Order is the detection tiebreak order.
QList<std::shared_ptr<const FormatParser>> builtinParsers();

/// nullptr when unknown.
std::shared_ptr<const FormatParser> parserById(QStringView id);

/// Same, over an explicit parser list (builtins + declarative formats).
std::shared_ptr<const FormatParser> parserById(
    QStringView id, const QList<std::shared_ptr<const FormatParser>>& parsers);

struct FormatScore {
    QString parserId;
    double score = 0.0;
};

/**
 * Score builtinParsers() against up to @p maxSampleLines non-empty lines
 * drawn from the first 1 MiB of the file. score = matchedFraction *
 * specificity; sorted descending, so front() is the recommendation. The
 * plaintext floor guarantees a non-empty result for non-empty files.
 * Synchronous and cheap (bounded sample).
 */
QList<FormatScore> detectFormat(const FileSource& source, const LineIndex& index,
                                int maxSampleLines = 200);

/// Same, over an explicit parser list (builtins + declarative formats).
QList<FormatScore> detectFormat(const FileSource& source, const LineIndex& index,
                                const QList<std::shared_ptr<const FormatParser>>& parsers,
                                int maxSampleLines = 200);

} // namespace logdor
