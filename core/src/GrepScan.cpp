#include "logdor/GrepScan.h"

#include "logdor/FileSource.h"
#include "TextMatch_p.h"

#include <QtConcurrentRun>

#include <cstring>

namespace logdor {

using detail::Matcher;

namespace {

constexpr qsizetype kChunkBytes = 16 * 1024 * 1024;

// Walk one file's lines, appending matches. Returns false on cancel.
bool grepFile(const FileSource& source, const Matcher& matcher,
              const GrepQuery& query, GrepFileResult& result,
              const QPromise<GrepFileResult>& promise)
{
    QByteArray buffer;   // scratch for non-contiguous sources
    QByteArray carry;    // unterminated line spanning chunks
    quint64 carryStart = 0;
    qint32 lineNo = 0;
    const quint64 size = source.size();

    const auto consider = [&](QByteArrayView line, quint64 offset) {
        if (result.matches.size() >= query.maxMatchesPerFile) {
            result.truncated = true;
            return false;
        }
        // Strip a trailing '\r' so DOS logs read cleanly.
        if (!line.empty() && line.back() == '\r')
            line.chop(1);
        if (matcher.textMatches(line)) {
            GrepMatch match;
            match.line = lineNo;
            match.offset = offset;
            match.excerpt = QString::fromUtf8(
                line.left(query.maxExcerptBytes));
            result.matches.append(std::move(match));
        }
        ++lineNo;
        return true;
    };

    for (quint64 pos = 0; pos < size;) {
        if (promise.isCanceled())
            return false;
        qsizetype len = qsizetype(qMin(quint64(kChunkBytes), size - pos));
        const char* chunk = nullptr;
        if (source.isContiguous()) {
            chunk = source.data() + pos;
        } else {
            buffer.resize(len);
            len = source.readInto(pos, buffer.data(), len);
            if (len <= 0)
                break;
            chunk = buffer.constData();
        }

        if (pos == 0 && std::memchr(chunk, '\0', size_t(len))) {
            result.skippedBinary = true;
            return true;
        }

        const char* p = chunk;
        const char* const end = chunk + len;
        while (p < end) {
            const char* nl = static_cast<const char*>(
                std::memchr(p, '\n', size_t(end - p)));
            if (!nl) {
                carry.append(p, end - p);
                if (carry.size() == qsizetype(end - p))
                    carryStart = pos + quint64(p - chunk);
                break;
            }
            if (!carry.isEmpty()) {
                carry.append(p, nl - p);
                if (!consider(QByteArrayView(carry), carryStart))
                    return true;
                carry.clear();
            } else if (!consider(
                           QByteArrayView(p, qsizetype(nl - p)),
                           pos + quint64(p - chunk))) {
                return true;
            }
            p = nl + 1;
        }
        pos += quint64(len);
    }
    if (!carry.isEmpty())
        consider(QByteArrayView(carry), carryStart);
    return true;
}

} // namespace

QFuture<GrepFileResult> grepFolder(QStringList files, GrepQuery query)
{
    return QtConcurrent::run([files = std::move(files),
                              query = std::move(query)](
                                 QPromise<GrepFileResult>& promise) {
        if (query.pattern.isEmpty())
            return; // no-op by contract
        const Matcher matcher(query.pattern,
                              query.caseSensitivity == Qt::CaseSensitive,
                              query.regexMode);
        promise.setProgressRange(0, int(files.size()));
        int done = 0;
        for (const QString& path : files) {
            if (promise.isCanceled())
                return;
            GrepFileResult result;
            result.path = path;
            QString error;
            const auto source = FileSource::open(path, &error);
            if (!source) {
                result.error = error;
                promise.addResult(std::move(result));
                continue;
            }
            if (!grepFile(*source, matcher, query, result, promise))
                return; // cancelled mid-file
            if (!result.matches.isEmpty() || result.skippedBinary
                || !result.error.isEmpty())
                promise.addResult(std::move(result));
            promise.setProgressValue(++done);
        }
    });
}

} // namespace logdor
