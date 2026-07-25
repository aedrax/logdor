#include <logdor/FileSource.h>
#include <logdor/LineIndexer.h>
#include <logdor/NetLogParser.h>
#include <logdor/TimestampParse.h>

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
    if (!source)
        return {};
    auto future = buildLineIndex(source);
    future.waitForFinished();
    return { source, future.result().index };
}

// A miniature chrome://net-export capture: constants on line 0 exactly as
// Chrome writes it (trailing comma, outer object left open), then one event
// per comma-terminated line.
const QByteArray kConstantsLine
    = "{\"constants\":{\"logEventTypes\":{\"REQUEST_ALIVE\":1,"
      "\"URL_REQUEST_START_JOB\":2,\"HTTP_TRANSACTION_SEND_REQUEST\":3},"
      "\"logSourceType\":{\"NONE\":0,\"URL_REQUEST\":1,\"SOCKET\":2},"
      "\"logEventPhase\":{\"PHASE_NONE\":0,\"PHASE_BEGIN\":1,\"PHASE_END\":2},"
      "\"timeTickOffset\":\"1521580000000\",\"clientInfo\":{}},";

const QByteArray kEventsHeader = "\"events\": [";

QByteArray sampleCapture(bool closed)
{
    QByteArray content = kConstantsLine + '\n' + kEventsHeader + '\n';
    content += "{\"phase\":1,\"source\":{\"id\":42,\"type\":1},"
               "\"time\":\"1000\",\"type\":1},\n";
    content += "{\"params\":{\"load_flags\":0,\"method\":\"GET\"},"
               "\"phase\":1,\"source\":{\"id\":42,\"type\":1},"
               "\"time\":\"1500\",\"type\":2},\n";
    content += "{\"phase\":2,\"source\":{\"id\":42,\"type\":1},"
               "\"time\":\"2000\",\"type\":1},\n";
    if (closed)
        content += "{\"phase\":0,\"source\":{\"id\":7,\"type\":2},"
                   "\"time\":\"2500\",\"type\":99}]}\n";
    else // crashed browser: final line cut mid-object, no closing brackets
        content += "{\"phase\":0,\"source\":{\"id\":7,\"ty";
    return content;
}

} // namespace

class tst_NetLogParser : public QObject {
    Q_OBJECT

private slots:
    void fromFileBuildsParser()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "capture.json", sampleCapture(true));
        QVERIFY(o.source && o.index);
        const auto parser = NetLogParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);
        QCOMPARE(parser->id(), QStringLiteral("netlog"));
        QCOMPARE(parser->schema().size(), int(NetLogParser::FieldCount));

        // Field order: Time, Source, Phase, Event, Params.
        // Tick arithmetic: offset 1521580000000 + 1000 ticks.
        ParsedRow row;
        parser->parseLine(QByteArrayView(
                              "{\"phase\":1,\"source\":{\"id\":42,\"type\":1},"
                              "\"time\":\"1000\",\"type\":1},"),
                          row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields[NetLogParser::Time],
                 QStringLiteral("2018-03-20T21:06:41.000Z"));
        QCOMPARE(row.fields[NetLogParser::Source],
                 QStringLiteral("URL_REQUEST 42"));
        QCOMPARE(row.fields[NetLogParser::Phase], QStringLiteral("+"));
        QCOMPARE(row.fields[NetLogParser::Event],
                 QStringLiteral("REQUEST_ALIVE"));
        QCOMPARE(row.fields[NetLogParser::Params], QString());

        // Params render as compact JSON; PHASE_END shows "-".
        parser->parseLine(QByteArrayView(
                              "{\"params\":{\"load_flags\":0,\"method\":\"GET\"},"
                              "\"phase\":2,\"source\":{\"id\":42,\"type\":1},"
                              "\"time\":\"1500\",\"type\":2},"),
                          row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields[NetLogParser::Phase], QStringLiteral("-"));
        QCOMPARE(row.fields[NetLogParser::Event],
                 QStringLiteral("URL_REQUEST_START_JOB"));
        QCOMPARE(row.fields[NetLogParser::Params],
                 QStringLiteral("{\"load_flags\":0,\"method\":\"GET\"}"));

        // The displayed Time must round-trip through the declared iso8601
        // codec for temporal sort/filter.
        const TimeParseContext ctx { QTimeZone::utc(), 2026, 7 };
        const auto codec = TimestampCodec::fromFormatString(
            parser->schema()[NetLogParser::Time].timeFormat, ctx);
        qint64 ms = 0;
        QVERIFY(codec.parse(QStringLiteral("2018-03-20T21:06:41.000Z"), &ms));
        QCOMPARE(ms, 1521580000000LL + 1000);
    }

    void wrapperAndTruncatedLinesFallBack()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "capture.json", sampleCapture(false));
        QVERIFY(o.source && o.index);
        // A truncated capture (no closing brackets) still yields a parser.
        const auto parser = NetLogParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);

        for (const QByteArray& wrapper :
             { kConstantsLine, kEventsHeader, QByteArray("]}"),
               QByteArray("],\"polledData\":{}}"),
               QByteArray("{\"phase\":0,\"source\":{\"id\":7,\"ty") }) {
            ParsedRow row;
            parser->parseLine(QByteArrayView(wrapper), row);
            QVERIFY2(!row.ok, wrapper.constData());
            QCOMPARE(row.fields[NetLogParser::Params],
                     QString::fromUtf8(wrapper));
            QVERIFY(!parser->matchesStructure(QByteArrayView(wrapper)));
        }
    }

    void unknownTypesRenderNumerically()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "capture.json", sampleCapture(true));
        const auto parser = NetLogParser::fromFile(*o.source, *o.index);
        QVERIFY(parser);

        // Event type 99 and source type 9 are not in this capture's
        // constants (newer Chrome); missing time leaves the cell empty.
        ParsedRow row;
        parser->parseLine(QByteArrayView(
                              "{\"phase\":0,\"source\":{\"id\":7,\"type\":9},"
                              "\"type\":99}"),
                          row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields[NetLogParser::Time], QString());
        QCOMPARE(row.fields[NetLogParser::Source], QStringLiteral("9 7"));
        QCOMPARE(row.fields[NetLogParser::Phase], QString());
        QCOMPARE(row.fields[NetLogParser::Event], QStringLiteral("99"));
    }

    void fromFileRejectsNonCaptures()
    {
        QTemporaryDir dir;
        for (const QByteArray& content :
             { QByteArray(), QByteArray("name,age\nalice,30\n"),
               QByteArray("{\"log\":\"x\\n\",\"stream\":\"stdout\","
                          "\"time\":\"2019-01-01T11:11:11Z\"}\n"),
               QByteArray("{\"constants\":{\"logEventTypes\":{}},\n") }) {
            auto o = openContent(dir,
                                 QStringLiteral("f%1.log").arg(content.size()),
                                 content);
            QVERIFY(o.source && o.index);
            QVERIFY(!NetLogParser::fromFile(*o.source, *o.index));
        }
    }
};

QTEST_APPLESS_MAIN(tst_NetLogParser)
#include "tst_netlogparser.moc"
