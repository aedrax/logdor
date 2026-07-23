#include <logdor/ClfParser.h>
#include <logdor/FormatParser.h>
#include <logdor/LogcatParser.h>
#include <logdor/PlainTextParser.h>

#include <QTest>

using namespace logdor;

class tst_FormatParsers : public QObject {
    Q_OBJECT

private slots:
    //=== PlainText ===========================================================

    void plainTextIdentity()
    {
        PlainTextParser p;
        QCOMPARE(p.schema().size(), 1);
        ParsedRow row;
        p.parseLine(QByteArrayView("hello \xC3\xA9 world"), row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields.size(), 1);
        QCOMPARE(row.fields[0], QString::fromUtf8("hello \xC3\xA9 world"));
        QCOMPARE(row.severity, Severity::None);

        p.parseLine(QByteArrayView(""), row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields[0], QString());
        QVERIFY(p.matchesStructure("anything"));
    }

    //=== Logcat golden table =================================================

    void logcatGolden_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<QStringList>("expected"); // Time,PID,TID,Level,Tag,Message
        QTest::addColumn<int>("severity");
        QTest::addColumn<bool>("ok");

        QTest::newRow("threadtime")
            << QByteArray("01-02 03:04:05.678 1234 5678 W ActivityManager: proc died")
            << QStringList{ "01-02 03:04:05.678", "1234", "5678", "Warning",
                            "ActivityManager", "proc died" }
            << int(Severity::Warning) << true;
        QTest::newRow("loggen-threadtime")
            << QByteArray("07-01 00:00:00.378 28650  4639 F ActivityManager: invalid recovered")
            << QStringList{ "07-01 00:00:00.378", "28650", "4639", "Fatal",
                            "ActivityManager", "invalid recovered" }
            << int(Severity::Fatal) << true;
        QTest::newRow("long")
            << QByteArray("[ 01-02 03:04:05.678 999: 111 E/AudioFlinger ] buffer overflow")
            << QStringList{ "01-02 03:04:05.678", "999", "111", "Error",
                            "AudioFlinger", "buffer overflow" }
            << int(Severity::Error) << true;
        QTest::newRow("time")
            << QByteArray("01-02 03:04:05.678 I/WifiService(  42): scan started")
            << QStringList{ "01-02 03:04:05.678", "42", "", "Info",
                            "WifiService", "scan started" }
            << int(Severity::Info) << true;
        QTest::newRow("brief")
            << QByteArray("D/Zygote( 800): forked child")
            << QStringList{ "", "800", "", "Debug", "Zygote", "forked child" }
            << int(Severity::Debug) << true;
        QTest::newRow("thread")
            << QByteArray("V( 123: 456) verbose detail")
            << QStringList{ "", "123", "456", "Verbose", "", "verbose detail" }
            << int(Severity::Verbose) << true;
        QTest::newRow("tag")
            << QByteArray("W/PackageManager: unknown package")
            << QStringList{ "", "", "", "Warning", "PackageManager", "unknown package" }
            << int(Severity::Warning) << true;
        QTest::newRow("raw-fallback")
            << QByteArray("plain garbage with no format")
            << QStringList{ "", "", "", "Unknown", "", "plain garbage with no format" }
            << int(Severity::None) << false;
        QTest::newRow("empty-message-tag")
            << QByteArray("E/Netd: ")
            << QStringList{ "", "", "", "Error", "Netd", "" }
            << int(Severity::Error) << true;
    }

    void logcatGolden()
    {
        QFETCH(QByteArray, line);
        QFETCH(QStringList, expected);
        QFETCH(int, severity);
        QFETCH(bool, ok);

        LogcatParser p;
        QCOMPARE(p.schema().size(), int(LogcatParser::FieldCount));
        ParsedRow row;
        p.parseLine(QByteArrayView(line), row);
        QCOMPARE(row.ok, ok);
        QCOMPARE(row.fields.size(), int(LogcatParser::FieldCount));
        for (int i = 0; i < expected.size(); ++i)
            QCOMPARE(row.fields[i], expected[i]);
        QCOMPARE(int(row.severity), severity);
        QCOMPARE(p.matchesStructure(QByteArrayView(line)), ok);
    }

    //=== CLF golden table ====================================================

    void clfGolden_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<QStringList>("expected"); // 9 fields
        QTest::addColumn<bool>("ok");

        // Timestamp field intentionally left as a placeholder "@TS@" where the
        // exact rendering depends on the legacy timezone quirk; those rows
        // assert non-empty + format shape instead of exact equality.
        QTest::newRow("common")
            << QByteArray("127.0.0.1 - frank [10/Oct/2000:13:55:36 -0700] "
                          "\"GET /apache_pb.gif HTTP/1.0\" 200 2326")
            << QStringList{ "127.0.0.1", "-", "frank", "@TS@", "GET",
                            "/apache_pb.gif", "HTTP/1.0", "200", "2326" }
            << true;
        QTest::newRow("combined-loggen")
            << QByteArray("192.168.223.127 - - [01/Jul/2026:00:00:00 -0400] "
                          "\"DELETE /api/v1/items HTTP/1.1\" 200 59006 \"-\" \"loggen/1.0\"")
            << QStringList{ "192.168.223.127", "-", "-", "@TS@", "DELETE",
                            "/api/v1/items", "HTTP/1.1", "200", "59006" }
            << true;
        QTest::newRow("dash-bytes")
            << QByteArray("10.0.0.5 - - [10/Oct/2000:13:55:36 -0700] "
                          "\"HEAD / HTTP/1.1\" 304 -")
            << QStringList{ "10.0.0.5", "-", "-", "@TS@", "HEAD", "/",
                            "HTTP/1.1", "304", "0" }
            << true;
        QTest::newRow("short-request")
            << QByteArray("10.0.0.5 - - [10/Oct/2000:13:55:36 -0700] \"-\" 400 0")
            << QStringList{ "10.0.0.5", "-", "-", "@TS@", "", "", "", "400", "0" }
            << true;
        QTest::newRow("garbage")
            << QByteArray("not an access log line at all")
            << QStringList{ "", "", "", "", "", "not an access log line at all",
                            "", "", "" }
            << false;
    }

    void clfGolden()
    {
        QFETCH(QByteArray, line);
        QFETCH(QStringList, expected);
        QFETCH(bool, ok);

        ClfParser p;
        QCOMPARE(p.schema().size(), int(ClfParser::FieldCount));
        ParsedRow row;
        p.parseLine(QByteArrayView(line), row);
        QCOMPARE(row.ok, ok);
        QCOMPARE(row.fields.size(), int(ClfParser::FieldCount));
        for (int i = 0; i < expected.size(); ++i) {
            if (expected[i] == u"@TS@") {
                // Rendered via the legacy quirk; shape must be dd/MMM/yyyy:HH:mm:ss.
                static const QRegularExpression shape(
                    "^\\d{2}/[A-Z][a-z]{2}/\\d{4}:\\d{2}:\\d{2}:\\d{2}$");
                QVERIFY2(shape.match(row.fields[i]).hasMatch(),
                         qPrintable(QStringLiteral("timestamp '%1'").arg(row.fields[i])));
            } else {
                QCOMPARE(row.fields[i], expected[i]);
            }
        }
        QCOMPARE(p.matchesStructure(QByteArrayView(line)), ok);
    }

    //=== Cross-cutting =======================================================

    void parsersAreReentrant()
    {
        // Same ParsedRow reused across parses must not leak previous fields.
        LogcatParser p;
        ParsedRow row;
        p.parseLine(QByteArrayView("W/PackageManager: unknown package"), row);
        p.parseLine(QByteArrayView("junk"), row);
        QCOMPARE(row.fields[LogcatParser::Tag], QString());
        QCOMPARE(row.fields[LogcatParser::Message], QStringLiteral("junk"));
    }
};

QTEST_APPLESS_MAIN(tst_FormatParsers)
#include "tst_formatparsers.moc"
