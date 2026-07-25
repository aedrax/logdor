#include <logdor/FileSource.h>
#include <logdor/LineIndexer.h>
#include <logdor/W3CExtendedParser.h>

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

const QByteArray kIisPreamble =
    "#Software: Microsoft Internet Information Services 10.0\n"
    "#Version: 1.0\n"
    "#Date: 2026-07-25 14:32:01\n"
    "#Fields: date time s-ip cs-method cs-uri-stem cs-uri-query s-port "
    "cs-username c-ip cs(User-Agent) sc-status sc-substatus sc-win32-status "
    "time-taken\n";

const QByteArray kIisDataLine =
    "2026-07-25 14:32:01 10.0.0.5 GET /default.htm - 80 - 192.0.2.7 "
    "Mozilla/5.0+(Windows+NT+10.0) 200 0 0 42";

} // namespace

class tst_W3CParser : public QObject {
    Q_OBJECT

private slots:
    void fromFileDerivesSchema()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "u_ex.log", kIisPreamble + kIisDataLine + "\n");
        const auto parser = W3CExtendedParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);

        const auto schema = parser->schema();
        // 14 #Fields tokens, date+time merged into one synthetic column.
        QCOMPARE(schema.size(), 13);
        QCOMPARE(schema[0].name, QStringLiteral("Time"));
        QCOMPARE(schema[0].type, FieldType::DateTime);
        QCOMPARE(schema[0].hint, FieldHint::Timestamp);
        QCOMPARE(schema[0].timeFormat, QStringLiteral("iso8601"));
        QCOMPARE(schema[1].name, QStringLiteral("s-ip"));
        QCOMPARE(schema[1].hint, FieldHint::Identifier);
        QCOMPARE(schema[3].name, QStringLiteral("cs-uri-stem"));
        QCOMPARE(schema[3].hint, FieldHint::Message);
        QCOMPARE(schema[5].name, QStringLiteral("s-port"));
        QCOMPARE(schema[5].type, FieldType::Integer);
        QCOMPARE(schema[7].name, QStringLiteral("c-ip"));
        QCOMPARE(schema[7].hint, FieldHint::Identifier);
        QCOMPARE(schema[9].name, QStringLiteral("sc-status"));
        QCOMPARE(schema[9].type, FieldType::Integer);
        QCOMPARE(schema[9].hint, FieldHint::Numeric);
        QCOMPARE(schema[12].name, QStringLiteral("time-taken"));
        QCOMPARE(schema[12].type, FieldType::Integer);
    }

    void parsesIisDataLine()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "u_ex.log", kIisPreamble + kIisDataLine + "\n");
        const auto parser = W3CExtendedParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);

        ParsedRow row;
        parser->parseLine(QByteArrayView(kIisDataLine), row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields.size(), 13);
        QCOMPARE(row.fields[0], QStringLiteral("2026-07-25 14:32:01")); // merged
        QCOMPARE(row.fields[1], QStringLiteral("10.0.0.5"));
        QCOMPARE(row.fields[2], QStringLiteral("GET"));
        QCOMPARE(row.fields[3], QStringLiteral("/default.htm"));
        QCOMPARE(row.fields[4], QStringLiteral("-")); // '-' stays literal
        QCOMPARE(row.fields[5], QStringLiteral("80"));
        QCOMPARE(row.fields[6], QStringLiteral("-"));
        QCOMPARE(row.fields[7], QStringLiteral("192.0.2.7"));
        QCOMPARE(row.fields[8], QStringLiteral("Mozilla/5.0+(Windows+NT+10.0)"));
        QCOMPARE(row.fields[9], QStringLiteral("200"));
        QCOMPARE(row.fields[12], QStringLiteral("42"));
    }

    void directiveAndBlankLinesFallBack()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "u_ex.log", kIisPreamble + kIisDataLine + "\n");
        const auto parser = W3CExtendedParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);

        ParsedRow row;
        parser->parseLine(QByteArrayView("#Software: Microsoft IIS 10.0"), row);
        QVERIFY(!row.ok);
        QCOMPARE(row.fields.size(), parser->schema().size());
        QCOMPARE(row.fields[3], QStringLiteral("#Software: Microsoft IIS 10.0"));

        parser->parseLine(QByteArrayView(""), row);
        QVERIFY(!row.ok);
        QCOMPARE(row.fields.size(), parser->schema().size());
    }

    void missingFieldsDirectiveReturnsNull()
    {
        QTemporaryDir dir;
        auto plain = openContent(dir, "p.log", QByteArray("just some text\nmore text\n"));
        QVERIFY(!W3CExtendedParser::fromFile(*plain.source, *plain.index));

        auto onlyVersion = openContent(dir, "v.log", QByteArray("#Version: 1.0\n"));
        QVERIFY(!W3CExtendedParser::fromFile(*onlyVersion.source, *onlyVersion.index));

        auto empty = openContent(dir, "e.log", QByteArray());
        QVERIFY(!W3CExtendedParser::fromFile(*empty.source, *empty.index));
    }

    void dataBeforeFieldsDirectiveReturnsNull()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "d.log",
                             kIisDataLine + QByteArray("\n") + kIisPreamble);
        QVERIFY(!W3CExtendedParser::fromFile(*o.source, *o.index));
    }

    void timeWithoutAdjacentDateDoesNotMerge()
    {
        const W3CExtendedParser parser({ QStringLiteral("time"),
                                         QStringLiteral("c-ip"),
                                         QStringLiteral("sc-status") });
        const auto schema = parser.schema();
        QCOMPARE(schema.size(), 3);
        QCOMPARE(schema[0].name, QStringLiteral("time"));
        QCOMPARE(schema[0].type, FieldType::String);
        QVERIFY(schema[0].hint != FieldHint::Timestamp);

        ParsedRow row;
        parser.parseLine(QByteArrayView("14:32:01 192.0.2.7 200"), row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields[0], QStringLiteral("14:32:01"));
    }

    void shortAndLongRows()
    {
        const W3CExtendedParser parser({ QStringLiteral("date"),
                                         QStringLiteral("time"),
                                         QStringLiteral("c-ip"),
                                         QStringLiteral("sc-status") });
        QCOMPARE(parser.schema().size(), 3); // merged

        ParsedRow row;
        parser.parseLine(QByteArrayView("2026-07-25 14:32:01 192.0.2.7"), row);
        QVERIFY(!row.ok); // short: padded
        QCOMPARE(row.fields.size(), 3);
        QCOMPARE(row.fields[0], QStringLiteral("2026-07-25 14:32:01"));
        QCOMPARE(row.fields[2], QString());

        parser.parseLine(QByteArrayView("2026-07-25 14:32:01 192.0.2.7 200 extra tokens"), row);
        QVERIFY(!row.ok); // long: overflow joined into the last column
        QCOMPARE(row.fields[2], QStringLiteral("200 extra tokens"));
    }

    void messageColumnFallback()
    {
        // No cs-uri-* field: the last column carries the Message hint.
        const W3CExtendedParser parser({ QStringLiteral("date"),
                                         QStringLiteral("time"),
                                         QStringLiteral("c-ip"),
                                         QStringLiteral("cs-comment") });
        const auto schema = parser.schema();
        QCOMPARE(schema.last().hint, FieldHint::Message);

        int messageCount = 0;
        for (const FieldSchema& f : schema)
            if (f.hint == FieldHint::Message)
                ++messageCount;
        QCOMPARE(messageCount, 1);
    }

    void matchesStructure()
    {
        const W3CExtendedParser parser({ QStringLiteral("date"),
                                         QStringLiteral("time"),
                                         QStringLiteral("c-ip"),
                                         QStringLiteral("sc-status") });
        QVERIFY(parser.matchesStructure("#Version: 1.0")); // directives count
        QVERIFY(parser.matchesStructure("2026-07-25 14:32:01 192.0.2.7 200"));
        QVERIFY(!parser.matchesStructure("2026-07-25 14:32:01 192.0.2.7"));
        QVERIFY(!parser.matchesStructure(""));
    }

    void headerFixups()
    {
        const W3CExtendedParser parser({ QStringLiteral("c-ip"),
                                         QStringLiteral("c-ip"),
                                         QStringLiteral("sc-status") });
        const auto schema = parser.schema();
        QCOMPARE(schema[0].name, QStringLiteral("c-ip"));
        QCOMPARE(schema[1].name, QStringLiteral("c-ip (2)"));
    }
};

QTEST_APPLESS_MAIN(tst_W3CParser)
#include "tst_w3cparser.moc"
