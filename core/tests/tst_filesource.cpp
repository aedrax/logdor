#include <logdor/FileSource.h>

#include <QTemporaryDir>
#include <QTest>

#ifdef LOGDOR_HAVE_ZLIB
#include <zlib.h>
#endif

using logdor::FileSource;

namespace {

// Patterned content: byte at offset i is a deterministic function of i, so
// any read can be verified without keeping a reference copy around.
QByteArray patternedContent(qsizetype size)
{
    QByteArray data(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        data[i] = char('A' + (i * 31 + i / 253) % 53);
    return data;
}

QString writeFile(const QTemporaryDir& dir, const QString& name,
                  const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size())
        return {};
    return path;
}

#ifdef LOGDOR_HAVE_ZLIB
// A real gzip container (deflate with the gzip wrapper), one member.
QByteArray gzipped(const QByteArray& payload)
{
    z_stream stream = {};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    QByteArray out(payload.size() + 128, Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef*>(
        const_cast<char*>(payload.constData()));
    stream.avail_in = uInt(payload.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = uInt(out.size());
    const int rc = deflate(&stream, Z_FINISH);
    out.resize(out.size() - qsizetype(stream.avail_out));
    deflateEnd(&stream);
    return rc == Z_STREAM_END ? out : QByteArray();
}
#endif

} // namespace

class tst_FileSource : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        qunsetenv("LOGDOR_FORCE_BUFFERED");
        qunsetenv("LOGDOR_MAX_DECOMPRESSED_MB");
    }

#ifdef LOGDOR_HAVE_ZLIB
    void gzipRoundtripsToPlainBytes()
    {
        QTemporaryDir dir;
        const QByteArray payload = patternedContent(200'000);
        const QString path = writeFile(dir, "log.gz", gzipped(payload));
        QVERIFY(FileSource::isCompressedFile(path));

        auto src = FileSource::open(path);
        QVERIFY(src);
        QCOMPARE(src->mode(), FileSource::Mode::Decompressed);
        QVERIFY(src->isContiguous());
        QCOMPARE(src->size(), quint64(payload.size()));
        QCOMPARE(src->read(0, payload.size()), payload);
        QCOMPARE(src->view(1000, 64).toByteArray(), payload.mid(1000, 64));
        QByteArray into(64, Qt::Uninitialized);
        QCOMPARE(src->readInto(500, into.data(), 64), qsizetype(64));
        QCOMPARE(into, payload.mid(500, 64));
        // Reads past the end clamp exactly like a plain source.
        QCOMPARE(src->read(quint64(payload.size()) - 5, 100).size(),
                 qsizetype(5));
    }

    void gzipMultiMemberConcatenates()
    {
        // Rotated-log convention: `cat a.gz b.gz` is a valid gzip file.
        QTemporaryDir dir;
        const QString path = writeFile(
            dir, "multi.gz", gzipped("first half\n") + gzipped("second half\n"));
        auto src = FileSource::open(path);
        QVERIFY(src);
        QCOMPARE(src->read(0, 64), QByteArray("first half\nsecond half\n"));
    }

    void gzipTruncatedAndCorruptFail()
    {
        QTemporaryDir dir;
        const QByteArray whole = gzipped(patternedContent(100'000));

        QString error;
        QVERIFY(!FileSource::open(
            writeFile(dir, "trunc.gz", whole.left(whole.size() / 2)), &error));
        QVERIFY(error.contains(QStringLiteral("truncated"))
                || error.contains(QStringLiteral("corrupt")));

        QByteArray corrupt = whole;
        for (qsizetype i = 40; i < 200 && i < corrupt.size(); ++i)
            corrupt[i] = char(~corrupt[i]);
        error.clear();
        QVERIFY(!FileSource::open(writeFile(dir, "corrupt.gz", corrupt),
                                  &error));
        QVERIFY(!error.isEmpty());
    }

    void gzipSuffixWithoutMagicOpensPlain()
    {
        QTemporaryDir dir;
        const QByteArray content = "just a plain file named .gz\n";
        const QString path = writeFile(dir, "plain.gz", content);
        QVERIFY(!FileSource::isCompressedFile(path));
        auto src = FileSource::open(path);
        QVERIFY(src);
        QVERIFY(src->mode() != FileSource::Mode::Decompressed);
        QCOMPARE(src->read(0, content.size()), content);
    }

    void gzipBombCapFails()
    {
        QTemporaryDir dir;
        qputenv("LOGDOR_MAX_DECOMPRESSED_MB", "1");
        const QString path = writeFile(
            dir, "bomb.gz", gzipped(QByteArray(3 * 1024 * 1024, 'x')));
        QString error;
        QVERIFY(!FileSource::open(path, &error));
        QVERIFY(error.contains(QStringLiteral("cap")));
    }

    void gzipOpenAsyncDeliversAndCancels()
    {
        QTemporaryDir dir;
        const QByteArray payload = patternedContent(300'000);
        const QString path = writeFile(dir, "async.gz", gzipped(payload));

        auto future = FileSource::openAsync(path);
        future.waitForFinished();
        const FileSource::AsyncOpenResult result = future.result();
        QVERIFY(result.source);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.source->read(0, payload.size()), payload);

        // Plain files resolve through the same path.
        const QString plain = writeFile(dir, "plain.log", payload);
        auto plainFuture = FileSource::openAsync(plain);
        plainFuture.waitForFinished();
        QVERIFY(plainFuture.result().source);
        QCOMPARE(plainFuture.result().source->size(), quint64(payload.size()));

        // Cancellation produces no result.
        auto cancelled = FileSource::openAsync(path);
        cancelled.cancel();
        cancelled.waitForFinished();
        QVERIFY(cancelled.isCanceled());
        QCOMPARE(cancelled.resultCount(), 0);
    }
#endif // LOGDOR_HAVE_ZLIB

    void nonexistentFileFails()
    {
        QString error;
        auto src = FileSource::open(QStringLiteral("/nonexistent/nope.log"), &error);
        QVERIFY(!src);
        QVERIFY(!error.isEmpty());
    }

    void emptyFileIsContiguous()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "empty.log", {});
        QVERIFY(!path.isEmpty());

        auto src = FileSource::open(path);
        QVERIFY(src);
        QCOMPARE(src->size(), quint64(0));
        QCOMPARE(src->mode(), FileSource::Mode::Mapped);
        QVERIFY(src->isContiguous());
        QVERIFY(src->data() != nullptr);
        QCOMPARE(src->read(0, 100), QByteArray());
    }

    void mappedModeExposesFileBytes()
    {
        QTemporaryDir dir;
        const QByteArray content = patternedContent(64 * 1024);
        const QString path = writeFile(dir, "mapped.log", content);

        auto src = FileSource::open(path);
        QVERIFY(src);
        QCOMPARE(src->mode(), FileSource::Mode::Mapped);
        QVERIFY(src->isContiguous());
        QCOMPARE(src->size(), quint64(content.size()));

        QCOMPARE(QByteArray(src->data(), qsizetype(src->size())), content);
        QCOMPARE(src->view(1000, 500).toByteArray(), content.mid(1000, 500));
        QCOMPARE(src->read(63 * 1024, 8 * 1024), content.mid(63 * 1024)); // clamped

        QByteArray buf(700, '\0');
        QCOMPARE(src->readInto(4096, buf.data(), buf.size()), qsizetype(700));
        QCOMPARE(buf, content.mid(4096, 700));
    }

    void bufferedModeMatchesMapped()
    {
        QTemporaryDir dir;
        // 9.5 MiB: spans three 4 MiB cache blocks.
        const QByteArray content = patternedContent(qsizetype(9.5 * 1024 * 1024));
        const QString path = writeFile(dir, "buffered.log", content);

        qputenv("LOGDOR_FORCE_BUFFERED", "1");
        auto src = FileSource::open(path);
        QVERIFY(src);
        QCOMPARE(src->mode(), FileSource::Mode::Buffered);
        QVERIFY(!src->isContiguous());
        QCOMPARE(src->data(), nullptr);

        // Within one block, across block boundaries, at the tail, clamped.
        QCOMPARE(src->read(100, 300), content.mid(100, 300));
        const qsizetype nearBoundary = FileSource::kBlockSize - 150;
        QCOMPARE(src->read(quint64(nearBoundary), 400), content.mid(nearBoundary, 400));
        QCOMPARE(src->read(quint64(2 * FileSource::kBlockSize - 10),
                           FileSource::kBlockSize),
                 content.mid(2 * FileSource::kBlockSize - 10, FileSource::kBlockSize));
        QCOMPARE(src->read(quint64(content.size() - 64), 1024),
                 content.mid(content.size() - 64));
        QCOMPARE(src->read(quint64(content.size()) + 5, 10), QByteArray());

        QByteArray buf(qsizetype(5 * 1024 * 1024), '\0');
        QCOMPARE(src->readInto(1234567, buf.data(), buf.size()), buf.size());
        QCOMPARE(buf, content.mid(1234567, buf.size()));
    }

};

QTEST_APPLESS_MAIN(tst_FileSource)
#include "tst_filesource.moc"
