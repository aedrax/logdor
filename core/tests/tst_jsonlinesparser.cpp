#include <logdor/FileSource.h>
#include <logdor/FormatRegistry.h>
#include <logdor/JsonLinesParser.h>
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

class tst_JsonLinesParser : public QObject {
    Q_OBJECT

    const JsonLinesParser m_parser;

private slots:
    void golden_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<QStringList>("expected");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<int>("severity");

        // Field order: Timestamp, Level, Source, Message.
        // Shape captured from real `journalctl -o json` output; the journal
        // timestamp is microseconds since the epoch, PRIORITY is syslog 0-7.
        QTest::newRow("journalctl")
            << QByteArray("{\"__REALTIME_TIMESTAMP\":\"1784922714673521\","
                          "\"PRIORITY\":\"6\",\"SYSLOG_IDENTIFIER\":\"systemd\","
                          "\"_SYSTEMD_UNIT\":\"init.scope\","
                          "\"MESSAGE\":\"ollama.service: Main process exited\"}")
            << QStringList{ "2026-07-24T19:51:54.673Z", "Info", "systemd",
                            "ollama.service: Main process exited" }
            << true << int(Severity::Info);
        QTest::newRow("journalctl-priority-err")
            << QByteArray("{\"PRIORITY\":\"3\",\"_SYSTEMD_UNIT\":\"cups.service\","
                          "\"MESSAGE\":\"backend failed\"}")
            << QStringList{ "", "Error", "cups.service", "backend failed" }
            << true << int(Severity::Error);
        QTest::newRow("pino-style")
            << QByteArray("{\"level\":\"warn\",\"time\":1784922714673,"
                          "\"name\":\"api\",\"msg\":\"slow request\"}")
            << QStringList{ "1784922714673", "Warning", "api", "slow request" }
            << true << int(Severity::Warning);
        QTest::newRow("at-timestamp")
            << QByteArray("{\"@timestamp\":\"2026-07-24T14:38:29Z\","
                          "\"level\":\"error\",\"message\":\"Connection timeout\","
                          "\"service\":\"database\"}")
            << QStringList{ "2026-07-24T14:38:29Z", "Error", "",
                            "Connection timeout" }
            << true << int(Severity::Error);
        QTest::newRow("unknown-level-string-shown-raw")
            << QByteArray("{\"level\":\"noise\",\"message\":\"hello\"}")
            << QStringList{ "", "noise", "", "hello" }
            << true << int(Severity::None);
        QTest::newRow("object-without-message-keeps-raw")
            << QByteArray("{\"event\":\"login\",\"user\":\"root\"}")
            << QStringList{ "", "", "",
                            "{\"event\":\"login\",\"user\":\"root\"}" }
            << true << int(Severity::None);
        QTest::newRow("non-json-fallback")
            << QByteArray("Jul 24 14:30:01 host CRON[1]: plain text")
            << QStringList{ "", "", "",
                            "Jul 24 14:30:01 host CRON[1]: plain text" }
            << false << int(Severity::None);
        QTest::newRow("json-array-is-nonmatch")
            << QByteArray("[1,2]")
            << QStringList{ "", "", "", "[1,2]" }
            << false << int(Severity::None);
        QTest::newRow("json-scalar-is-nonmatch")
            << QByteArray("123")
            << QStringList{ "", "", "", "123" }
            << false << int(Severity::None);
        QTest::newRow("empty-line")
            << QByteArray("")
            << QStringList{ "", "", "", "" }
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

    void prioritySweep_data()
    {
        QTest::addColumn<int>("priority");
        QTest::addColumn<int>("severity");

        QTest::newRow("0-emerg") << 0 << int(Severity::Fatal);
        QTest::newRow("1-alert") << 1 << int(Severity::Fatal);
        QTest::newRow("2-crit") << 2 << int(Severity::Fatal);
        QTest::newRow("3-err") << 3 << int(Severity::Error);
        QTest::newRow("4-warning") << 4 << int(Severity::Warning);
        QTest::newRow("5-notice") << 5 << int(Severity::Info);
        QTest::newRow("6-info") << 6 << int(Severity::Info);
        QTest::newRow("7-debug") << 7 << int(Severity::Debug);
        QTest::newRow("out-of-range") << 42 << int(Severity::None);
    }

    void prioritySweep()
    {
        QFETCH(int, priority);
        QFETCH(int, severity);

        const QByteArray line = "{\"PRIORITY\":\"" + QByteArray::number(priority)
            + "\",\"MESSAGE\":\"x\"}";
        ParsedRow row;
        m_parser.parseLine(QByteArrayView(line), row);
        QVERIFY(row.ok);
        QCOMPARE(int(row.severity), severity);
    }

    void detection()
    {
        const QByteArray line =
            "{\"__REALTIME_TIMESTAMP\":\"1784922714673521\",\"PRIORITY\":\"6\","
            "\"SYSLOG_IDENTIFIER\":\"systemd\",\"MESSAGE\":\"Started session\"}";
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

QTEST_APPLESS_MAIN(tst_JsonLinesParser)
#include "tst_jsonlinesparser.moc"
