#include <logdor/GrepScan.h>

#include <QTemporaryDir>
#include <QTest>

using namespace logdor;

namespace {

QString writeFile(const QTemporaryDir& dir, const QString& name,
                  const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size())
        return {};
    return path;
}

QList<GrepFileResult> grepSync(const QStringList& files, GrepQuery query)
{
    auto future = grepFolder(files, std::move(query));
    future.waitForFinished();
    QList<GrepFileResult> results;
    for (int i = 0; i < future.resultCount(); ++i)
        results.append(future.resultAt(i));
    return results;
}

} // namespace

class tst_GrepScan : public QObject {
    Q_OBJECT

private slots:
    void matchesAcrossFilesWithLinesAndOffsets()
    {
        QTemporaryDir dir;
        const QString a = writeFile(dir, "a.log",
                                    "nothing\nneedle here\nmore\n");
        const QString b = writeFile(dir, "b.log", "clean file\n");
        const QString c = writeFile(dir, "c.log",
                                    "NEEDLE up top\nneedle again");

        GrepQuery query;
        query.pattern = QStringLiteral("needle");
        const auto results = grepSync({ a, b, c }, query);
        QCOMPARE(results.size(), 2); // b has no matches: silent

        QCOMPARE(results[0].path, a);
        QCOMPARE(results[0].matches.size(), 1);
        QCOMPARE(results[0].matches[0].line, qint32(1));
        QCOMPARE(results[0].matches[0].offset, quint64(8));
        QCOMPARE(results[0].matches[0].excerpt,
                 QStringLiteral("needle here"));

        QCOMPARE(results[1].path, c);
        QCOMPARE(results[1].matches.size(), 2); // case-insensitive default
        QCOMPARE(results[1].matches[1].line, qint32(1)); // unterminated tail
    }

    void regexAndCaseSensitivity()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "r.log",
                                       "Error: one\nerror: two\nwarn\n");
        GrepQuery regex;
        regex.pattern = QStringLiteral("^Error");
        regex.regexMode = true;
        regex.caseSensitivity = Qt::CaseSensitive;
        const auto results = grepSync({ path }, regex);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].matches.size(), 1);
        QCOMPARE(results[0].matches[0].line, qint32(0));
    }

    void capTruncatesAndFlags()
    {
        QTemporaryDir dir;
        QByteArray content;
        for (int i = 0; i < 50; ++i)
            content += "hit " + QByteArray::number(i) + "\n";
        const QString path = writeFile(dir, "many.log", content);
        GrepQuery query;
        query.pattern = QStringLiteral("hit");
        query.maxMatchesPerFile = 10;
        const auto results = grepSync({ path }, query);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].matches.size(), 10);
        QVERIFY(results[0].truncated);
    }

    void binarySkippedAndUnreadableReported()
    {
        QTemporaryDir dir;
        const QString binary = writeFile(
            dir, "bin.dat", QByteArray("match\0hidden", 12));
        GrepQuery query;
        query.pattern = QStringLiteral("match");
        const auto results = grepSync(
            { binary, dir.filePath("missing.log") }, query);
        QCOMPARE(results.size(), 2);
        QVERIFY(results[0].skippedBinary);
        QVERIFY(results[0].matches.isEmpty());
        QVERIFY(!results[1].error.isEmpty());
    }

    void excerptTrimsLongLines()
    {
        QTemporaryDir dir;
        const QString path = writeFile(
            dir, "long.log", "needle " + QByteArray(2000, 'x') + "\n");
        GrepQuery query;
        query.pattern = QStringLiteral("needle");
        query.maxExcerptBytes = 40;
        const auto results = grepSync({ path }, query);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].matches[0].excerpt.size(), 40);
    }

    void emptyPatternIsNoOp()
    {
        QTemporaryDir dir;
        const QString path = writeFile(dir, "x.log", "content\n");
        QCOMPARE(grepSync({ path }, {}).size(), 0);
    }

    void cancellationStops()
    {
        QTemporaryDir dir;
        QByteArray big;
        while (big.size() < 4 * 1024 * 1024)
            big += "some line without the term\n";
        QStringList files;
        for (int i = 0; i < 8; ++i)
            files << writeFile(dir, QStringLiteral("f%1.log").arg(i), big);
        GrepQuery query;
        query.pattern = QStringLiteral("nonexistent-needle");
        auto future = grepFolder(files, query);
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
    }
};

QTEST_APPLESS_MAIN(tst_GrepScan)
#include "tst_grepscan.moc"
