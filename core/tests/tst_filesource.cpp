#include <logdor/FileSource.h>

#include <QTemporaryDir>
#include <QTest>

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

} // namespace

class tst_FileSource : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        qunsetenv("LOGDOR_FORCE_BUFFERED");
    }

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
