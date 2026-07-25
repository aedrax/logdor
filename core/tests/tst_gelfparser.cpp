#include <logdor/FileSource.h>
#include <logdor/FormatRegistry.h>
#include <logdor/GelfParser.h>
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
    if (!source)
        return {};
    auto future = buildLineIndex(source);
    future.waitForFinished();
    return { source, future.result().index };
}

} // namespace

class tst_GelfParser : public QObject {
    Q_OBJECT

    const GelfParser m_parser;

private slots:
    void golden_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<QStringList>("expected");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<int>("severity");

        // Field order: Timestamp, Level, Host, Message, Extra.
        // The GELF 1.1 spec example; level 1 (alert) maps to Fatal, the
        // fractional epoch timestamp is preserved for the epoch-s codec,
        // full_message and _* fields (minus the deprecated _id) gather
        // into Extra as compact JSON (object keys sort alphabetically).
        QTest::newRow("spec-example")
            << QByteArray("{\"version\":\"1.1\",\"host\":\"example.org\","
                          "\"short_message\":\"A short message that helps you "
                          "identify what is going on\","
                          "\"full_message\":\"Backtrace here\\n\\nmore stuff\","
                          "\"timestamp\":1385053862.3072,\"level\":1,"
                          "\"_user_id\":9001,\"_some_info\":\"foo\"}")
            << QStringList{ "1385053862.3072", "Fatal", "example.org",
                            "A short message that helps you identify what is "
                            "going on",
                            "{\"_some_info\":\"foo\",\"_user_id\":9001,"
                            "\"full_message\":\"Backtrace here\\n\\nmore "
                            "stuff\"}" }
            << true << int(Severity::Fatal);
        QTest::newRow("minimal")
            << QByteArray("{\"version\":\"1.1\",\"host\":\"web01\","
                          "\"short_message\":\"disk ok\"}")
            << QStringList{ "", "", "web01", "disk ok", "" }
            << true << int(Severity::None);
        QTest::newRow("level-3-error")
            << QByteArray("{\"version\":\"1.1\",\"host\":\"web01\","
                          "\"short_message\":\"backend failed\",\"level\":3,"
                          "\"timestamp\":1385053862}")
            << QStringList{ "1385053862", "Error", "web01", "backend failed",
                            "" }
            << true << int(Severity::Error);
        QTest::newRow("string-level-name")
            << QByteArray("{\"version\":\"1.1\",\"host\":\"web01\","
                          "\"short_message\":\"x\",\"level\":\"warning\"}")
            << QStringList{ "", "Warning", "web01", "x", "" }
            << true << int(Severity::Warning);
        QTest::newRow("numeric-string-level")
            << QByteArray("{\"version\":\"1.1\",\"host\":\"web01\","
                          "\"short_message\":\"x\",\"level\":\"7\"}")
            << QStringList{ "", "Debug", "web01", "x", "" }
            << true << int(Severity::Debug);
        QTest::newRow("deprecated-_id-dropped")
            << QByteArray("{\"version\":\"1.1\",\"host\":\"web01\","
                          "\"short_message\":\"x\",\"_id\":\"abc\","
                          "\"_req\":\"r1\"}")
            << QStringList{ "", "", "web01", "x", "{\"_req\":\"r1\"}" }
            << true << int(Severity::None);
        // JSON, but not GELF: required key missing => structural mismatch.
        QTest::newRow("docker-json-is-nonmatch")
            << QByteArray("{\"log\":\"hello\\n\",\"stream\":\"stdout\","
                          "\"time\":\"2019-01-01T11:11:11Z\"}")
            << QStringList{ "", "", "",
                            "{\"log\":\"hello\\n\",\"stream\":\"stdout\","
                            "\"time\":\"2019-01-01T11:11:11Z\"}",
                            "" }
            << false << int(Severity::None);
        QTest::newRow("missing-short-message")
            << QByteArray("{\"version\":\"1.1\",\"host\":\"web01\"}")
            << QStringList{ "", "", "",
                            "{\"version\":\"1.1\",\"host\":\"web01\"}", "" }
            << false << int(Severity::None);
        QTest::newRow("truncated-chunk-fallback")
            << QByteArray("{\"version\":\"1.1\",\"host\":\"web01\",\"short_")
            << QStringList{ "", "", "",
                            "{\"version\":\"1.1\",\"host\":\"web01\",\"short_",
                            "" }
            << false << int(Severity::None);
        QTest::newRow("empty-line")
            << QByteArray("")
            << QStringList{ "", "", "", "", "" }
            << false << int(Severity::None);
    }

    void golden()
    {
        QFETCH(QByteArray, line);
        QFETCH(QStringList, expected);
        QFETCH(bool, ok);
        QFETCH(int, severity);

        QCOMPARE(m_parser.schema().size(), expected.size());

        ParsedRow row;
        m_parser.parseLine(QByteArrayView(line), row);
        QCOMPARE(row.ok, ok);
        QCOMPARE(row.fields.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i)
            QCOMPARE(row.fields[i], expected[i]);
        QCOMPARE(int(row.severity), severity);
        QCOMPARE(m_parser.matchesStructure(QByteArrayView(line)), ok);
    }

    // A GELF export must beat both the generic jsonlines parser
    // (specificity) and docker-json (zero matched lines there).
    void detectionBeatsJsonLines()
    {
        const QByteArray line
            = "{\"version\":\"1.1\",\"host\":\"example.org\","
              "\"short_message\":\"A short message\","
              "\"timestamp\":1385053862.3072,\"level\":1}";
        QByteArray content;
        for (int i = 0; i < 250; ++i)
            content += line + '\n';

        QTemporaryDir dir;
        auto o = openContent(dir, "gelf.log", content);
        QVERIFY(o.source && o.index);
        const auto scores = detectFormat(*o.source, *o.index, builtinParsers());
        QVERIFY(!scores.isEmpty());
        QCOMPARE(scores.front().parserId, QStringLiteral("gelf"));
    }
};

QTEST_APPLESS_MAIN(tst_GelfParser)
#include "tst_gelfparser.moc"
