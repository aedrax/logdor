#include "logdor/FileSource.h"

#include <QtEnvironmentVariables>

#include <cstring>

namespace logdor {

std::shared_ptr<FileSource> FileSource::open(const QString& path, QString* error)
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
    src->m_size = quint64(src->m_file.size());

    // QFile::map(0, 0) fails by contract; an empty file is trivially
    // "mapped" — data() hands out a valid empty range.
    if (src->m_size == 0) {
        src->m_mode = Mode::Mapped;
        return src;
    }

    if (!qEnvironmentVariableIsSet("LOGDOR_FORCE_BUFFERED"))
        src->m_map = src->m_file.map(0, qint64(src->m_size));

    src->m_mode = src->m_map ? Mode::Mapped : Mode::Buffered;
    return src;
}

const char* FileSource::data() const
{
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
