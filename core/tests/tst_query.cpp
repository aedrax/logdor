#include <logdor/Query.h>

#include <QDateTime>
#include <QTest>

using namespace logdor;

namespace {

// Deterministic context: UTC, log written 2026 (kRows are January dates).
TimeParseContext testCtx()
{
    return { QTimeZone::utc(), 2026, 12 };
}

qint64 utcMs(int mo, int d, int h, int mi, int s = 0, int ms = 0)
{
    return QDateTime(QDate(2026, mo, d), QTime(h, mi, s, ms), QTimeZone::utc())
        .toMSecsSinceEpoch();
}

// Logcat-shaped schema for most tests.
QList<FieldSchema> testSchema()
{
    return {
        { QStringLiteral("Time"), FieldType::DateTime, FieldHint::Timestamp,
          QStringLiteral("MM-dd HH:mm:ss.zzz") },
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
        builders.emplace_back(field.type,
                              TimestampCodec::fromFormatString(field.timeFormat,
                                                               testCtx()));
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
                                        err ? err : &localError, testCtx());
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

    // kRows times: Jan 1 10:00, Jan 2 11:00, Jan 3 12:00, and one invalid.
    void dateTimeTemporal()
    {
        // Date-only literals span the whole local day.
        QCOMPARE(matchesOf("time=2026-01-02"), QList<int>{ 1 });
        QCOMPARE(matchesOf("time!=2026-01-02"), (QList<int>{ 0, 2 }));
        QCOMPARE(matchesOf("time>=2026-01-02"), (QList<int>{ 1, 2 }));
        QCOMPARE(matchesOf("time<=2026-01-02"), (QList<int>{ 0, 1 })); // thru end
        QCOMPARE(matchesOf("time>2026-01-02"), QList<int>{ 2 });
        // The invalid row (3) never matches - unlike the old byte compare.
        QCOMPARE(matchesOf("time<2026-01-02"), QList<int>{ 0 });

        // Minute granularity: 11:00 is inside "11:00", after it only > it.
        QCOMPARE(matchesOf("time>\"2026-01-02 11:00\""), QList<int>{ 2 });
        QCOMPARE(matchesOf("time>=\"2026-01-02 11:00\""), (QList<int>{ 1, 2 }));
        QCOMPARE(matchesOf("time=\"2026-01-02 11:00\""), QList<int>{ 1 });

        // Cell display text (year-less logcat shape) round-trips.
        QCOMPARE(matchesOf("time=\"01-02 11:00:00.000\""), QList<int>{ 1 });
        QCOMPARE(matchesOf("time>=\"01-02 11:00:00.000\""), (QList<int>{ 1, 2 }));
    }

    void dateTimeTimeOfDay()
    {
        // Bare times match the time of ANY day (rows are on different days).
        QCOMPARE(matchesOf("time<\"11:30\""), (QList<int>{ 0, 1 }));
        QCOMPARE(matchesOf("time>=11:00"), (QList<int>{ 1, 2 }));
        QCOMPARE(matchesOf("time=11:00"), QList<int>{ 1 });
        QCOMPARE(matchesOf("time!=11:00"), (QList<int>{ 0, 2 }));
    }

    void dateTimeStringFallback()
    {
        // Values that are no time literal keep string semantics under
        // ':'/'='/'!='...
        QCOMPARE(matchesOf("time:01-02"), QList<int>{ 1 });      // contains
        QCOMPARE(matchesOf("time:01-0*"), (QList<int>{ 0, 1, 2 })); // wildcard
        QCOMPARE(matchesOf("time=\"\""), QList<int>{ 3 });       // exact empty
        // ...but error under ordering operators.
        QueryError error;
        QCOMPARE(matchesOf("time>banana", Qt::CaseInsensitive, &error),
                 QList<int>{ -1 });
        QVERIFY(error.message.contains("not a date or time"));
    }

    void dateTimeEpochLiteral()
    {
        const qint64 jan2 = utcMs(1, 2, 11, 0); // epoch seconds literal
        QCOMPARE(matchesOf(QStringLiteral("time>=%1").arg(jan2 / 1000)),
                 (QList<int>{ 1, 2 }));
    }

    void atTimePseudoField()
    {
        QueryError error;
        auto query = CompiledQuery::compile(
            QStringLiteral("@time>=2026-01-02"), testSchema(),
            Qt::CaseInsensitive, {}, &error, testCtx());
        QVERIFY2(query, qPrintable(error.message));
        QCOMPARE(query->referencedColumns(), QList<int>{ 0 }); // Time column
        QCOMPARE(matchesOf("@time>=2026-01-02"), (QList<int>{ 1, 2 }));

        // No timestamp column anywhere: a normal unknown-field error, which
        // AllowUnknownFields (the filter-bar tint) degrades to always-true.
        const QList<FieldSchema> plain {
            { QStringLiteral("Message"), FieldType::String, FieldHint::Message },
        };
        QVERIFY(!CompiledQuery::compile(QStringLiteral("@time>=2026-01-02"),
                                        plain, Qt::CaseInsensitive, {}, &error,
                                        testCtx()));
        QVERIFY(error.message.contains("unknown field"));
        QVERIFY(CompiledQuery::compile(QStringLiteral("@time>=2026-01-02"),
                                       plain, Qt::CaseInsensitive,
                                       QueryOption::AllowUnknownFields, &error,
                                       testCtx()));
    }

    void monotonicUptimeField()
    {
        const QList<FieldSchema> schema {
            { QStringLiteral("Time"), FieldType::DateTime, FieldHint::Timestamp,
              QStringLiteral("uptime") },
            { QStringLiteral("Message"), FieldType::String, FieldHint::Message },
        };
        ColumnSnapshot snapshot;
        ColumnData::Builder time(FieldType::DateTime,
                                 TimestampCodec::fromFormatString(
                                     QStringLiteral("uptime"), testCtx()));
        for (const char* v : { "1.500", "12345.678", "" })
            time.append(QString::fromLatin1(v), FieldType::DateTime);
        snapshot.columns.insert(
            0, std::make_shared<const ColumnData>(std::move(time).build()));

        const auto matches = [&](const QString& text) {
            QueryError error;
            auto query = CompiledQuery::compile(text, schema,
                                                Qt::CaseInsensitive, {},
                                                &error, testCtx());
            if (!query)
                return QList<int>{ -1 };
            QList<int> out;
            for (int i = 0; i < 3; ++i) {
                if (query->evaluate(i, "", snapshot))
                    out.append(i);
            }
            return out;
        };
        QCOMPARE(matches("time>100"), QList<int>{ 1 });
        QCOMPARE(matches("time<100"), QList<int>{ 0 }); // invalid row excluded
        QCOMPARE(matches("time=1.5"), QList<int>{ 0 });
        QCOMPARE(matches("time>=1"), (QList<int>{ 0, 1 })); // second granularity
        // Calendar literals are meaningless against uptime.
        QueryError error;
        QVERIFY(!CompiledQuery::compile(QStringLiteral("time>=2026-01-01"),
                                        schema, Qt::CaseInsensitive, {}, &error,
                                        testCtx()));
        QVERIFY(error.message.contains("seconds since boot"));
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

    void quoteQueryValueEscaping()
    {
        QCOMPARE(quoteQueryValue("bareword"), QStringLiteral("bareword"));
        // Op chars and backslashes survive barewords; only the first op in a
        // term splits it, and the value sits after the operator.
        QCOMPARE(quoteQueryValue("a=b"), QStringLiteral("a=b"));
        QCOMPARE(quoteQueryValue("back\\slash"), QStringLiteral("back\\slash"));
        QCOMPARE(quoteQueryValue("has space"), QStringLiteral("\"has space\""));
        QCOMPARE(quoteQueryValue("(paren)"), QStringLiteral("\"(paren)\""));
        QCOMPARE(quoteQueryValue("say \"hi\""),
                 QStringLiteral("\"say \\\"hi\\\"\""));
        QCOMPARE(quoteQueryValue("a \\ \"b\""),
                 QStringLiteral("\"a \\\\ \\\"b\\\"\""));
        QCOMPARE(quoteQueryValue(""), QStringLiteral("\"\""));
        QCOMPARE(quoteQueryValue("bareword", true),
                 QStringLiteral("\"bareword\""));
    }

    void buildQueryTermBasics()
    {
        const FieldSchema tag { QStringLiteral("Tag"), FieldType::String,
                                FieldHint::Identifier };
        QCOMPARE(buildQueryTerm(tag, "WifiService"),
                 QStringLiteral("Tag=WifiService"));
        QCOMPARE(buildQueryTerm(tag, "WifiService", true),
                 QStringLiteral("Tag!=WifiService"));
        QCOMPARE(buildQueryTerm(tag, "foo bar"),
                 QStringLiteral("Tag=\"foo bar\""));
        QCOMPARE(buildQueryTerm(tag, ""), QStringLiteral("Tag=\"\""));

        // Spaces are stripped from the name (resolution ignores them anyway).
        const FieldSchema spaced { QStringLiteral("Status Code"),
                                   FieldType::String, FieldHint::None };
        QCOMPARE(buildQueryTerm(spaced, "404"),
                 QStringLiteral("StatusCode=404"));

        // Names that cannot survive tokenization are inexpressible.
        const FieldSchema opName { QStringLiteral("a=b"), FieldType::String,
                                   FieldHint::None };
        QCOMPARE(buildQueryTerm(opName, "x"), QString());
        const FieldSchema blank { QStringLiteral("  "), FieldType::String,
                                  FieldHint::None };
        QCOMPARE(buildQueryTerm(blank, "x"), QString());
    }

    void buildQueryTermIntegers()
    {
        const FieldSchema pid { QStringLiteral("PID"), FieldType::Integer,
                                FieldHint::Numeric };
        QCOMPARE(buildQueryTerm(pid, "100"), QStringLiteral("PID=100"));
        QCOMPARE(buildQueryTerm(pid, " 100 "), QStringLiteral("PID=100"));
        QCOMPARE(buildQueryTerm(pid, "100", true),
                 QStringLiteral("PID!=100"));
        QCOMPARE(buildQueryTerm(pid, "not-a-pid"), QString());
        QCOMPARE(buildQueryTerm(pid, ""), QString());
    }

    void buildQueryTermOrdering()
    {
        const auto time = testSchema()[0]; // DateTime, logcat format
        // Ordering terms gate on the value parsing as a time literal...
        QCOMPARE(buildQueryTerm(time, "01-02 11:00:00.000", QueryCmp::Ge,
                                testCtx()),
                 QStringLiteral("Time>=\"01-02 11:00:00.000\""));
        QCOMPARE(buildQueryTerm(time, "garbage", QueryCmp::Ge, testCtx()),
                 QString());
        // ...while '='/'!=' keep the permissive string path.
        QCOMPARE(buildQueryTerm(time, "garbage", QueryCmp::Equals, testCtx()),
                 QStringLiteral("Time=garbage"));

        const FieldSchema uptime { QStringLiteral("Time"), FieldType::DateTime,
                                   FieldHint::Timestamp,
                                   QStringLiteral("uptime") };
        QCOMPARE(buildQueryTerm(uptime, "12345.678", QueryCmp::Le, testCtx()),
                 QStringLiteral("Time<=12345.678"));
        QCOMPARE(buildQueryTerm(uptime, "abc", QueryCmp::Le, testCtx()),
                 QString());

        const FieldSchema pid { QStringLiteral("PID"), FieldType::Integer,
                                FieldHint::Numeric };
        QCOMPARE(buildQueryTerm(pid, "100", QueryCmp::Gt),
                 QStringLiteral("PID>100"));
    }

    // Ordering terms built from displayed cell text must compile and behave.
    void builtOrderingTermsRoundTrip()
    {
        const auto time = testSchema()[0];
        const QString after = buildQueryTerm(time, kRows[1].time, QueryCmp::Ge,
                                             testCtx());
        QVERIFY(!after.isEmpty());
        QCOMPARE(matchesOf(after), (QList<int>{ 1, 2 }));
        const QString before = buildQueryTerm(time, kRows[1].time, QueryCmp::Le,
                                              testCtx());
        QCOMPARE(matchesOf(before), (QList<int>{ 0, 1 }));
    }

    // Terms built from a row's displayed values must compile and match that
    // row (and the exclude form must not) - proves the builder agrees with
    // the tokenizer and evaluator, including quoting of spaced values.
    void builtTermsRoundTrip()
    {
        const auto schema = testSchema();
        const TestRow& row = kRows.first();
        const QStringList values = { row.time, row.pid,  row.tid,
                                     row.level, row.tag, row.message };
        for (int i = 0; i < schema.size(); ++i) {
            const QString include = buildQueryTerm(schema[i], values[i]);
            QVERIFY2(!include.isEmpty(), qPrintable(schema[i].name));
            const QList<int> matched = matchesOf(include);
            QVERIFY2(!matched.contains(-1), qPrintable(include)); // compiled
            QVERIFY2(matched.contains(0), qPrintable(include));

            const QString exclude = buildQueryTerm(schema[i], values[i], true);
            const QList<int> unmatched = matchesOf(exclude);
            QVERIFY2(!unmatched.contains(-1), qPrintable(exclude));
            QVERIFY2(!unmatched.contains(0), qPrintable(exclude));
        }
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
