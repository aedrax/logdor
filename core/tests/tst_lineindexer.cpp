#include <logdor/FileSource.h>
#include <logdor/LineIndex.h>
#include <logdor/LineIndexer.h>

#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using logdor::FileSource;
using logdor::IndexingResult;
using logdor::LineIndex;
using logdor::buildLineIndex;
using logdor::extendLineIndex;

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

IndexingResult extendSync(const QString& path,
                          std::shared_ptr<const LineIndex> previous,
                          qsizetype chunkSize)
{
    auto src = FileSource::open(path); // follow mode always reopens
    if (!src)
        return {};
    auto future = extendLineIndex(std::move(src), std::move(previous), chunkSize);
    future.waitForFinished();
    return future.result();
}

bool appendToFile(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    return f.write(bytes) == bytes.size();
}

void compareIndexes(const LineIndex& actual, const LineIndex& expected)
{
    QCOMPARE(actual.lineCount(), expected.lineCount());
    QCOMPARE(actual.fileSize(), expected.fileSize());
    QCOMPARE(actual.lastLineTerminated(), expected.lastLineTerminated());
    QCOMPARE(actual.resumeOffset(), expected.resumeOffset());
    for (qint64 line = 0; line < expected.lineCount(); ++line) {
        QCOMPARE(actual.offsetOf(line), expected.offsetOf(line));
        QCOMPARE(actual.rawLengthOf(line), expected.rawLengthOf(line));
        QCOMPARE(actual.endsWithCrLf(line), expected.endsWithCrLf(line));
    }
}

// A burst of appended log data hitting every line-ending shape; roughly one
// in three bursts ends without a newline (a partially written line).
QByteArray randomBurst(QRandomGenerator& rng)
{
    QByteArray burst;
    const int lines = int(rng.bounded(5));
    for (int i = 0; i < lines; ++i) {
        switch (rng.bounded(4)) {
        case 0: burst += "plain " + QByteArray::number(rng.generate()) + "\n"; break;
        case 1: burst += "dos " + QByteArray::number(rng.generate()) + "\r\n"; break;
        case 2: burst += "\n"; break;
        case 3: burst += "lone\rcr content\n"; break;
        }
    }
    if (rng.bounded(3) == 0)
        burst += "partial tail";
    else if (rng.bounded(4) == 0)
        burst += "ends with cr\r"; // next burst may start with '\n'
    return burst;
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

    void extendMatchesFullRebuild_data()
    {
        QTest::addColumn<int>("seed");
        QTest::addColumn<int>("chunkSize");
        QTest::addColumn<bool>("forceBuffered");
        for (int seed : { 1, 2, 3 }) {
            for (int chunk : { 5, 4096 }) {
                QTest::addRow("seed=%d/chunk=%d/mapped", seed, chunk)
                    << seed << chunk << false;
            }
        }
        QTest::addRow("seed=1/chunk=4096/buffered") << 1 << 4096 << true;
    }

    void extendMatchesFullRebuild()
    {
        QFETCH(int, seed);
        QFETCH(int, chunkSize);
        QFETCH(bool, forceBuffered);

        QTemporaryDir dir;
        QRandomGenerator rng{ quint32(seed) };
        QByteArray content = randomBurst(rng);
        const QString path = writeFile(dir, "follow.log", content);
        QVERIFY(!path.isEmpty());
        if (forceBuffered)
            qputenv("LOGDOR_FORCE_BUFFERED", "1");

        auto current = buildSync(path, chunkSize).index;
        QVERIFY(current);

        for (int step = 0; step < 12; ++step) {
            const QByteArray burst = randomBurst(rng);
            QVERIFY(appendToFile(path, burst));
            content += burst;

            const IndexingResult extended
                = extendSync(path, current, chunkSize);
            QVERIFY(extended.index);
            QCOMPARE(extended.bytesScanned,
                     quint64(content.size()) - current->resumeOffset());

            const IndexingResult fresh = buildSync(path, chunkSize);
            QVERIFY(fresh.index);
            compareIndexes(*extended.index, *fresh.index);
            comparePairwise(content, *extended.index);
            current = extended.index;
        }
    }

    void extendCrlfSplitAtOldEof()
    {
        // Old file ends "...\r" unterminated; the appended bytes start with
        // '\n'. resumeOffset() re-covers the '\r', so the pair is detected.
        QTemporaryDir dir;
        const QString path = writeFile(dir, "crlf.log", "abc\r");
        auto base = buildSync(path, 4096).index;
        QVERIFY(base);
        QCOMPARE(base->lineCount(), qint64(1));

        QVERIFY(appendToFile(path, "\ndef\n"));
        const IndexingResult extended = extendSync(path, base, 4096);
        QVERIFY(extended.index);
        QCOMPARE(extended.index->lineCount(), qint64(2));
        QVERIFY(extended.index->endsWithCrLf(0));
        QCOMPARE(extended.index->lengthOf(0), qsizetype(3));    // "abc"
        QCOMPARE(extended.index->rawLengthOf(0), qsizetype(4)); // "abc\r"
    }

    void extendZeroGrowthIsIdentity()
    {
        QTemporaryDir dir;
        for (const QByteArray& content :
             { QByteArray("a\nb\n"), QByteArray("a\nb"), QByteArray() }) {
            const QString path = writeFile(dir, "same.log", content);
            auto base = buildSync(path, 4096).index;
            QVERIFY(base);
            const IndexingResult extended = extendSync(path, base, 4096);
            QVERIFY(extended.index);
            QCOMPARE(extended.bytesScanned,
                     quint64(content.size()) - base->resumeOffset());
            compareIndexes(*extended.index, *base);
        }
    }

    void extendShrunkFileReturnsPrevious()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "shrink.log", "a\nb\nc\n");
        auto base = buildSync(path, 4096).index;
        QVERIFY(base);

        QVERIFY(writeFile(dir, "shrink.log", "a\n") == path); // truncate
        const IndexingResult extended = extendSync(path, base, 4096);
        QCOMPARE(extended.index.get(), base.get());
        QCOMPARE(extended.lineCount, base->lineCount());
    }

    void extendCancelLeavesPreviousIntact()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "grow.log", "start\n");
        auto base = buildSync(path, 4096).index;
        QVERIFY(base);
        const qint64 baseCount = base->lineCount();
        const quint64 baseSize = base->fileSize();

        QByteArray big;
        while (big.size() < 4 * 1024 * 1024)
            big += "appended line that keeps the scanner busy\n";
        QVERIFY(appendToFile(path, big));

        auto src = FileSource::open(path);
        QVERIFY(src);
        auto future = extendLineIndex(src, base, 4096); // many cancel points
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        QCOMPARE(future.resultCount(), 0);
        QCOMPARE(base->lineCount(), baseCount); // untouched snapshot
        QCOMPARE(base->fileSize(), baseSize);
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
