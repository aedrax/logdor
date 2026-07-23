#include <logdor/Query.h>

#include <QTest>

using namespace logdor;

namespace {

// Logcat-shaped schema for most tests.
QList<FieldSchema> testSchema()
{
    return {
        { QStringLiteral("Time"), FieldType::DateTime, FieldHint::Timestamp },
        { QStringLiteral("PID"), FieldType::Integer, FieldHint::Numeric },
        { QStringLiteral("TID"), FieldType::Integer, FieldHint::Numeric },
        { QStringLiteral("Level"), FieldType::String, FieldHint::SeverityName },
        { QStringLiteral("Tag"), FieldType::String, FieldHint::Identifier },
        { QStringLiteral("Message"), FieldType::String, FieldHint::Message },
    };
}

struct TestRow {
    QString time, pid, tid, level, tag, message;
    Severity severity;
    QString raw; // the whole line, for free-text terms
};

ColumnSnapshot buildSnapshot(const QList<TestRow>& rows)
{
    ColumnSnapshot snapshot;
    const auto schema = testSchema();
    QList<ColumnData::Builder> builders;
    for (const auto& field : schema)
        builders.emplace_back(field.type);
    auto severity = std::make_shared<std::vector<quint8>>();

    for (const TestRow& row : rows) {
        builders[0].append(row.time, schema[0].type);
        builders[1].append(row.pid, schema[1].type);
        builders[2].append(row.tid, schema[2].type);
        builders[3].append(row.level, schema[3].type);
        builders[4].append(row.tag, schema[4].type);
        builders[5].append(row.message, schema[5].type);
        severity->push_back(quint8(row.severity));
    }
    for (int i = 0; i < builders.size(); ++i)
        snapshot.columns.insert(
            i, std::make_shared<const ColumnData>(std::move(builders[i]).build()));
    snapshot.severity = severity;
    return snapshot;
}

const QList<TestRow> kRows = {
    { "01-01 10:00:00.000", "100", "1", "Error", "WifiService",
      "connection failed hard", Severity::Error,
      "01-01 10:00:00.000 100 1 E WifiService: connection failed hard" },
    { "01-02 11:00:00.000", "200", "2", "Info", "ActivityManager",
      "started ok", Severity::Info,
      "01-02 11:00:00.000 200 2 I ActivityManager: started ok" },
    { "01-03 12:00:00.000", "300", "3", "Warning", "WifiManager",
      "weak signal", Severity::Warning,
      "01-03 12:00:00.000 300 3 W WifiManager: weak signal" },
    { "", "not-a-pid", "", "Unknown", "", "raw fallback line and more",
      Severity::None, "raw fallback line and more" },
};

// Which row indices match a query.
QList<int> matchesOf(const QString& text,
                     Qt::CaseSensitivity cs = Qt::CaseInsensitive,
                     QueryError* err = nullptr)
{
    QueryError localError;
    auto query = CompiledQuery::compile(text, testSchema(), cs, {},
                                        err ? err : &localError);
    if (!query)
        return { -1 }; // sentinel: compile failed
    static const ColumnSnapshot snapshot = buildSnapshot(kRows);
    QList<int> out;
    for (int i = 0; i < kRows.size(); ++i) {
        if (query->evaluate(i, kRows[i].raw.toUtf8(), snapshot))
            out.append(i);
    }
    return out;
}

} // namespace

class tst_Query : public QObject {
    Q_OBJECT

private slots:
    void freeTextMatchesRawLine()
    {
        QCOMPARE(matchesOf("connection"), QList<int>{ 0 });
        QCOMPARE(matchesOf("\"failed hard\""), QList<int>{ 0 });
        QCOMPARE(matchesOf("\"failed  hard\""), QList<int>{}); // exact substring
        QCOMPARE(matchesOf("CONNECTION"), QList<int>{ 0 });    // folded
        QCOMPARE(matchesOf("CONNECTION", Qt::CaseSensitive), QList<int>{});
    }

    void stringFieldSemantics()
    {
        QCOMPARE(matchesOf("tag:wifi"), (QList<int>{ 0, 2 }));      // contains
        QCOMPARE(matchesOf("tag:Wifi*"), (QList<int>{ 0, 2 }));     // wildcard anchored
        QCOMPARE(matchesOf("tag:*Manager"), (QList<int>{ 1, 2 }));  // wildcard
        QCOMPARE(matchesOf("tag=WifiService"), QList<int>{ 0 });    // exact
        QCOMPARE(matchesOf("tag=wifiservice", Qt::CaseSensitive), QList<int>{});
        QCOMPARE(matchesOf("tag!=WifiService"), (QList<int>{ 1, 2, 3 }));
    }

    void integerFieldSemantics()
    {
        QCOMPARE(matchesOf("pid:200"), QList<int>{ 1 });
        QCOMPARE(matchesOf("pid=200"), QList<int>{ 1 });
        QCOMPARE(matchesOf("pid>=200"), (QList<int>{ 1, 2 }));
        QCOMPARE(matchesOf("pid<200"), QList<int>{ 0 }); // invalid row never matches
        QCOMPARE(matchesOf("pid!=100"), (QList<int>{ 1, 2 })); // nor for !=
    }

    void severitySemantics()
    {
        QCOMPARE(matchesOf("level:error"), QList<int>{ 0 });
        QCOMPARE(matchesOf("level:warn"), QList<int>{ 2 }); // alias
        QCOMPARE(matchesOf("level>=warning"), (QList<int>{ 0, 2 }));
        QCOMPARE(matchesOf("level!=info"), (QList<int>{ 0, 2, 3 }));
        QCOMPARE(matchesOf("level:unknown"), QList<int>{ 3 });
        // Unrecognized level names degrade to string matching on the field.
        QCOMPARE(matchesOf("level:rror"), QList<int>{ 0 });
    }

    void dateTimeLexicographic()
    {
        QCOMPARE(matchesOf("time>=\"01-02\""), (QList<int>{ 1, 2 }));
        QCOMPARE(matchesOf("time<\"01-02\""), (QList<int>{ 0, 3 })); // empty sorts first
    }

    void booleanOperatorsAndPrecedence()
    {
        QCOMPARE(matchesOf("tag:wifi level:error"), QList<int>{ 0 }); // implicit AND
        QCOMPARE(matchesOf("tag:wifi AND level:error"), QList<int>{ 0 });
        QCOMPARE(matchesOf("level:error OR level:info"), (QList<int>{ 0, 1 }));
        QCOMPARE(matchesOf("NOT tag:wifi"), (QList<int>{ 1, 3 }));
        // AND binds tighter than OR.
        QCOMPARE(matchesOf("level:error OR level:info tag:wifi"), QList<int>{ 0 });
        QCOMPARE(matchesOf("(level:error OR level:info) tag:wifi"), QList<int>{ 0 });
        QCOMPARE(matchesOf("NOT (tag:wifi OR pid:200)"), QList<int>{ 3 });
        // Keywords inside quotes are text.
        QCOMPARE(matchesOf("\"and more\""), QList<int>{ 3 });
    }

    void fieldNameNormalization()
    {
        const QList<FieldSchema> schema = {
            { QStringLiteral("Remote Host"), FieldType::String, FieldHint::None },
        };
        QueryError error;
        auto query = CompiledQuery::compile(QStringLiteral("remotehost:10.0"),
                                            schema, Qt::CaseInsensitive, {},
                                            &error);
        QVERIFY(query);
        QCOMPARE(query->referencedColumns(), QList<int>{ 0 });
    }

    void quotedFieldValues()
    {
        QCOMPARE(matchesOf("message:\"failed hard\""), QList<int>{ 0 });
        QCOMPARE(matchesOf("tag: WifiService"), QList<int>{ 0 }); // detached value
    }

    void compileErrors_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("messagePart");
        QTest::addColumn<int>("position");

        QTest::newRow("unknown-field") << "foo:bar" << "unknown field 'foo'" << 0;
        QTest::newRow("unknown-field-pos") << "level:error foo:bar"
                                           << "unknown field" << 12;
        QTest::newRow("unbalanced-paren") << "(level:error" << "unbalanced" << 0;
        QTest::newRow("unexpected-rparen") << "level:error)" << "unexpected ')'" << 11;
        QTest::newRow("dangling-and") << "level:error AND" << "after AND" << 15;
        QTest::newRow("dangling-not") << "NOT" << "after NOT" << 0;
        QTest::newRow("non-numeric") << "pid:abc" << "not a number" << 0;
        QTest::newRow("missing-value") << "tag:" << "missing value" << 0;
        QTest::newRow("op-without-field") << ":foo" << "field name before" << 0;
        QTest::newRow("empty") << "   " << "empty query" << 0;
    }

    void compileErrors()
    {
        QFETCH(QString, query);
        QFETCH(QString, messagePart);
        QFETCH(int, position);

        QueryError error;
        auto compiled = CompiledQuery::compile(query, testSchema(),
                                               Qt::CaseInsensitive, {}, &error);
        QVERIFY(!compiled);
        QVERIFY(error.isError());
        QVERIFY2(error.message.contains(messagePart),
                 qPrintable(QStringLiteral("'%1' !~ '%2'")
                                .arg(error.message, messagePart)));
        QCOMPARE(int(error.position), position);
    }

    void allowUnknownFieldsDegradesToTrue()
    {
        QueryError error;
        auto query = CompiledQuery::compile(
            QStringLiteral("nope:x level:error"), testSchema(),
            Qt::CaseInsensitive, QueryOption::AllowUnknownFields, &error);
        QVERIFY(query);
        // Also usable with an EMPTY schema for pure syntax validation.
        auto syntaxOnly = CompiledQuery::compile(
            QStringLiteral("a:1 (b:2 OR c)"), {}, Qt::CaseInsensitive,
            QueryOption::AllowUnknownFields, &error);
        QVERIFY(syntaxOnly);
        auto bad = CompiledQuery::compile(
            QStringLiteral("a:1 AND"), {}, Qt::CaseInsensitive,
            QueryOption::AllowUnknownFields, &error);
        QVERIFY(!bad);
    }

    void referencedColumnsAndSeverity()
    {
        QueryError error;
        auto query = CompiledQuery::compile(
            QStringLiteral("tag:wifi pid>5 level:error \"free text\""),
            testSchema(), Qt::CaseInsensitive, {}, &error);
        QVERIFY(query);
        QCOMPARE(query->referencedColumns(), (QList<int>{ 4, 1 }));
        QVERIFY(query->needsSeverity());

        auto textOnly = CompiledQuery::compile(
            QStringLiteral("just text"), testSchema(), Qt::CaseInsensitive, {},
            &error);
        QVERIFY(textOnly);
        QVERIFY(textOnly->referencedColumns().isEmpty());
        QVERIFY(!textOnly->needsSeverity());
    }
};

QTEST_APPLESS_MAIN(tst_Query)
#include "tst_query.moc"
