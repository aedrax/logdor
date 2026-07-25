#include <logdor/CsvParser.h>
#include <logdor/ExportScan.h>
#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>

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

ExportResult exportSync(const Opened& o, RowSet rows,
                        std::vector<qint32> order, ExportRequest request,
                        const QString& parserId = QStringLiteral("plaintext"))
{
    auto future = exportRows(o.source, o.index, parserById(parserId),
                             std::move(rows), std::move(order),
                             std::move(request));
    future.waitForFinished();
    return future.result();
}

QByteArray fileBytes(const QString& path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

} // namespace

class tst_ExportScan : public QObject {
    Q_OBJECT

private slots:
    void textExportIsByteExact()
    {
        QTemporaryDir dir;
        // CRLF line: lengthOf strips the '\r', matching what the viewer
        // shows; the unterminated tail still exports as a line.
        auto o = openContent(dir, "src.log", "alpha\r\nbeta\ngamma");
        const QString out = dir.filePath("out.txt");
        const ExportResult result
            = exportSync(o, RowSet::all(3), {}, { ExportFormat::Text, out });
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.rowsWritten, qint64(3));
        QCOMPARE(fileBytes(out), QByteArray("alpha\nbeta\ngamma\n"));
    }

    void textExportRespectsRowSetAndOrder()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "src.log", "a\nb\nc\nd\n");
        const QString out = dir.filePath("out.txt");
        // Visible rows {0,2,3}, displayed sorted as 3,0,2.
        const ExportResult result
            = exportSync(o, RowSet::fromLines({ 0, 2, 3 }, 4), { 2, 0, 1 },
                         { ExportFormat::Text, out });
        QVERIFY(result.error.isEmpty());
        QCOMPARE(fileBytes(out), QByteArray("d\na\nc\n"));
    }

    void csvExportQuotesPerRfc4180()
    {
        QTemporaryDir dir;
        // CSV parser: header-derived schema, so exporting round-trips it.
        auto o = openContent(dir, "src.csv",
                             "name,note\n"
                             "plain,simple\n"
                             "comma,\"a,b\"\n"
                             "quote,\"say \"\"hi\"\"\"\n");
        auto parser = CsvParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);
        const QString out = dir.filePath("out.csv");
        auto future = exportRows(o.source, o.index, parser,
                                 RowSet::fromLines({ 1, 2, 3 }, 4), {},
                                 { ExportFormat::Csv, out, true });
        future.waitForFinished();
        const ExportResult result = future.result();
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(fileBytes(out),
                 QByteArray("name,note\n"
                            "plain,simple\n"
                            "comma,\"a,b\"\n"
                            "quote,\"say \"\"hi\"\"\"\n"));
    }

    void csvHeaderToggle()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "src.csv", "col\nvalue\n");
        auto parser = CsvParser::fromFile(*o.source, *o.index);
        const QString out = dir.filePath("out.csv");
        auto future = exportRows(o.source, o.index, parser,
                                 RowSet::fromLines({ 1 }, 2), {},
                                 { ExportFormat::Csv, out, false });
        future.waitForFinished();
        QVERIFY(future.result().error.isEmpty());
        QCOMPARE(fileBytes(out), QByteArray("value\n"));
    }

    void unwritablePathReportsError()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "src.log", "a\n");
        const ExportResult result = exportSync(
            o, RowSet::all(1), {},
            { ExportFormat::Text, QStringLiteral("/nonexistent/dir/out.txt") });
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(result.rowsWritten, qint64(0));
    }

    void cancelRemovesPartialFile()
    {
        QTemporaryDir dir;
        QByteArray big;
        for (int i = 0; i < 500'000; ++i)
            big += "line that will be cancelled midway through\n";
        auto o = openContent(dir, "big.log", big);
        const QString out = dir.filePath("partial.txt");

        auto future = exportRows(o.source, o.index, parserById(u"plaintext"),
                                 RowSet::all(o.index->lineCount()), {},
                                 { ExportFormat::Text, out });
        future.cancel();
        future.waitForFinished();
        QVERIFY(future.isCanceled());
        QCOMPARE(future.resultCount(), 0);
        QVERIFY(!QFile::exists(out)); // no half-truths on disk
    }
};

QTEST_APPLESS_MAIN(tst_ExportScan)
#include "tst_exportscan.moc"
