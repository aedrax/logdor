#include <logdor/DeclarativeParser.h>
#include <logdor/FormatSpec.h>

#include <QTemporaryDir>
#include <QTest>

using namespace logdor;

namespace {

QByteArray syslogSpecJson()
{
    return "{\n"
            "        \"id\": \"syslog-test\",\n"
            "        \"displayName\": \"Syslog Test\",\n"
            "        \"pattern\": \"^(?<time>[A-Z][a-z]{2}\\\\s+\\\\d{1,2} \\\\d{2}:\\\\d{2}:\\\\d{2}) (?<host>\\\\S+) (?<proc>[^\\\\[:]+)(?:\\\\[(?<pid>\\\\d+)\\\\])?: ?(?<msg>.*)$\",\n"
            "        \"fields\": [\n"
            "            { \"name\": \"Time\", \"capture\": \"time\", \"type\": \"datetime\", \"hint\": \"timestamp\" },\n"
            "            { \"name\": \"Host\", \"capture\": \"host\" },\n"
            "            { \"name\": \"Process\", \"capture\": \"proc\" },\n"
            "            { \"name\": \"PID\", \"capture\": \"pid\", \"type\": \"integer\", \"hint\": \"numeric\" },\n"
            "            { \"name\": \"Message\", \"capture\": \"msg\", \"hint\": \"message\" }\n"
            "        ]\n"
            "    }";
}

QByteArray keyValueSpecJson()
{
    return "{\n"
            "        \"id\": \"kv-test\",\n"
            "        \"displayName\": \"KV Test\",\n"
            "        \"pattern\": \"^ts=(?<ts>\\\\S+) level=(?<level>\\\\S+) msg=(?<msg>.*)$\",\n"
            "        \"fields\": [\n"
            "            { \"name\": \"Timestamp\", \"capture\": \"ts\", \"type\": \"datetime\", \"hint\": \"timestamp\" },\n"
            "            { \"name\": \"Level\", \"capture\": \"level\", \"hint\": \"severityname\" },\n"
            "            { \"name\": \"Message\", \"capture\": \"msg\", \"hint\": \"message\" }\n"
            "        ],\n"
            "        \"severity\": {\n"
            "            \"capture\": \"level\",\n"
            "            \"map\": { \"warn\": \"warning\", \"ERROR\": \"error\" },\n"
            "            \"default\": \"info\"\n"
            "        },\n"
            "        \"specificity\": 0.9\n"
            "    }";
}

} // namespace

class tst_FormatSpec : public QObject {
    Q_OBJECT

private slots:
    void parsesValidSpec()
    {
        FormatSpecError error;
        const auto spec = parseFormatSpec(keyValueSpecJson(), "kv.json", &error);
        QVERIFY2(spec.has_value(), qPrintable(error.message));
        QCOMPARE(spec->id, QStringLiteral("kv-test"));
        QCOMPARE(spec->fields.size(), 3);
        QCOMPARE(spec->fields[1].hint, FieldHint::SeverityName);
        QCOMPARE(spec->severityMap.value("warn"), Severity::Warning);
        QCOMPARE(spec->severityMap.value("error"), Severity::Error); // key lowercased
        QCOMPARE(spec->severityDefault, Severity::Info);
        QCOMPARE(spec->specificity, 0.9);
    }

    void declarativeParserGolden()
    {
        FormatSpecError error;
        auto spec = parseFormatSpec(syslogSpecJson(), "s.json", &error);
        QVERIFY(spec);
        const DeclarativeParser parser(std::move(*spec));

        QCOMPARE(parser.schema().size(), 5);
        QCOMPARE(parser.schema()[4].hint, FieldHint::Message);

        ParsedRow row;
        parser.parseLine(QByteArrayView(
            "Jan  5 03:04:05 myhost sshd[4242]: Accepted publickey for root"), row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields[0], QStringLiteral("Jan  5 03:04:05"));
        QCOMPARE(row.fields[1], QStringLiteral("myhost"));
        QCOMPARE(row.fields[2], QStringLiteral("sshd"));
        QCOMPARE(row.fields[3], QStringLiteral("4242"));
        QCOMPARE(row.fields[4], QStringLiteral("Accepted publickey for root"));

        // Optional capture absent.
        parser.parseLine(QByteArrayView("Feb 12 10:00:00 host kernel: oops"), row);
        QVERIFY(row.ok);
        QCOMPARE(row.fields[2], QStringLiteral("kernel"));
        QCOMPARE(row.fields[3], QString());

        // Fallback: raw line lands in the Message column.
        parser.parseLine(QByteArrayView("not a syslog line"), row);
        QVERIFY(!row.ok);
        QCOMPARE(row.fields[4], QStringLiteral("not a syslog line"));
        QVERIFY(!parser.matchesStructure("not a syslog line"));
    }

    void severityMapping()
    {
        FormatSpecError error;
        auto spec = parseFormatSpec(keyValueSpecJson(), "kv.json", &error);
        QVERIFY(spec);
        const DeclarativeParser parser(std::move(*spec));
        QVERIFY(parser.colorsBySeverity());

        ParsedRow row;
        parser.parseLine(QByteArrayView("ts=1 level=WARN msg=x"), row);
        QCOMPARE(row.severity, Severity::Warning); // case-insensitive
        parser.parseLine(QByteArrayView("ts=1 level=weird msg=x"), row);
        QCOMPARE(row.severity, Severity::Info); // default
        parser.parseLine(QByteArrayView("garbage"), row);
        QCOMPARE(row.severity, Severity::None); // fallback rows uncolored
    }

    void validationErrors_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("messagePart");

        QTest::newRow("bad-json") << QByteArray("{ nope") << QString("invalid JSON");
        QTest::newRow("not-object") << QByteArray("[1,2]") << QString("must be an object");
        QTest::newRow("no-id") << QByteArray("{\"displayName\":\"x\"}") << QString("id:");
        QTest::newRow("no-pattern")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\"}") << QString("pattern:");
        QTest::newRow("bad-regex")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\",\"pattern\":\"([\",\n"
            "                \"fields\":[{\"name\":\"M\",\"capture\":\"m\",\"hint\":\"message\"}]}")
            << QString("invalid regex");
        QTest::newRow("no-fields")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\",\"pattern\":\"(?<m>.*)\"}")
            << QString("fields:");
        QTest::newRow("missing-capture")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\",\"pattern\":\"(?<m>.*)\",\n"
            "                \"fields\":[{\"name\":\"M\",\"capture\":\"nope\",\"hint\":\"message\"}]}")
            << QString("named group 'nope' not found");
        QTest::newRow("bad-type")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\",\"pattern\":\"(?<m>.*)\",\n"
            "                \"fields\":[{\"name\":\"M\",\"capture\":\"m\",\"type\":\"number\",\"hint\":\"message\"}]}")
            << QString("unknown value 'number'");
        QTest::newRow("bad-hint")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\",\"pattern\":\"(?<m>.*)\",\n"
            "                \"fields\":[{\"name\":\"M\",\"capture\":\"m\",\"hint\":\"stretch\"}]}")
            << QString("unknown value 'stretch'");
        QTest::newRow("duplicate-name")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\",\"pattern\":\"(?<a>.)(?<m>.*)\",\n"
            "                \"fields\":[{\"name\":\"M\",\"capture\":\"a\",\"hint\":\"message\"},\n"
            "                          {\"name\":\"m\",\"capture\":\"m\"}]}")
            << QString("duplicate field name");
        QTest::newRow("no-message-hint")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\",\"pattern\":\"(?<m>.*)\",\n"
            "                \"fields\":[{\"name\":\"M\",\"capture\":\"m\"}]}")
            << QString("exactly one field");
        QTest::newRow("bad-severity")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\",\"pattern\":\"(?<m>.*)\",\n"
            "                \"fields\":[{\"name\":\"M\",\"capture\":\"m\",\"hint\":\"message\"}],\n"
            "                \"severity\":{\"capture\":\"m\",\"map\":{\"a\":\"loud\"}}}")
            << QString("unknown severity 'loud'");
        QTest::newRow("bad-specificity")
            << QByteArray("{\"id\":\"x\",\"displayName\":\"x\",\"pattern\":\"(?<m>.*)\",\n"
            "                \"fields\":[{\"name\":\"M\",\"capture\":\"m\",\"hint\":\"message\"}],\n"
            "                \"specificity\":1.5}")
            << QString("specificity");
    }

    void validationErrors()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, messagePart);
        FormatSpecError error;
        const auto spec = parseFormatSpec(json, "t.json", &error);
        QVERIFY(!spec.has_value());
        QVERIFY2(error.message.contains(messagePart),
                 qPrintable(QStringLiteral("'%1' !~ '%2'")
                                .arg(error.message, messagePart)));
        QCOMPARE(error.sourcePath, QStringLiteral("t.json"));
    }

    void loadFormatSpecsSkipsBadFilesAndOverrides()
    {
        QTemporaryDir bundled, user;
        auto write = [](const QTemporaryDir& dir, const QString& name,
                        const QByteArray& content) {
            QFile f(dir.filePath(name));
            f.open(QIODevice::WriteOnly);
            f.write(content);
        };
        write(bundled, "a-kv.json", keyValueSpecJson());
        write(bundled, "broken.json", "{ nope");
        // User dir overrides the same id with a different displayName.
        QByteArray overridden = keyValueSpecJson();
        overridden.replace("KV Test", "KV Override");
        write(user, "mine.json", overridden);

        QList<FormatSpecError> errors;
        const auto specs = loadFormatSpecs({ bundled.path(), user.path() }, &errors);
        QCOMPARE(specs.size(), 1);
        QCOMPARE(specs[0].displayName, QStringLiteral("KV Override"));
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors[0].sourcePath.contains("broken.json"));
    }

    void bundledSpecsAreValid()
    {
        // The specs shipped in core/formats must always parse.
        const QString dir = QFINDTESTDATA("../formats");
        QVERIFY(!dir.isEmpty());
        QList<FormatSpecError> errors;
        const auto specs = loadFormatSpecs({ dir }, &errors);
        QVERIFY2(errors.isEmpty(),
                 qPrintable(errors.isEmpty() ? QString()
                                             : errors.first().message));
        QStringList ids;
        for (const auto& spec : specs)
            ids.append(spec.id);
        for (const char* required : { "syslog-rfc3164", "syslog-iso", "keyvalue",
                                      "dpkg", "dmesg", "apt-history", "apt-term",
                                      "cloud-init", "cloud-init-output", "apport",
                                      "xorg", "alternatives" })
            QVERIFY2(ids.contains(QLatin1StringView(required)), required);
    }
};

QTEST_APPLESS_MAIN(tst_FormatSpec)
#include "tst_formatspec.moc"
