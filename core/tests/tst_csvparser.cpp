#include <logdor/CsvParser.h>
#include <logdor/FileSource.h>
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

} // namespace

class tst_CsvParser : public QObject {
    Q_OBJECT

private slots:
    void recordSplitting_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<QStringList>("expected");

        QTest::newRow("plain") << QByteArray("a,b,c")
                               << QStringList { "a", "b", "c" };
        QTest::newRow("quoted-commas") << QByteArray("a,\"b,c\",d")
                                       << QStringList { "a", "b,c", "d" };
        QTest::newRow("escaped-quotes") << QByteArray("\"say \"\"hi\"\"\",x")
                                        << QStringList { "say \"hi\"", "x" };
        QTest::newRow("empty-fields") << QByteArray("a,,c")
                                      << QStringList { "a", "", "c" };
        QTest::newRow("trailing-comma") << QByteArray("a,b,")
                                        << QStringList { "a", "b", "" };
        QTest::newRow("trim-unquoted") << QByteArray("  a , b ,c  ")
                                       << QStringList { "a", "b", "c" };
        QTest::newRow("quoted-keeps-spaces") << QByteArray("\" a \",b")
                                             << QStringList { " a ", "b" };
        QTest::newRow("lenient-tail") << QByteArray("\"a\"tail,b")
                                      << QStringList { "atail", "b" };
        QTest::newRow("utf8") << QByteArray("caf\xC3\xA9,\xE6\x97\xA5")
                              << QStringList { QString::fromUtf8("caf\xC3\xA9"),
                                               QString::fromUtf8("\xE6\x97\xA5") };
        QTest::newRow("single") << QByteArray("solo") << QStringList { "solo" };
        QTest::newRow("empty") << QByteArray("") << QStringList { "" };
    }

    void recordSplitting()
    {
        QFETCH(QByteArray, line);
        QFETCH(QStringList, expected);
        QCOMPARE(parseCsvRecord(QByteArrayView(line)), expected);
    }

    void fromFileBuildsSchemaAndSniffsTypes()
    {
        QTemporaryDir dir;
        QByteArray csv = "time,status,bytes,message\n";
        for (int i = 0; i < 80; ++i)
            csv += QByteArray("10:0") + QByteArray::number(i % 10) + ",200,"
                + QByteArray::number(i * 100) + ",request handled\n";
        auto o = openContent(dir, "t.csv", csv);

        const auto parser = CsvParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);
        const auto schema = parser->schema();
        QCOMPARE(schema.size(), 4);
        QCOMPARE(schema[0].name, QStringLiteral("time"));
        QCOMPARE(schema[0].type, FieldType::String); // "10:03" isn't an int
        QCOMPARE(schema[1].type, FieldType::Integer);
        QCOMPARE(schema[1].hint, FieldHint::Numeric);
        QCOMPARE(schema[2].type, FieldType::Integer);
        // Column literally named "message" carries the Message hint.
        QCOMPARE(schema[3].hint, FieldHint::Message);

        ParsedRow row;
        parser->parseLine(QByteArrayView("11:00,404,0,not found"), row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields[1], QStringLiteral("404"));
    }

    void fromFileHidesHeaderRow()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "m.csv", QByteArray("a,b\n1,2\n"));
        const auto parser = CsvParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);
        QVERIFY(parser->hasMetaLines());
        QVERIFY(!parser->isDataLine(0, QByteArrayView("a,b")));
        QVERIFY(parser->isDataLine(1, QByteArrayView("1,2")));

        // A directly constructed parser was given its schema: the file has
        // no header row to hide.
        const CsvParser direct({ "a", "b" });
        QVERIFY(!direct.hasMetaLines());
        QVERIFY(direct.isDataLine(0, QByteArrayView("1,2")));
    }

    void headerNameFixups()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "h.csv", QByteArray("a,,a,b\n1,2,3,4\n"));
        const auto parser = CsvParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);
        const auto schema = parser->schema();
        QCOMPARE(schema[0].name, QStringLiteral("a"));
        QCOMPARE(schema[1].name, QStringLiteral("Column 2"));
        QCOMPARE(schema[2].name, QStringLiteral("a (2)"));
        QCOMPARE(schema[3].name, QStringLiteral("b"));
    }

    void emptyFileFallsBack()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "e.csv", QByteArray());
        QVERIFY(!CsvParser::fromFile(*o.source, *o.index));
        auto blank = openContent(dir, "b.csv", QByteArray("\ndata\n"));
        QVERIFY(!CsvParser::fromFile(*blank.source, *blank.index));
    }

    void shortAndLongRows()
    {
        const CsvParser parser({ "a", "b", "c" });
        ParsedRow row;
        parser.parseLine(QByteArrayView("1,2"), row);
        QVERIFY(!row.ok); // short: padded
        QCOMPARE(row.fields.size(), 3);
        QCOMPARE(row.fields[2], QString());

        parser.parseLine(QByteArrayView("1,2,3,4,5"), row);
        QVERIFY(!row.ok); // long: overflow joined into the last column
        QCOMPARE(row.fields[2], QStringLiteral("3,4,5"));

        QVERIFY(parser.matchesStructure("x,y,z"));
        QVERIFY(!parser.matchesStructure("x,y"));
    }

    void multiLineQuotedRecordIsDocumentedLimitation()
    {
        // A quoted field with an embedded newline spans two index lines and
        // renders as two separate rows - same behavior the legacy viewer had.
        // The fragments still display (unterminated quote runs to the line
        // end), the record is just split.
        const CsvParser parser({ "a", "b" });
        ParsedRow row;
        parser.parseLine(QByteArrayView("1,\"start of"), row);
        QCOMPARE(row.fields[1], QStringLiteral("start of"));
        parser.parseLine(QByteArrayView("the end\""), row);
        QVERIFY(!row.ok); // fragment has too few fields
        QCOMPARE(row.fields[0], QStringLiteral("the end\""));
    }
};

QTEST_APPLESS_MAIN(tst_CsvParser)
#include "tst_csvparser.moc"
