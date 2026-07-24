#include "logdor/FormatRegistry.h"

#include "logdor/ClfParser.h"
#include "logdor/FileSource.h"
#include "logdor/JsonLinesParser.h"
#include "logdor/LineIndex.h"
#include "logdor/LogcatParser.h"
#include "logdor/PlainTextParser.h"

#include <algorithm>

namespace logdor {

QList<std::shared_ptr<const FormatParser>> builtinParsers()
{
    return {
        std::make_shared<const ClfParser>(),
        std::make_shared<const LogcatParser>(),
        std::make_shared<const JsonLinesParser>(),
        std::make_shared<const PlainTextParser>(),
    };
}

std::shared_ptr<const FormatParser> parserById(QStringView id)
{
    return parserById(id, builtinParsers());
}

std::shared_ptr<const FormatParser> parserById(
    QStringView id, const QList<std::shared_ptr<const FormatParser>>& parsers)
{
    for (const auto& parser : parsers) {
        if (parser->id() == id)
            return parser;
    }
    return nullptr;
}

QList<FormatScore> detectFormat(const FileSource& source, const LineIndex& index,
                                int maxSampleLines)
{
    return detectFormat(source, index, builtinParsers(), maxSampleLines);
}

QList<FormatScore> detectFormat(const FileSource& source, const LineIndex& index,
                                const QList<std::shared_ptr<const FormatParser>>& parsers,
                                int maxSampleLines)
{
    constexpr quint64 kSampleByteCap = 1024 * 1024;
    QList<qint64> sampleLines;
    sampleLines.reserve(maxSampleLines);
    for (qint64 line = 0; line < index.lineCount()
         && sampleLines.size() < maxSampleLines
         && index.offsetOf(line) < kSampleByteCap;
         ++line) {
        if (index.lengthOf(line) > 0)
            sampleLines.append(line);
    }

    QList<FormatScore> scores;
    scores.reserve(parsers.size());
    for (const auto& parser : parsers) {
        qint64 matched = 0;
        for (qint64 line : sampleLines) {
            const QByteArray raw = source.read(index.offsetOf(line),
                                               index.lengthOf(line));
            if (parser->matchesStructure(QByteArrayView(raw)))
                ++matched;
        }
        const double fraction = sampleLines.isEmpty()
            ? (parser->id() == u"plaintext" ? 1.0 : 0.0)
            : double(matched) / double(sampleLines.size());
        scores.append({ parser->id(), fraction * parser->specificity() });
    }

    std::stable_sort(scores.begin(), scores.end(),
                     [](const FormatScore& a, const FormatScore& b) {
                         return a.score > b.score;
                     });
    return scores;
}

} // namespace logdor
