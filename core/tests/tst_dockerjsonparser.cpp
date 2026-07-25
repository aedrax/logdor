#include <logdor/DockerJsonParser.h>
#include <logdor/FileSource.h>
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
    if (!source)
        return {};
    auto future = buildLineIndex(source);
    future.waitForFinished();
    return { source, future.result().index };
}

} // namespace

class tst_DockerJsonParser : public QObject {
    Q_OBJECT

    const DockerJsonParser m_parser;

private slots:
    void golden_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<QStringList>("expected");
        QTest::addColumn<bool>("ok");

        // Field order: Time, Stream, Message. The trailing newline the
        // json-file driver stores inside "log" is chopped.
        QTest::newRow("stdout")
            << QByteArray("{\"log\":\"Server started on :8080\\n\","
                          "\"stream\":\"stdout\","
                          "\"time\":\"2019-01-01T11:11:11.111111111Z\"}")
            << QStringList{ "2019-01-01T11:11:11.111111111Z", "stdout",
                            "Server started on :8080" }
            << true;
        QTest::newRow("stderr-crlf")
            << QByteArray("{\"log\":\"panic: nil deref\\r\\n\","
                          "\"stream\":\"stderr\","
                          "\"time\":\"2024-03-15T08:22:45.000000001Z\"}")
            << QStringList{ "2024-03-15T08:22:45.000000001Z", "stderr",
                            "panic: nil deref" }
            << true;
        QTest::newRow("key-order-and-attrs-ignored")
            << QByteArray("{\"time\":\"2019-01-01T11:11:11Z\",\"attrs\":"
                          "{\"tag\":\"web\"},\"stream\":\"stdout\","
                          "\"log\":\"hello\"}")
            << QStringList{ "2019-01-01T11:11:11Z", "stdout", "hello" }
            << true;
        QTest::newRow("escapes-and-utf8")
            << QByteArray("{\"log\":\"quote \\\" tab \\t caf\\u00e9\\n\","
                          "\"stream\":\"stdout\","
                          "\"time\":\"2019-01-01T11:11:11Z\"}")
            << QStringList{ "2019-01-01T11:11:11Z", "stdout",
                            QString::fromUtf8("quote \" tab \t caf\xc3\xa9") }
            << true;
        QTest::newRow("empty-log")
            << QByteArray("{\"log\":\"\\n\",\"stream\":\"stdout\","
                          "\"time\":\"2019-01-01T11:11:11Z\"}")
            << QStringList{ "2019-01-01T11:11:11Z", "stdout", "" }
            << true;
        // JSON, but not the json-file shape: required key missing or
        // wrong-typed => structural mismatch, raw line kept.
        QTest::newRow("missing-time")
            << QByteArray("{\"log\":\"x\\n\",\"stream\":\"stdout\"}")
            << QStringList{ "", "",
                            "{\"log\":\"x\\n\",\"stream\":\"stdout\"}" }
            << false;
        QTest::newRow("journalctl-json-is-nonmatch")
            << QByteArray("{\"__REALTIME_TIMESTAMP\":\"1784922714673521\","
                          "\"PRIORITY\":\"6\",\"MESSAGE\":\"Started\"}")
            << QStringList{ "", "",
                            "{\"__REALTIME_TIMESTAMP\":\"1784922714673521\","
                            "\"PRIORITY\":\"6\",\"MESSAGE\":\"Started\"}" }
            << false;
        QTest::newRow("gelf-is-nonmatch")
            << QByteArray("{\"version\":\"1.1\",\"host\":\"example.org\","
                          "\"short_message\":\"A short message\","
                          "\"timestamp\":1385053862.3072,\"level\":1}")
            << QStringList{ "", "",
                            "{\"version\":\"1.1\",\"host\":\"example.org\","
                            "\"short_message\":\"A short message\","
                            "\"timestamp\":1385053862.3072,\"level\":1}" }
            << false;
        QTest::newRow("non-string-log")
            << QByteArray("{\"log\":42,\"stream\":\"stdout\","
                          "\"time\":\"2019-01-01T11:11:11Z\"}")
            << QStringList{ "", "",
                            "{\"log\":42,\"stream\":\"stdout\","
                            "\"time\":\"2019-01-01T11:11:11Z\"}" }
            << false;
        QTest::newRow("non-json-fallback")
            << QByteArray("plain text line")
            << QStringList{ "", "", "plain text line" }
            << false;
        QTest::newRow("empty-line")
            << QByteArray("")
            << QStringList{ "", "", "" }
            << false;
    }

    void golden()
    {
        QFETCH(QByteArray, line);
        QFETCH(QStringList, expected);
        QFETCH(bool, ok);

        QCOMPARE(m_parser.schema().size(), expected.size());

        ParsedRow row;
        m_parser.parseLine(QByteArrayView(line), row);
        QCOMPARE(row.ok, ok);
        QCOMPARE(row.fields.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i)
            QCOMPARE(row.fields[i], expected[i]);
        QCOMPARE(int(row.severity), int(Severity::None)); // never colored
        QCOMPARE(m_parser.matchesStructure(QByteArrayView(line)), ok);
    }

    // A json-file log must beat the generic jsonlines parser (both match
    // 100%; specificity 0.97 vs 0.95 decides)...
    void detectionBeatsJsonLines()
    {
        const QByteArray line
            = "{\"log\":\"GET /healthz 200\\n\",\"stream\":\"stdout\","
              "\"time\":\"2019-01-01T11:11:11.111111111Z\"}";
        QByteArray content;
        for (int i = 0; i < 250; ++i)
            content += line + '\n';

        QTemporaryDir dir;
        auto o = openContent(dir, "docker.log", content);
        QVERIFY(o.source && o.index);
        const auto scores = detectFormat(*o.source, *o.index, builtinParsers());
        QVERIFY(!scores.isEmpty());
        QCOMPARE(scores.front().parserId, QStringLiteral("docker-json"));
    }

    // ...while journald exports keep resolving to jsonlines (docker-json
    // matches zero lines there).
    void journaldStaysJsonLines()
    {
        const QByteArray line
            = "{\"__REALTIME_TIMESTAMP\":\"1784922714673521\",\"PRIORITY\":\"6\","
              "\"SYSLOG_IDENTIFIER\":\"systemd\",\"MESSAGE\":\"Started\"}";
        QByteArray content;
        for (int i = 0; i < 250; ++i)
            content += line + '\n';

        QTemporaryDir dir;
        auto o = openContent(dir, "journal.log", content);
        QVERIFY(o.source && o.index);
        const auto scores = detectFormat(*o.source, *o.index, builtinParsers());
        QVERIFY(!scores.isEmpty());
        QCOMPARE(scores.front().parserId, QStringLiteral("jsonlines"));
    }
};

QTEST_APPLESS_MAIN(tst_DockerJsonParser)
#include "tst_dockerjsonparser.moc"
