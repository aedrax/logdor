#include <logdor/FileSource.h>
#include <logdor/LineIndex.h>
#include <logdor/LineIndexer.h>

#include <QTemporaryDir>
#include <QTest>

using logdor::FileSource;
using logdor::IndexingResult;
using logdor::LineIndex;
using logdor::buildLineIndex;

namespace {

struct RefLine {
    qsizetype offset;
    qsizetype rawLength; // excludes '\n', keeps '\r' - legacy-exact
};

// Reference reimplementation of the legacy MainWindow scan: split on '\n',
// entry spans exclude the '\n', an unterminated tail is still a line, and a
// trailing '\n' creates no empty final line. This is the regression lock for
// the plugin-facing bridge.
QList<RefLine> legacyScan(const QByteArray& data)
{
    QList<RefLine> lines;
    qsizetype start = 0;
    while (start < data.size()) {
        const qsizetype nl = data.indexOf('\n', start);
        if (nl < 0) {
            lines.append({ start, data.size() - start });
            break;
        }
        lines.append({ start, nl - start });
        start = nl + 1;
    }
    return lines;
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

IndexingResult buildSync(const QString& path, qsizetype chunkSize)
{
    auto src = FileSource::open(path);
    if (!src)
        return {};
    auto future = buildLineIndex(std::move(src), chunkSize);
    future.waitForFinished();
    return future.result();
}

void comparePairwise(const QByteArray& data, const LineIndex& idx)
{
    const QList<RefLine> ref = legacyScan(data);
    QCOMPARE(idx.lineCount(), qint64(ref.size()));
    for (qint64 i = 0; i < idx.lineCount(); ++i) {
        QCOMPARE(idx.offsetOf(i), quint64(ref[i].offset));
        QCOMPARE(idx.rawLengthOf(i), ref[i].rawLength);
    }
    QCOMPARE(idx.fileSize(), quint64(data.size()));
}

} // namespace

class tst_LineIndexer : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { qunsetenv("LOGDOR_FORCE_BUFFERED"); }

    void legacyParity_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::addColumn<int>("chunkSize");
        QTest::addColumn<bool>("forceBuffered");

        QByteArray mixed;
        for (int i = 0; i < 500; ++i) {
            switch (i % 5) {
            case 0: mixed += "plain line " + QByteArray::number(i) + "\n"; break;
            case 1: mixed += "dos line " + QByteArray::number(i) + "\r\n"; break;
            case 2: mixed += "\n"; break;                       // empty line
            case 3: mixed += "embedded\rcarriage " + QByteArray::number(i) + "\n"; break;
            case 4: mixed += QByteArray(300, 'x') + "\n"; break; // long-ish line
            }
        }
        // UTF-8 multibyte content (each é is 2 bytes, 日 is 3).
        QByteArray utf8;
        for (int i = 0; i < 200; ++i)
            utf8 += QByteArrayLiteral("caf\xC3\xA9 \xE6\x97\xA5 line ")
                    + QByteArray::number(i) + "\n";

        const QByteArray unterminated = mixed + "tail without newline";

        for (int chunk : { 7, 64, 4096, 1 << 20 }) {
            for (bool buffered : { false, true }) {
                const char* mode = buffered ? "buffered" : "mapped";
                QTest::addRow("mixed/chunk=%d/%s", chunk, mode)
                    << mixed << chunk << buffered;
                QTest::addRow("utf8/chunk=%d/%s", chunk, mode)
                    << utf8 << chunk << buffered;
                QTest::addRow("unterminated/chunk=%d/%s", chunk, mode)
                    << unterminated << chunk << buffered;
            }
        }
        QTest::addRow("empty/mapped") << QByteArray() << 4096 << false;
        QTest::addRow("newlines-only/mapped") << QByteArray("\n\n\n\n") << 2 << false;
    }

    void legacyParity()
    {
        QFETCH(QByteArray, content);
        QFETCH(int, chunkSize);
        QFETCH(bool, forceBuffered);

        QTemporaryDir dir;
        const QString path = writeFile(dir, "fixture.log", content);
        QVERIFY(!path.isEmpty());
        if (forceBuffered)
            qputenv("LOGDOR_FORCE_BUFFERED", "1");

        const IndexingResult result = buildSync(path, chunkSize);
        QVERIFY(result.index);
        QCOMPARE(result.bytesScanned, quint64(content.size()));
        comparePairwise(content, *result.index);
    }

    void crlfSplitAcrossChunkBoundary()
    {
        // "ab\r\n..." with chunk size 3: the '\r' is the last byte of chunk 0,
        // the '\n' the first byte of chunk 1 - the carry byte must preserve
        // CRLF detection.
        const QByteArray content = "ab\r\ncd\r\nef";
        QTemporaryDir dir;
        const QString path = writeFile(dir, "crlf.log", content);

        const IndexingResult result = buildSync(path, 3);
        QVERIFY(result.index);
        QCOMPARE(result.index->lineCount(), qint64(3));
        QVERIFY(result.index->endsWithCrLf(0));
        QVERIFY(result.index->endsWithCrLf(1));
        QCOMPARE(result.index->lengthOf(0), qsizetype(2));    // "ab"
        QCOMPARE(result.index->rawLengthOf(0), qsizetype(3)); // "ab\r"
        comparePairwise(content, *result.index);
    }

    void progressReachesFullScale()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "p.log", QByteArray(256 * 1024, 'a'));
        auto src = FileSource::open(path);
        QVERIFY(src);
        auto future = buildLineIndex(std::move(src), 64 * 1024);
        future.waitForFinished();
        QCOMPARE(future.progressValue(), 1000);
        QCOMPARE(future.result().lineCount, qint64(1)); // one giant line
    }

    void cancellationProducesNoResult()
    {
        QTemporaryDir dir;
        QByteArray big;
        big.reserve(8 * 1024 * 1024);
        while (big.size() < 8 * 1024 * 1024)
            big += "some log line that repeats forever and ever\n";
        const QString path = writeFile(dir, "big.log", big);

        auto src = FileSource::open(path);
        QVERIFY(src);
        auto future = buildLineIndex(std::move(src), 4096); // many cancel points
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        QCOMPARE(future.resultCount(), 0);
    }
};

QTEST_APPLESS_MAIN(tst_LineIndexer)
#include "tst_lineindexer.moc"
