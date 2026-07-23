#include "logdor/FilterScan.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QThread>
#include <QtConcurrentMap>
#include <QtConcurrentRun>

#include <algorithm>
#include <cstring>

namespace logdor {

namespace {

bool isAsciiOnly(const QString& s)
{
    return std::all_of(s.begin(), s.end(),
                       [](QChar c) { return c.unicode() < 0x80; });
}

// Substring search with a memchr first-byte skip. @p needle must be
// pre-lowercased when @p folded; ASCII-only case folding (matches
// QString::contains for ASCII needles under Qt's simple folding).
bool containsAscii(QByteArrayView hay, QByteArrayView needle, bool folded)
{
    const qsizetype n = needle.size();
    if (n == 0)
        return true;
    if (hay.size() < n)
        return false;

    const char first = needle[0];
    const char firstUp = folded && first >= 'a' && first <= 'z'
        ? char(first - 32) : first;
    const char* p = hay.data();
    const char* const end = hay.data() + hay.size() - n + 1;

    while (p < end) {
        const char* c1 = static_cast<const char*>(std::memchr(p, first, size_t(end - p)));
        const char* c = c1;
        if (firstUp != first) {
            const char* c2 = static_cast<const char*>(
                std::memchr(p, firstUp, size_t(end - p)));
            if (!c || (c2 && c2 < c))
                c = c2;
        }
        if (!c)
            return false;
        bool ok = true;
        for (qsizetype i = 1; i < n; ++i) {
            char h = c[i];
            if (folded && h >= 'A' && h <= 'Z')
                h = char(h + 32);
            if (h != needle[i]) {
                ok = false;
                break;
            }
        }
        if (ok)
            return true;
        p = c + 1;
    }
    return false;
}

// Compiled once per scan; match paths are thread-safe (QRegularExpression::
// match is documented thread-safe on a shared const instance).
struct Matcher {
    enum class Mode { Empty, AsciiExact, AsciiFolded, Utf16, Regex };
    Mode mode = Mode::Empty;
    QByteArray asciiNeedle; // pre-lowercased in AsciiFolded mode
    QString utf16Needle;
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    QRegularExpression regex;

    explicit Matcher(const LineFilter& filter)
    {
        cs = filter.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (filter.query.isEmpty())
            return;
        if (filter.regexMode) {
            mode = Mode::Regex;
            regex = QRegularExpression(filter.query,
                filter.caseSensitive ? QRegularExpression::NoPatternOption
                                     : QRegularExpression::CaseInsensitiveOption);
            // Invalid pattern: never matches (legacy behavior).
        } else if (isAsciiOnly(filter.query)) {
            mode = filter.caseSensitive ? Mode::AsciiExact : Mode::AsciiFolded;
            asciiNeedle = filter.query.toUtf8();
            if (mode == Mode::AsciiFolded)
                asciiNeedle = asciiNeedle.toLower();
        } else {
            mode = Mode::Utf16;
            utf16Needle = filter.query;
        }
    }

    bool textMatches(QByteArrayView raw) const
    {
        switch (mode) {
        case Mode::Empty:
            return true;
        case Mode::AsciiExact:
            return containsAscii(raw, asciiNeedle, false);
        case Mode::AsciiFolded:
            return containsAscii(raw, asciiNeedle, true);
        case Mode::Utf16:
            return QString::fromUtf8(raw).contains(utf16Needle, cs);
        case Mode::Regex:
            return regex.isValid()
                && regex.match(QString::fromUtf8(raw)).hasMatch();
        }
        return false;
    }
};

std::vector<qint32> scanRange(const FileSource& source, const LineIndex& index,
                              const Matcher& matcher, const LineFilter& filter,
                              qint64 first, qint64 end)
{
    std::vector<qint32> matches;
    const bool queryEmpty = matcher.mode == Matcher::Mode::Empty;

    // Buffered sources: one bulk read per chunk instead of a lock per line.
    QByteArray scratch;
    const char* base = nullptr;
    quint64 baseOffset = 0;
    if (source.isContiguous()) {
        base = source.data();
    } else {
        baseOffset = index.offsetOf(first);
        const quint64 endOffset = end < index.lineCount()
            ? index.offsetOf(end) : index.fileSize();
        scratch.resize(qsizetype(endOffset - baseOffset));
        source.readInto(baseOffset, scratch.data(), scratch.size());
        base = scratch.constData();
    }

    for (qint64 line = first; line < end; ++line) {
        const QByteArrayView raw(base + (index.offsetOf(line) - baseOffset),
                                 index.lengthOf(line));
        // Empty query ignores invert (legacy); with a query, XOR applies.
        bool match = queryEmpty || (matcher.textMatches(raw) != filter.invert);
        if (match && filter.extraPredicate)
            match = filter.extraPredicate(line, raw);
        if (match)
            matches.push_back(qint32(line));
    }
    return matches;
}

// Union of [m - before, m + after] over sorted matches, clamped to
// [0, total). Linear interval merge; no per-line hashing.
std::vector<qint32> expandContext(const std::vector<qint32>& matches,
                                  int before, int after, qint64 total)
{
    if (before == 0 && after == 0)
        return matches;

    std::vector<qint32> visible;
    visible.reserve(matches.size());
    qint64 nextUnemitted = 0;
    for (qint32 m : matches) {
        const qint64 lo = std::max<qint64>(qint64(m) - before, nextUnemitted);
        const qint64 hi = std::min<qint64>(qint64(m) + after, total - 1);
        for (qint64 line = lo; line <= hi; ++line)
            visible.push_back(qint32(line));
        nextUnemitted = std::max(nextUnemitted, hi + 1);
    }
    return visible;
}

} // namespace

QFuture<FilterScanResult> scanFilter(std::shared_ptr<FileSource> source,
                                     std::shared_ptr<const LineIndex> index,
                                     LineFilter filter, qint64 linesPerChunk)
{
    Q_ASSERT(source && index);
    Q_ASSERT(linesPerChunk > 0);

    return QtConcurrent::run([source, index, filter = std::move(filter),
                              linesPerChunk](QPromise<FilterScanResult>& promise) {
        QElapsedTimer timer;
        timer.start();
        promise.setProgressRange(0, 1000);

        const qint64 total = index->lineCount();
        FilterScanResult result;

        if (filter.isPassthrough() || total == 0) {
            result.rows = RowSet::all(total);
            result.matchCount = total;
            result.elapsedMs = timer.elapsed();
            promise.setProgressValue(1000);
            promise.addResult(std::move(result));
            return;
        }

        const Matcher matcher(filter);
        const int threads = qMax(1, QThread::idealThreadCount());
        const qint64 superChunk = linesPerChunk * threads;
        std::vector<qint32> matches;

        struct Range { qint64 first, end; };
        for (qint64 base = 0; base < total; base += superChunk) {
            if (promise.isCanceled())
                return;
            const qint64 superEnd = std::min(total, base + superChunk);
            QList<Range> ranges;
            for (qint64 s = base; s < superEnd; s += linesPerChunk)
                ranges.append({ s, std::min(superEnd, s + linesPerChunk) });

            // blockingMapped also executes on this (calling) thread, so
            // running it from inside a pool thread cannot deadlock the pool.
            const auto chunkMatches = QtConcurrent::blockingMapped(ranges,
                std::function<std::vector<qint32>(const Range&)>(
                    [&](const Range& r) {
                        return scanRange(*source, *index, matcher, filter,
                                         r.first, r.end);
                    }));
            for (const auto& v : chunkMatches)
                matches.insert(matches.end(), v.begin(), v.end());
            promise.setProgressValue(int(superEnd * 1000 / total));
        }

        result.matchCount = qint64(matches.size());
        result.rows = RowSet::fromLines(
            expandContext(matches, filter.contextBefore, filter.contextAfter, total),
            total);
        result.elapsedMs = timer.elapsed();
        promise.setProgressValue(1000);
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
