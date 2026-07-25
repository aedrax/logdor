#include <logdor/DeclarativeParser.h>
#include <logdor/FormatSpec.h>
#include <logdor/FormatRegistry.h>
#include <logdor/LineIndexer.h>
#include <logdor/TimeProbe.h>

#include <QDateTime>
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

QByteArray gelfLine(qint64 epochSeconds, int n)
{
    return QByteArrayLiteral("{\"version\":\"1.1\",\"host\":\"h\","
                             "\"short_message\":\"m")
        + QByteArray::number(n) + "\",\"timestamp\":"
        + QByteArray::number(epochSeconds) + ".0}\n";
}

} // namespace

class tst_TimeProbe : public QObject {
    Q_OBJECT

private slots:
    void orderedSpanIsBracketed()
    {
        QTemporaryDir dir;
        QByteArray content;
        constexpr qint64 kBase = 1767225600; // 2026-01-01T00:00:00Z
        for (int i = 0; i < 300; ++i)
            content += gelfLine(kBase + i, i);
        auto o = openContent(dir, "g.log", content);
        const TimeRangeProbe probe
            = probeTimeRange(*o.source, *o.index, *parserById(u"gelf"));
        QVERIFY(probe.valid);
        QVERIFY(!probe.monotonic);
        QCOMPARE(probe.firstMs, kBase * 1000);
        QCOMPARE(probe.lastMs, (kBase + 299) * 1000);
    }

    void jitteredNearOrderStillBracketed()
    {
        // Slightly out-of-order at both ends: min/max over the sample
        // windows still find the true extremes.
        QTemporaryDir dir;
        constexpr qint64 kBase = 1767225600;
        QByteArray content = gelfLine(kBase + 5, 0) + gelfLine(kBase, 1)
            + gelfLine(kBase + 3, 2);
        for (int i = 0; i < 100; ++i)
            content += gelfLine(kBase + 10 + i, 3 + i);
        content += gelfLine(kBase + 200, 200) + gelfLine(kBase + 150, 201);
        auto o = openContent(dir, "j.log", content);
        const TimeRangeProbe probe
            = probeTimeRange(*o.source, *o.index, *parserById(u"gelf"));
        QVERIFY(probe.valid);
        QCOMPARE(probe.firstMs, kBase * 1000);
        QCOMPARE(probe.lastMs, (kBase + 200) * 1000);
    }

    void detectedCodecFromSamples()
    {
        // A datetime field WITHOUT a declared timeFormat forces the probe
        // through codec detection over the head sample; the tiny file also
        // exercises overlapping head/tail windows.
        const QByteArray specJson
            = "{ \"id\": \"probe\", \"displayName\": \"P\","
              " \"pattern\": \"^(?<t>\\\\S+) (?<msg>.*)$\","
              " \"fields\": ["
              "   { \"name\": \"Time\", \"capture\": \"t\","
              "     \"type\": \"datetime\", \"hint\": \"timestamp\" },"
              "   { \"name\": \"Message\", \"capture\": \"msg\","
              "     \"type\": \"string\", \"hint\": \"message\" } ] }";
        auto spec = parseFormatSpec(specJson, QStringLiteral("test"));
        QVERIFY(spec.has_value());
        const DeclarativeParser parser(std::move(*spec));

        QTemporaryDir dir;
        auto o = openContent(dir, "d.log",
                             "2026-03-01T10:00:00Z a\n"
                             "2026-03-01T10:05:00Z b\n");
        const TimeRangeProbe probe
            = probeTimeRange(*o.source, *o.index, parser);
        QVERIFY(probe.valid);
        QVERIFY(!probe.monotonic);
        QCOMPARE(probe.lastMs - probe.firstMs, qint64(5 * 60 * 1000));
    }

    void monotonicUptimeFlagged()
    {
        const QByteArray specJson
            = "{ \"id\": \"up\", \"displayName\": \"Up\","
              " \"pattern\": \"^(?<t>[0-9.]+) (?<msg>.*)$\","
              " \"fields\": ["
              "   { \"name\": \"Time\", \"capture\": \"t\","
              "     \"type\": \"datetime\", \"hint\": \"timestamp\","
              "     \"timeFormat\": \"uptime\" },"
              "   { \"name\": \"Message\", \"capture\": \"msg\","
              "     \"type\": \"string\", \"hint\": \"message\" } ] }";
        auto spec = parseFormatSpec(specJson, QStringLiteral("test"));
        QVERIFY(spec.has_value());
        const DeclarativeParser parser(std::move(*spec));

        QTemporaryDir dir;
        auto o = openContent(dir, "u.log", "1.500 boot\n9.250 later\n");
        const TimeRangeProbe probe
            = probeTimeRange(*o.source, *o.index, parser);
        QVERIFY(probe.valid);
        QVERIFY(probe.monotonic);
        QCOMPARE(probe.firstMs, qint64(1500));
        QCOMPARE(probe.lastMs, qint64(9250));
    }

    void timestamplessFormatIsInvalid()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "p.log", "just text\nno timestamps\n");
        const TimeRangeProbe probe
            = probeTimeRange(*o.source, *o.index, *parserById(u"plaintext"));
        QVERIFY(!probe.valid);
    }

    void emptyFileIsInvalid()
    {
        QTemporaryDir dir;
        auto o = openContent(dir, "e.log", "");
        const TimeRangeProbe probe
            = probeTimeRange(*o.source, *o.index, *parserById(u"gelf"));
        QVERIFY(!probe.valid);
    }
};

QTEST_APPLESS_MAIN(tst_TimeProbe)
#include "tst_timeprobe.moc"
