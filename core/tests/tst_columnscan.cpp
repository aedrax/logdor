#include <logdor/ColumnScan.h>
#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>
#include <logdor/LogcatParser.h>

#include <QTemporaryDir>
#include <QTest>

using namespace logdor;

namespace {

struct Opened {
    std::shared_ptr<FileSource> source;
    std::shared_ptr<const LineIndex> index;
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
    return { source, future.result().index };
}

QByteArray logcatCorpus(int lines)
{
    QByteArray out;
    const char levels[] = "VDIWEF";
    for (int i = 0; i < lines; ++i) {
        if (i % 11 == 10) {
            out += "raw garbage line " + QByteArray::number(i) + "\n";
            continue;
        }
        out += "01-0" + QByteArray::number(1 + i % 9) + " 10:00:0"
            + QByteArray::number(i % 10) + ".000 " + QByteArray::number(100 + i)
            + " " + QByteArray::number(i) + " ";
        out += levels[i % 6];
        out += " Tag" + QByteArray::number(i % 5) + ": message "
            + QByteArray::number(i) + "\n";
    }
    return out;
}

} // namespace

class tst_ColumnScan : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { qunsetenv("LOGDOR_FORCE_BUFFERED"); }

    void extractedValuesMatchDirectParse()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "c.log", logcatCorpus(500));
        auto parser = parserById(u"logcat");

        // Tag (string), PID (integer), plus severity, with tiny chunks so
        // shard merging is exercised.
        auto future = extractColumns(o.source, o.index, parser,
                                     { LogcatParser::Tag, LogcatParser::Pid },
                                     true, 7);
        future.waitForFinished();
        const ColumnScanResult result = future.result();

        const auto tag = result.columns[LogcatParser::Tag];
        const auto pid = result.columns[LogcatParser::Pid];
        QVERIFY(tag && pid && result.severity);
        QCOMPARE(tag->lineCount(), o.index->lineCount());
        QCOMPARE(qint64(result.severity->size()), o.index->lineCount());

        ParsedRow row;
        for (qint64 line = 0; line < o.index->lineCount(); ++line) {
            const QByteArray raw = o.source->read(o.index->offsetOf(line),
                                                  o.index->lengthOf(line));
            parser->parseLine(QByteArrayView(raw), row);

            QCOMPARE(QString::fromUtf8(tag->stringAt(line)),
                     row.fields[LogcatParser::Tag]);
            qint64 pidValue = 0;
            const bool pidValid = pid->intAt(line, &pidValue);
            bool expectedOk = false;
            const qint64 expected =
                row.fields[LogcatParser::Pid].toLongLong(&expectedOk);
            QCOMPARE(pidValid, expectedOk);
            if (pidValid)
                QCOMPARE(pidValue, expected);
            QCOMPARE((*result.severity)[size_t(line)], quint8(row.severity));
        }
    }

    void bufferedModeParity()
    {
        QTemporaryDir dir;
        const QByteArray corpus = logcatCorpus(300);
        auto mapped = openContent(dir, "m.log", corpus);
        auto parser = parserById(u"logcat");
        auto f1 = extractColumns(mapped.source, mapped.index, parser,
                                 { LogcatParser::Tag }, false, 13);
        f1.waitForFinished();

        qputenv("LOGDOR_FORCE_BUFFERED", "1");
        auto buffered = openContent(dir, "b.log", corpus);
        QCOMPARE(buffered.source->mode(), FileSource::Mode::Buffered);
        auto f2 = extractColumns(buffered.source, buffered.index, parser,
                                 { LogcatParser::Tag }, false, 13);
        f2.waitForFinished();

        const auto a = f1.result().columns[LogcatParser::Tag];
        const auto b = f2.result().columns[LogcatParser::Tag];
        QCOMPARE(a->lineCount(), b->lineCount());
        for (qint64 line = 0; line < a->lineCount(); ++line)
            QCOMPARE(a->stringAt(line).toByteArray(),
                     b->stringAt(line).toByteArray());
    }

    void emptyFileYieldsEmptyColumns()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "e.log", QByteArray());
        auto future = extractColumns(o.source, o.index, parserById(u"logcat"),
                                     { LogcatParser::Tag }, true);
        future.waitForFinished();
        const auto result = future.result();
        QCOMPARE(result.columns[LogcatParser::Tag]->lineCount(), qint64(0));
        QCOMPARE(result.severity->size(), size_t(0));
    }

    void cancellation()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "big.log", logcatCorpus(200'000));
        auto future = extractColumns(o.source, o.index, parserById(u"logcat"),
                                     { LogcatParser::Message }, true, 64);
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        QCOMPARE(future.resultCount(), 0);
    }

    void cacheSnapshotAndMissing()
    {
        ColumnCache cache;
        QCOMPARE(cache.missing({ 0, 2 }), (QList<int>{ 0, 2 }));

        ColumnData::Builder builder(FieldType::String);
        builder.appendString("x");
        cache.insert(0, std::make_shared<const ColumnData>(std::move(builder).build()));
        QCOMPARE(cache.missing({ 0, 2 }), QList<int>{ 2 });
        QVERIFY(!cache.hasSeverity());

        auto snap = cache.snapshot({ 0 }, false);
        QVERIFY(snap.covers({ 0 }, false));
        QVERIFY(!snap.covers({ 0, 2 }, false));
        QVERIFY(!snap.covers({ 0 }, true));

        cache.setSeverity(std::make_shared<std::vector<quint8>>(1, 3));
        QVERIFY(cache.snapshot({ 0 }, true).covers({ 0 }, true));
        QVERIFY(cache.memoryUsage() > 0);
        cache.clear();
        QCOMPARE(cache.missing({ 0 }), QList<int>{ 0 });
    }
};

QTEST_APPLESS_MAIN(tst_ColumnScan)
#include "tst_columnscan.moc"
