#include "logdor/LineIndexer.h"

#include <QElapsedTimer>
#include <QtConcurrentRun>

#include <cstring>

namespace logdor {

QFuture<IndexingResult> buildLineIndex(std::shared_ptr<FileSource> source,
                                       qsizetype chunkSize)
{
    Q_ASSERT(source);
    Q_ASSERT(chunkSize > 0);

    return QtConcurrent::run([source, chunkSize](QPromise<IndexingResult>& promise) {
        QElapsedTimer timer;
        timer.start();
        promise.setProgressRange(0, 1000);

        const quint64 size = source->size();
        auto index = std::make_shared<LineIndex>();
        // Rough reserve; logs average well above 64 B/line, so this
        // over-reserves mildly and finalize() trims the slack.
        index->reserveLines(qint64(size / 64) + 1);

        QByteArray buffer; // scratch for non-contiguous sources only
        char lastByteOfPrevChunk = '\0';
        quint64 pos = 0;

        while (pos < size) {
            if (promise.isCanceled())
                return;

            qsizetype len = qsizetype(qMin(quint64(chunkSize), size - pos));
            const char* chunk = nullptr;
            if (source->isContiguous()) {
                chunk = source->data() + pos;
            } else {
                if (buffer.size() < len)
                    buffer.resize(chunkSize);
                const qsizetype got = source->readInto(pos, buffer.data(), len);
                if (got <= 0)
                    break; // truncated underneath us; index what we have
                len = got;
                chunk = buffer.constData();
            }

            const char* p = chunk;
            const char* const end = chunk + len;
            while ((p = static_cast<const char*>(
                        std::memchr(p, '\n', size_t(end - p))))) {
                const bool precededByCr =
                    p > chunk ? p[-1] == '\r' : lastByteOfPrevChunk == '\r';
                index->addTerminator(pos + quint64(p - chunk), precededByCr);
                ++p;
            }

            lastByteOfPrevChunk = chunk[len - 1];
            pos += quint64(len);
            promise.setProgressValue(int(pos * 1000 / size));
        }

        index->finalize(pos);

        IndexingResult result;
        result.lineCount = index->lineCount();
        result.bytesScanned = pos;
        result.elapsedMs = timer.elapsed();
        result.index = std::move(index);
        promise.setProgressValue(1000);
        promise.addResult(std::move(result));
    });
}

} // namespace logdor
