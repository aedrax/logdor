#include <logdor/LineIndex.h>

#include <QTest>

using logdor::LineIndex;

// Reference builder: scan a buffer the way the indexer will.
static LineIndex indexOf(const QByteArray& data)
{
    LineIndex idx;
    for (qsizetype i = 0; i < data.size(); ++i) {
        if (data[i] == '\n')
            idx.addTerminator(quint64(i), i > 0 && data[i - 1] == '\r');
    }
    idx.finalize(quint64(data.size()));
    return idx;
}

// The line content a legacy consumer would see (excludes '\n', keeps '\r').
static QByteArray rawLine(const QByteArray& data, const LineIndex& idx, qint64 line)
{
    return data.mid(qsizetype(idx.offsetOf(line)), idx.rawLengthOf(line));
}

// Follow-mode reference: index head alone, resume, then scan only what lies
// past resumeOffset() of head+tail - the incremental path extendLineIndex
// will drive.
static LineIndex extendOf(const QByteArray& head, const QByteArray& tail)
{
    const LineIndex base = indexOf(head);
    LineIndex resumed = LineIndex::resumedFrom(base);
    const QByteArray all = head + tail;
    for (qsizetype i = qsizetype(base.resumeOffset()); i < all.size(); ++i) {
        if (all[i] == '\n')
            resumed.addTerminator(quint64(i), i > 0 && all[i - 1] == '\r');
    }
    resumed.finalize(quint64(all.size()));
    return resumed;
}

static void compareIndexes(const LineIndex& actual, const LineIndex& expected)
{
    QCOMPARE(actual.lineCount(), expected.lineCount());
    QCOMPARE(actual.fileSize(), expected.fileSize());
    QCOMPARE(actual.lastLineTerminated(), expected.lastLineTerminated());
    QCOMPARE(actual.resumeOffset(), expected.resumeOffset());
    for (qint64 line = 0; line < expected.lineCount(); ++line) {
        QCOMPARE(actual.offsetOf(line), expected.offsetOf(line));
        QCOMPARE(actual.rawLengthOf(line), expected.rawLengthOf(line));
        QCOMPARE(actual.lengthOf(line), expected.lengthOf(line));
        QCOMPARE(actual.endsWithCrLf(line), expected.endsWithCrLf(line));
    }
}

class tst_LineIndex : public QObject {
    Q_OBJECT
private slots:
    void emptyFile()
    {
        const LineIndex idx = indexOf("");
        QCOMPARE(idx.lineCount(), 0);
        QCOMPARE(idx.fileSize(), quint64(0));
    }

    void trailingNewlineCreatesNoEmptyLine()
    {
        const QByteArray data = "a\nb\n";
        const LineIndex idx = indexOf(data);
        QCOMPARE(idx.lineCount(), 2);
        QCOMPARE(rawLine(data, idx, 0), QByteArray("a"));
        QCOMPARE(rawLine(data, idx, 1), QByteArray("b"));
        QCOMPARE(idx.endOffsetOf(1), quint64(4));
    }

    void noTrailingNewline()
    {
        const QByteArray data = "a\nb";
        const LineIndex idx = indexOf(data);
        QCOMPARE(idx.lineCount(), 2);
        QCOMPARE(rawLine(data, idx, 1), QByteArray("b"));
        QCOMPARE(idx.endOffsetOf(1), quint64(3));
    }

    void noNewlineAtAll()
    {
        const QByteArray data = "abc";
        const LineIndex idx = indexOf(data);
        QCOMPARE(idx.lineCount(), 1);
        QCOMPARE(rawLine(data, idx, 0), QByteArray("abc"));
    }

    void allEmptyLines()
    {
        const QByteArray data = "\n\n\n";
        const LineIndex idx = indexOf(data);
        QCOMPARE(idx.lineCount(), 3);
        for (qint64 i = 0; i < 3; ++i) {
            QCOMPARE(idx.rawLengthOf(i), qsizetype(0));
            QCOMPARE(idx.lengthOf(i), qsizetype(0));
        }
    }

    void crlfKeptRawTrimmedClean()
    {
        const QByteArray data = "a\r\nbc\r\n";
        const LineIndex idx = indexOf(data);
        QCOMPARE(idx.lineCount(), 2);
        QCOMPARE(rawLine(data, idx, 0), QByteArray("a\r")); // legacy keeps \r
        QCOMPARE(idx.rawLengthOf(0), qsizetype(2));
        QCOMPARE(idx.lengthOf(0), qsizetype(1));
        QVERIFY(idx.endsWithCrLf(0));
        QVERIFY(idx.endsWithCrLf(1));
    }

    void loneCrIsNotASeparator()
    {
        const QByteArray data = "a\rb\n";
        const LineIndex idx = indexOf(data);
        QCOMPARE(idx.lineCount(), 1);
        QCOMPARE(rawLine(data, idx, 0), QByteArray("a\rb"));
        QVERIFY(!idx.endsWithCrLf(0));
        QCOMPARE(idx.lengthOf(0), qsizetype(3));
    }

    void mixedLineEndings()
    {
        const QByteArray data = "unix\ndos\r\nlast";
        const LineIndex idx = indexOf(data);
        QCOMPARE(idx.lineCount(), 3);
        QVERIFY(!idx.endsWithCrLf(0));
        QVERIFY(idx.endsWithCrLf(1));
        QVERIFY(!idx.endsWithCrLf(2));
        QCOMPARE(rawLine(data, idx, 1), QByteArray("dos\r"));
        QCOMPARE(idx.lengthOf(1), qsizetype(3));
        QCOMPARE(rawLine(data, idx, 2), QByteArray("last"));
    }

    void blockBoundaries()
    {
        // 3000 lines of varying width crossing three 1024-line blocks.
        QByteArray data;
        std::vector<quint64> starts;
        for (int i = 0; i < 3000; ++i) {
            starts.push_back(quint64(data.size()));
            data += QByteArray::number(i);
            data += QByteArray(i % 7, 'x');
            data += '\n';
        }
        const LineIndex idx = indexOf(data);
        QCOMPARE(idx.lineCount(), 3000);
        QVERIFY(!idx.isWide());
        for (qint64 line : { qint64(0), qint64(1023), qint64(1024),
                             qint64(1025), qint64(2047), qint64(2048),
                             qint64(2999) }) {
            QCOMPARE(idx.offsetOf(line), starts[size_t(line)]);
        }
    }

    void wideModeMigrationPreservesOffsets()
    {
        // Synthetic terminators only - no real multi-GB file needed. A >4 GiB
        // line inside the first block forces wide-mode migration mid-build.
        LineIndex idx;
        idx.addTerminator(10, false);                    // line 0: [0,10)
        idx.addTerminator(20, false);                    // line 1: [11,20)
        idx.addTerminator(6'000'000'000ULL, false);      // line 2: >4 GiB span
        idx.addTerminator(6'000'000'005ULL, true);       // line 3, crlf
        idx.finalize(6'000'000'010ULL);

        QVERIFY(idx.isWide());
        QCOMPARE(idx.lineCount(), 5);
        QCOMPARE(idx.offsetOf(0), quint64(0));
        QCOMPARE(idx.offsetOf(1), quint64(11));
        QCOMPARE(idx.offsetOf(2), quint64(21));
        QCOMPARE(idx.offsetOf(3), quint64(6'000'000'001ULL));
        QCOMPARE(idx.offsetOf(4), quint64(6'000'000'006ULL));
        QCOMPARE(idx.rawLengthOf(2), qsizetype(6'000'000'000ULL - 21));
        QVERIFY(idx.endsWithCrLf(3));
        QCOMPARE(idx.lengthOf(3), qsizetype(3)); // 5 raw span - \r
        QCOMPARE(idx.rawLengthOf(4), qsizetype(4)); // unterminated tail
    }

    void narrowFilesStayNarrow()
    {
        QByteArray data;
        for (int i = 0; i < 5000; ++i)
            data += "some ordinary log line\n";
        const LineIndex idx = indexOf(data);
        QVERIFY(!idx.isWide());
        QCOMPARE(idx.lineCount(), 5000);
    }

    void resumeAccessors()
    {
        const LineIndex terminated = indexOf("a\nb\n");
        QVERIFY(terminated.lastLineTerminated());
        QCOMPARE(terminated.resumeOffset(), quint64(4));

        const LineIndex unterminated = indexOf("a\nb");
        QVERIFY(!unterminated.lastLineTerminated());
        QCOMPARE(unterminated.resumeOffset(), quint64(2)); // start of "b"

        const LineIndex empty = indexOf("");
        QVERIFY(!empty.lastLineTerminated());
        QCOMPARE(empty.resumeOffset(), quint64(0));

        const LineIndex oneLine = indexOf("abc");
        QCOMPARE(oneLine.resumeOffset(), quint64(0)); // rescan whole line
    }

    void resumedExtendMatchesFreshBuild_data()
    {
        QTest::addColumn<QByteArray>("head");
        QTest::addColumn<QByteArray>("tail");
        QTest::newRow("terminated head") << QByteArray("a\nb\n")
                                         << QByteArray("c\nd");
        QTest::newRow("unterminated line grows") << QByteArray("a\nHAL")
                                                 << QByteArray("F\nmore\n");
        QTest::newRow("crlf split at old EOF") << QByteArray("abc\r")
                                               << QByteArray("\ndef\n");
        QTest::newRow("empty head") << QByteArray() << QByteArray("a\r\nb");
        QTest::newRow("zero growth terminated") << QByteArray("a\nb\n")
                                                << QByteArray();
        QTest::newRow("zero growth unterminated") << QByteArray("a\nb")
                                                  << QByteArray();
        QTest::newRow("newline-only growth") << QByteArray("tail")
                                             << QByteArray("\n");
        QTest::newRow("empty lines around boundary")
            << QByteArray("\n\nx") << QByteArray("\n\n");
    }

    void resumedExtendMatchesFreshBuild()
    {
        QFETCH(QByteArray, head);
        QFETCH(QByteArray, tail);
        compareIndexes(extendOf(head, tail), indexOf(head + tail));
    }

    void memoryStaysCompact()
    {
        LineIndex idx;
        idx.reserveLines(100'000);
        quint64 pos = 0;
        for (int i = 0; i < 100'000; ++i) {
            pos += 50;
            idx.addTerminator(pos, false);
        }
        idx.finalize(pos + 1);
        QCOMPARE(idx.lineCount(), 100'000);
        const double bytesPerLine =
            double(idx.memoryUsage()) / double(idx.lineCount());
        QVERIFY2(bytesPerLine <= 5.0,
                 qPrintable(QStringLiteral("%1 B/line").arg(bytesPerLine)));
    }
};

QTEST_APPLESS_MAIN(tst_LineIndex)
#include "tst_lineindex.moc"
