#include <logdor/ColumnScan.h>
#include <logdor/FileSource.h>
#include <logdor/FilterScan.h>
#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>
#include <logdor/LogcatParser.h>

#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace logdor;

namespace {

struct Opened {
    std::shared_ptr<FileSource> source;
    std::shared_ptr<const LineIndex> index;
    QStringList lines; // reference copy
};

Opened openContent(const QTemporaryDir& dir, const QString& name,
                   const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(content) != content.size())
        return {};
    f.close();
    auto source = FileSource::open(path);
    auto future = buildLineIndex(source);
    future.waitForFinished();
    Opened o { source, future.result().index, {} };
    for (qint64 i = 0; i < o.index->lineCount(); ++i)
        o.lines << QString::fromUtf8(
            source->read(o.index->offsetOf(i), o.index->lengthOf(i)));
    return o;
}

// Naive single-threaded reference implementation of the documented semantics.
QList<qint32> referenceScan(const QStringList& lines, const LineFilter& f)
{
    QList<qint32> matches;
    const Qt::CaseSensitivity cs =
        f.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QRegularExpression re;
    if (f.regexMode)
        re = QRegularExpression(f.query, f.caseSensitive
            ? QRegularExpression::NoPatternOption
            : QRegularExpression::CaseInsensitiveOption);

    for (qint32 i = 0; i < lines.size(); ++i) {
        bool match;
        if (f.query.isEmpty()) {
            match = true; // empty query ignores invert
        } else {
            const bool text = f.regexMode
                ? (re.isValid() && re.match(lines[i]).hasMatch())
                : lines[i].contains(f.query, cs);
            match = text != f.invert;
        }
        if (match && f.extraPredicate) {
            const QByteArray raw = lines[i].toUtf8();
            match = f.extraPredicate(i, QByteArrayView(raw));
        }
        if (match)
            matches << i;
    }

    // Context expansion: union of windows.
    if (f.contextBefore == 0 && f.contextAfter == 0)
        return matches;
    QSet<qint32> visible;
    for (qint32 m : matches) {
        for (qint32 l = qMax(0, m - f.contextBefore);
             l <= qMin(qint32(lines.size()) - 1, m + f.contextAfter); ++l)
            visible.insert(l);
    }
    QList<qint32> out(visible.begin(), visible.end());
    std::sort(out.begin(), out.end());
    return out;
}

FilterScanResult runScan(const Opened& o, const LineFilter& f, qint64 chunk = 7)
{
    auto future = scanFilter(o.source, o.index, f, chunk);
    future.waitForFinished();
    return future.result();
}

void compareToReference(const Opened& o, const LineFilter& f, qint64 chunk = 7)
{
    const auto result = runScan(o, f, chunk);
    const auto expected = referenceScan(o.lines, f);
    QCOMPARE(result.rows.size(), qint64(expected.size()));
    for (qint64 row = 0; row < result.rows.size(); ++row)
        QCOMPARE(result.rows.sourceLine(row), qint64(expected[int(row)]));
}

QByteArray mixedCorpus(int lines)
{
    QByteArray out;
    for (int i = 0; i < lines; ++i) {
        switch (i % 4) {
        case 0: out += "ERROR something failed at step " + QByteArray::number(i); break;
        case 1: out += "info: all good " + QByteArray::number(i); break;
        case 2: out += "Warning: check item " + QByteArray::number(i); break;
        case 3: out += "debug trace caf\xC3\xA9 " + QByteArray::number(i); break;
        }
        out += '\n';
    }
    return out;
}

} // namespace

class tst_FilterScan : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { qunsetenv("LOGDOR_FORCE_BUFFERED"); }

    void passthroughIsAllWithoutFileAccess()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "p.log", mixedCorpus(100));
        const auto result = runScan(o, LineFilter{});
        QVERIFY(result.rows.isAll());
        QCOMPARE(result.rows.size(), 100);
        QCOMPARE(result.matchCount, 100);
        QCOMPARE(result.rows.memoryUsage(), size_t(0));
    }

    void semantics_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<bool>("caseSensitive");
        QTest::addColumn<bool>("regexMode");
        QTest::addColumn<bool>("invert");
        QTest::addColumn<int>("before");
        QTest::addColumn<int>("after");

        QTest::newRow("substring-insensitive") << "error" << false << false << false << 0 << 0;
        QTest::newRow("substring-sensitive") << "ERROR" << true << false << false << 0 << 0;
        QTest::newRow("substring-sensitive-miss") << "error" << true << false << false << 0 << 0;
        QTest::newRow("invert") << "ERROR" << false << false << true << 0 << 0;
        QTest::newRow("regex") << "step \\d+" << false << true << false << 0 << 0;
        QTest::newRow("regex-case") << "warning" << true << true << false << 0 << 0;
        QTest::newRow("regex-invalid") << "([" << false << true << false << 0 << 0;
        QTest::newRow("regex-invalid-invert") << "([" << false << true << true << 0 << 0;
        QTest::newRow("context-after") << "ERROR" << false << false << false << 0 << 2;
        QTest::newRow("context-before") << "ERROR" << false << false << false << 2 << 0;
        QTest::newRow("context-both-overlap") << "a" << false << false << false << 3 << 3;
        QTest::newRow("context-invert") << "ERROR" << false << false << true << 1 << 1;
        QTest::newRow("non-ascii-query") << QString::fromUtf8("caf\xC3\xA9") << false << false << false << 0 << 0;
        QTest::newRow("non-ascii-case") << QString::fromUtf8("CAF\xC3\x89") << false << false << false << 0 << 0;
    }

    void semantics()
    {
        QFETCH(QString, query);
        QFETCH(bool, caseSensitive);
        QFETCH(bool, regexMode);
        QFETCH(bool, invert);
        QFETCH(int, before);
        QFETCH(int, after);

        QTemporaryDir dir;
        auto o = openContent(dir, "s.log", mixedCorpus(200));
        LineFilter f;
        f.query = query;
        f.caseSensitive = caseSensitive;
        f.regexMode = regexMode;
        f.invert = invert;
        f.contextBefore = before;
        f.contextAfter = after;
        compareToReference(o, f);
    }

    void extraPredicateAloneAndCombined()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "e.log", mixedCorpus(120));

        LineFilter onlyExtra;
        onlyExtra.extraPredicate = [](qint64 line, QByteArrayView) {
            return line % 3 == 0;
        };
        compareToReference(o, onlyExtra);

        LineFilter combined;
        combined.query = "ERROR";
        combined.extraPredicate = [](qint64, QByteArrayView raw) {
            return raw.toByteArray().contains("step 1");
        };
        compareToReference(o, combined);
    }

    void chunkBoundariesAndBufferedParity()
    {
        QTemporaryDir dir;
        const QByteArray corpus = mixedCorpus(500);

        auto mapped = openContent(dir, "m.log", corpus);
        LineFilter f;
        f.query = "ERROR";
        f.contextBefore = 1;
        f.contextAfter = 1;
        const auto mappedResult = runScan(mapped, f, 3); // tiny chunks

        qputenv("LOGDOR_FORCE_BUFFERED", "1");
        auto buffered = openContent(dir, "b.log", corpus);
        QCOMPARE(buffered.source->mode(), FileSource::Mode::Buffered);
        const auto bufferedResult = runScan(buffered, f, 3);

        QCOMPARE(bufferedResult.rows.size(), mappedResult.rows.size());
        for (qint64 row = 0; row < mappedResult.rows.size(); ++row)
            QCOMPARE(bufferedResult.rows.sourceLine(row),
                     mappedResult.rows.sourceLine(row));
        compareToReference(mapped, f, 3);
    }

    void crlfLinesMatchWithoutCr()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "crlf.log",
                             QByteArray("tail\r\nhead\r\nother\r\n"));
        LineFilter f;
        f.query = "tail"; // must match line 0, not via a "tail\r" artifact
        f.regexMode = true;
        f.query = "tail$"; // anchors would fail if \r leaked into the text
        const auto result = runScan(o, f);
        QCOMPARE(result.rows.size(), qint64(1));
        QCOMPARE(result.rows.sourceLine(0), qint64(0));
    }

    void everyLineMatchingCollapsesToAll()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "a.log", mixedCorpus(64));
        LineFilter f;
        f.query = " "; // present in every corpus line
        const auto result = runScan(o, f);
        QVERIFY(result.rows.isAll());
    }

    void fieldQueryMatchesReference_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<bool>("invert");
        QTest::addColumn<int>("before");
        QTest::addColumn<int>("after");
        QTest::addColumn<bool>("withExtra");

        QTest::newRow("severity") << "level:error" << false << 0 << 0 << false;
        QTest::newRow("severity-order") << "level>=warning" << false << 0 << 0 << false;
        QTest::newRow("tag-wildcard") << "tag:Tag1* OR tag:Tag3*" << false << 0 << 0 << false;
        QTest::newRow("int-range") << "pid>=150 pid<250" << false << 0 << 0 << false;
        QTest::newRow("mixed-freetext") << "level:error \"message 1\"" << false << 0 << 0 << false;
        QTest::newRow("invert") << "level:error" << true << 0 << 0 << false;
        QTest::newRow("context") << "level:fatal" << false << 2 << 1 << false;
        QTest::newRow("with-extra") << "pid>=100" << false << 0 << 0 << true;
    }

    void fieldQueryMatchesReference()
    {
        QFETCH(QString, query);
        QFETCH(bool, invert);
        QFETCH(int, before);
        QFETCH(int, after);
        QFETCH(bool, withExtra);

        QTemporaryDir dir;
        QByteArray corpus;
        const char levels[] = "VDIWEF";
        for (int i = 0; i < 400; ++i) {
            if (i % 13 == 12) {
                corpus += "raw fallback " + QByteArray::number(i) + "\n";
                continue;
            }
            corpus += "01-01 10:00:00.000 " + QByteArray::number(100 + i) + " "
                + QByteArray::number(i) + " ";
            corpus += levels[i % 6];
            corpus += " Tag" + QByteArray::number(i % 5) + ": message "
                + QByteArray::number(i) + "\n";
        }
        auto o = openContent(dir, "q.log", corpus);
        auto parser = parserById(u"logcat");

        QueryError error;
        auto compiled = CompiledQuery::compile(query, parser->schema(),
                                               Qt::CaseInsensitive, {}, &error);
        QVERIFY2(compiled, qPrintable(error.message));

        auto extract = extractColumns(o.source, o.index, parser,
                                      compiled->referencedColumns(),
                                      compiled->needsSeverity(), {}, 17);
        extract.waitForFinished();
        const auto columns = extract.result();

        LineFilter filter;
        filter.fieldQuery = compiled;
        filter.columns.columns = columns.columns;
        filter.columns.severity = columns.severity;
        filter.invert = invert;
        filter.contextBefore = before;
        filter.contextAfter = after;
        if (withExtra)
            filter.extraPredicate = [](qint64 line, QByteArrayView) {
                return line % 2 == 0;
            };

        auto scan = scanFilter(o.source, o.index, filter, 7);
        scan.waitForFinished();
        const FilterScanResult result = scan.result();

        // Single-threaded reference over the same compiled query.
        QList<qint32> refMatches;
        for (qint32 i = 0; i < qint32(o.lines.size()); ++i) {
            const QByteArray raw = o.lines[i].toUtf8();
            bool match = compiled->evaluate(i, QByteArrayView(raw),
                                            filter.columns) != invert;
            if (match && filter.extraPredicate)
                match = filter.extraPredicate(i, QByteArrayView(raw));
            if (match)
                refMatches << i;
        }
        QSet<qint32> visible;
        for (qint32 m : refMatches) {
            for (qint32 l = qMax(0, m - before);
                 l <= qMin(qint32(o.lines.size()) - 1, m + after); ++l)
                visible.insert(l);
        }
        QList<qint32> expected(visible.begin(), visible.end());
        std::sort(expected.begin(), expected.end());

        QCOMPARE(result.matchCount, qint64(refMatches.size()));
        QCOMPARE(result.rows.size(), qint64(expected.size()));
        for (qint64 row = 0; row < result.rows.size(); ++row)
            QCOMPARE(result.rows.sourceLine(row), qint64(expected[int(row)]));
    }

    void cancellationProducesNoResult()
    {
        QTemporaryDir dir;
        QByteArray big;
        for (int i = 0; i < 400'000; ++i)
            big += "some repetitive line that goes on and on\n";
        auto o = openContent(dir, "big.log", big);
        LineFilter f;
        f.query = "never-present-needle";
        auto future = scanFilter(o.source, o.index, f, 64);
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        QCOMPARE(future.resultCount(), 0);
    }
};

QTEST_APPLESS_MAIN(tst_FilterScan)
#include "tst_filterscan.moc"
