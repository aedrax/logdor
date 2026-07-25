#include "logdor/FileSource.h"

#include <QPromise>
#include <QtConcurrentRun>
#include <QtEnvironmentVariables>

#ifdef LOGDOR_HAVE_ZLIB
#include <zlib.h>
#endif

#include <cstring>

namespace logdor {

namespace {

constexpr char kGzipMagic[2] = { '\x1f', '\x8b' };

qint64 maxDecompressedBytes()
{
    // Gzip-bomb cap: 4 GiB unless overridden.
    bool ok = false;
    const qint64 mb = qEnvironmentVariableIntValue("LOGDOR_MAX_DECOMPRESSED_MB",
                                                   &ok);
    return (ok && mb > 0 ? mb : qint64(4096)) * 1024 * 1024;
}

#ifdef LOGDOR_HAVE_ZLIB
// Inflate the whole (possibly multi-member, per rotated-log convention)
// gzip stream. Returns false with @p error set on corrupt/truncated input,
// the bomb cap, or cancellation (empty error).
bool inflateAll(QFile& file, QByteArray& out, QString* error,
                QPromise<FileSource::AsyncOpenResult>* promise)
{
    constexpr qsizetype kStep = 4 * 1024 * 1024;
    const qint64 compressedSize = qMax<qint64>(file.size(), 1);
    const qint64 cap = maxDecompressedBytes();

    z_stream stream = {};
    if (inflateInit2(&stream, 15 + 32) != Z_OK) { // gzip or zlib wrapper
        if (error)
            *error = QStringLiteral("zlib initialization failed");
        return false;
    }

    QByteArray inBuffer(kStep, Qt::Uninitialized);
    QByteArray outBuffer(kStep, Qt::Uninitialized);
    bool sawEnd = false;
    for (;;) {
        if (promise && promise->isCanceled()) {
            inflateEnd(&stream);
            if (error)
                error->clear();
            return false;
        }
        if (stream.avail_in == 0) {
            const qint64 got = file.read(inBuffer.data(), inBuffer.size());
            if (got < 0) {
                inflateEnd(&stream);
                if (error)
                    *error = QStringLiteral("read failed: %1")
                                 .arg(file.errorString());
                return false;
            }
            if (got == 0)
                break; // input exhausted
            stream.next_in = reinterpret_cast<Bytef*>(inBuffer.data());
            stream.avail_in = uInt(got);
            if (promise)
                promise->setProgressValue(
                    int(file.pos() * 1000 / compressedSize));
        }

        stream.next_out = reinterpret_cast<Bytef*>(outBuffer.data());
        stream.avail_out = uInt(outBuffer.size());
        const int rc = inflate(&stream, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&stream);
            if (error)
                *error = QStringLiteral("corrupt gzip stream");
            return false;
        }
        out.append(outBuffer.constData(),
                   outBuffer.size() - qsizetype(stream.avail_out));
        if (out.size() > cap) {
            inflateEnd(&stream);
            if (error)
                *error = QStringLiteral(
                    "decompressed size exceeds the %1 MiB cap "
                    "(LOGDOR_MAX_DECOMPRESSED_MB overrides)")
                             .arg(maxDecompressedBytes() / (1024 * 1024));
            return false;
        }
        if (rc == Z_STREAM_END) {
            sawEnd = true;
            if (stream.avail_in == 0 && file.atEnd())
                break;
            // Rotated logs are commonly concatenated members.
            if (inflateReset2(&stream, 15 + 32) != Z_OK)
                break;
            sawEnd = false;
        }
    }
    inflateEnd(&stream);
    if (!sawEnd) {
        if (error)
            *error = QStringLiteral("truncated gzip stream");
        return false;
    }
    return true;
}
#endif // LOGDOR_HAVE_ZLIB

bool startsWithGzipMagic(QFile& file)
{
    char magic[2] = {};
    const bool match = file.read(magic, 2) == 2
        && magic[0] == kGzipMagic[0] && magic[1] == kGzipMagic[1];
    file.seek(0);
    return match;
}

} // namespace

bool FileSource::isCompressedFile(const QString& path)
{
#ifdef LOGDOR_HAVE_ZLIB
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && startsWithGzipMagic(file);
#else
    Q_UNUSED(path)
    return false;
#endif
}

std::shared_ptr<FileSource> FileSource::openImpl(
    const QString& path, QString* error, QPromise<AsyncOpenResult>* promise)
{
    // std::make_shared can't reach the private constructor.
    std::shared_ptr<FileSource> src(new FileSource);
    src->m_file.setFileName(path);
    if (!src->m_file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2")
                         .arg(path, src->m_file.errorString());
        return nullptr;
    }

#ifdef LOGDOR_HAVE_ZLIB
    // Magic bytes win over suffix: a .gz named plainly still inflates, a
    // plain file named .gz opens as-is.
    if (startsWithGzipMagic(src->m_file)) {
        QString inflateError;
        if (!inflateAll(src->m_file, src->m_decompressed, &inflateError,
                        promise)) {
            if (error)
                *error = inflateError.isEmpty()
                    ? QString() // cancelled
                    : QStringLiteral("Cannot decompress %1: %2")
                          .arg(path, inflateError);
            return nullptr;
        }
        src->m_decompressed.squeeze();
        src->m_size = quint64(src->m_decompressed.size());
        src->m_mode = Mode::Decompressed;
        return src;
    }
#else
    Q_UNUSED(promise)
#endif

    src->m_size = quint64(src->m_file.size());

    // QFile::map(0, 0) fails by contract; an empty file is trivially
    // "mapped" - data() hands out a valid empty range.
    if (src->m_size == 0) {
        src->m_mode = Mode::Mapped;
        return src;
    }

    if (!qEnvironmentVariableIsSet("LOGDOR_FORCE_BUFFERED"))
        src->m_map = src->m_file.map(0, qint64(src->m_size));

    src->m_mode = src->m_map ? Mode::Mapped : Mode::Buffered;
    return src;
}

std::shared_ptr<FileSource> FileSource::open(const QString& path, QString* error)
{
    return openImpl(path, error, nullptr);
}

QFuture<FileSource::AsyncOpenResult> FileSource::openAsync(const QString& path)
{
    return QtConcurrent::run([path](QPromise<AsyncOpenResult>& promise) {
        promise.setProgressRange(0, 1000);
        AsyncOpenResult result;
        result.source = openImpl(path, &result.error, &promise);
        if (promise.isCanceled())
            return;
        promise.setProgressValue(1000);
        promise.addResult(std::move(result));
    });
}

const char* FileSource::data() const
{
    if (m_mode == Mode::Decompressed)
        return m_decompressed.constData();
    if (m_size == 0)
        return "";
    if (m_map)
        return reinterpret_cast<const char*>(m_map);
    return nullptr;
}

QByteArrayView FileSource::view(quint64 offset, qsizetype length) const
{
    const char* base = data();
    Q_ASSERT(base != nullptr);
    Q_ASSERT(offset + quint64(length) <= m_size);
    return QByteArrayView(base + offset, length);
}

QByteArray FileSource::read(quint64 offset, qsizetype length) const
{
    if (offset >= m_size)
        return {};
    length = qsizetype(qMin(quint64(length), m_size - offset));

    if (const char* base = data())
        return QByteArray(base + offset, length);

    // Buffered: assemble from cached 4 MiB blocks.
    QByteArray out;
    out.reserve(length);
    QMutexLocker lock(&m_ioMutex);
    while (length > 0) {
        const quint64 blockNo = offset / kBlockSize;
        QByteArray* block = m_blockCache.object(blockNo);
        if (!block) {
            auto loaded = std::make_unique<QByteArray>(
                readUncached(blockNo * kBlockSize, kBlockSize));
            block = loaded.get();
            // insert() takes ownership; cost 1 per block, capacity 32.
            if (!m_blockCache.insert(blockNo, loaded.get()))
                break; // insertion failure only if cost > maxCost; cannot happen
            loaded.release();
        }
        const qsizetype within = qsizetype(offset - blockNo * kBlockSize);
        const qsizetype take = qMin(length, block->size() - within);
        if (take <= 0)
            break;
        out.append(block->constData() + within, take);
        offset += quint64(take);
        length -= take;
    }
    return out;
}

qsizetype FileSource::readInto(quint64 offset, char* dst, qsizetype length) const
{
    if (offset >= m_size)
        return 0;
    length = qsizetype(qMin(quint64(length), m_size - offset));

    if (const char* base = data()) {
        std::memcpy(dst, base + offset, size_t(length));
        return length;
    }

    QMutexLocker lock(&m_ioMutex);
    if (!m_file.seek(qint64(offset)))
        return 0;
    qsizetype done = 0;
    while (done < length) {
        const qint64 n = m_file.read(dst + done, length - done);
        if (n <= 0)
            break;
        done += qsizetype(n);
    }
    return done;
}

QByteArray FileSource::readUncached(quint64 offset, qsizetype length) const
{
    // Caller holds m_ioMutex.
    if (!m_file.seek(qint64(offset)))
        return {};
    return m_file.read(length);
}

} // namespace logdor
