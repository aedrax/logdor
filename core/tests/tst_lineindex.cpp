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
