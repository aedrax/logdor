#include <logdor/TimestampParse.h>

#include <QDateTime>
#include <QTest>

using namespace logdor;

namespace {

qint64 utcMs(int y, int mo, int d, int h, int mi, int s, int ms = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s, ms), QTimeZone::utc())
        .toMSecsSinceEpoch();
}

TimeParseContext utcCtx(int refYear = 0, int refMonth = 12)
{
    return { QTimeZone::utc(), refYear, refMonth };
}

qint64 parsedMs(const TimestampCodec& codec, const QString& text)
{
    qint64 ms = -1;
    if (!codec.parse(text, &ms))
        return -1;
    return ms;
}

} // namespace

class tst_TimestampParse : public QObject {
    Q_OBJECT

private slots:
    void isoWithExplicitZone()
    {
        const auto codec = TimestampCodec::fromFormatString("iso8601", utcCtx());
        QCOMPARE(codec.kind(), TimestampCodec::Kind::Iso8601);
        QVERIFY(!codec.isMonotonic());
        QCOMPARE(parsedMs(codec, "2026-07-01T12:30:45.123Z"),
                 utcMs(2026, 7, 1, 12, 30, 45, 123));
        QCOMPARE(parsedMs(codec, "2026-07-01T14:30:45+02:00"),
                 utcMs(2026, 7, 1, 12, 30, 45));
        QCOMPARE(parsedMs(codec, "2026-07-01 12:30:45"), // space separator
                 utcMs(2026, 7, 1, 12, 30, 45));
        QCOMPARE(parsedMs(codec, "2026-07-01"), utcMs(2026, 7, 1, 0, 0, 0));
        QCOMPARE(parsedMs(codec, "not a date"), -1);
        QCOMPARE(parsedMs(codec, ""), -1);
        QCOMPARE(parsedMs(codec, "2026-13-01T00:00:00"), -1); // bad month
    }

    void isoAssumedZone()
    {
        // Fixed UTC+1: wall noon is 11:00 UTC; an explicit suffix wins.
        const TimeParseContext ctx { QTimeZone(3600), 0, 12 };
        const auto codec = TimestampCodec::fromFormatString("iso8601", ctx);
        QCOMPARE(parsedMs(codec, "2026-07-01 12:00:00"),
                 utcMs(2026, 7, 1, 11, 0, 0));
        QCOMPARE(parsedMs(codec, "2026-07-01T12:00:00Z"),
                 utcMs(2026, 7, 1, 12, 0, 0));
    }

    void logcatYearInference()
    {
        const auto codec = TimestampCodec::fromFormatString(
            "MM-dd HH:mm:ss.zzz", utcCtx(2026, 7));
        QCOMPARE(codec.kind(), TimestampCodec::Kind::Logcat);
        QCOMPARE(parsedMs(codec, "07-24 06:15:02.123"),
                 utcMs(2026, 7, 24, 6, 15, 2, 123));
        // Months after the reference month belong to the previous year.
        QCOMPARE(parsedMs(codec, "12-30 00:00:00.000"),
                 utcMs(2025, 12, 30, 0, 0, 0));
        // Default reference month (12) never rolls over.
        const auto whole = TimestampCodec::fromFormatString(
            "MM-dd HH:mm:ss.zzz", utcCtx(2026));
        QCOMPARE(parsedMs(whole, "12-30 00:00:00.000"),
                 utcMs(2026, 12, 30, 0, 0, 0));
    }

    void klogShapes()
    {
        const auto codec = TimestampCodec::fromFormatString(
            "MMdd HH:mm:ss.zzzzzz", utcCtx(2026, 7));
        QCOMPARE(codec.kind(), TimestampCodec::Kind::Klog);
        // Microsecond fractions truncate to milliseconds.
        QCOMPARE(parsedMs(codec, "0203 12:34:56.789012"),
                 utcMs(2026, 2, 3, 12, 34, 56, 789));
        // klog pads the tid with spaces; the value itself has none, but a
        // fraction-less time is still valid.
        QCOMPARE(parsedMs(codec, "0203 12:34:56"),
                 utcMs(2026, 2, 3, 12, 34, 56));
        // Months after the reference month belong to the previous year.
        QCOMPARE(parsedMs(codec, "1230 00:00:00.000000"),
                 utcMs(2025, 12, 30, 0, 0, 0));
        QCOMPARE(parsedMs(codec, "02-03 12:34:56.789"), -1); // logcat shape
        QCOMPARE(parsedMs(codec, "0203 12:34:56.789012 extra"), -1);
        QCOMPARE(parsedMs(codec, "1350 12:34:56.789012"), -1); // bad month
    }

    void klogAssumedZone()
    {
        // Fixed UTC+1: klog wall times convert through the assumed zone.
        const TimeParseContext ctx { QTimeZone(3600), 2026, 7 };
        const auto codec
            = TimestampCodec::fromFormatString("MMdd HH:mm:ss.zzzzzz", ctx);
        QCOMPARE(parsedMs(codec, "0203 12:00:00.000000"),
                 utcMs(2026, 2, 3, 11, 0, 0));
    }

    void rfc3164Shapes()
    {
        const auto codec = TimestampCodec::fromFormatString(
            "MMM d HH:mm:ss", utcCtx(2026, 7));
        QCOMPARE(codec.kind(), TimestampCodec::Kind::Rfc3164);
        QCOMPARE(parsedMs(codec, "Jul 24 06:15:02"),
                 utcMs(2026, 7, 24, 6, 15, 2));
        QCOMPARE(parsedMs(codec, "Jul  4 06:15:02"), // double-space day
                 utcMs(2026, 7, 4, 6, 15, 2));
        QCOMPARE(parsedMs(codec, "jUl 24 06:15:02"), // case-folded month
                 utcMs(2026, 7, 24, 6, 15, 2));
        QCOMPARE(parsedMs(codec, "Xyz 24 06:15:02"), -1);
    }

    void clfShapes()
    {
        const TimeParseContext ctx { QTimeZone(3600), 0, 12 };
        const auto codec
            = TimestampCodec::fromFormatString("dd/MMM/yyyy:HH:mm:ss", ctx);
        QCOMPARE(codec.kind(), TimestampCodec::Kind::Clf);
        QCOMPARE(parsedMs(codec, "24/Jul/2026:06:15:02"), // assumed +01:00
                 utcMs(2026, 7, 24, 5, 15, 2));
        QCOMPARE(parsedMs(codec, "24/Jul/2026:06:15:02 +0200"), // explicit
                 utcMs(2026, 7, 24, 4, 15, 2));
    }

    void epochKinds()
    {
        const auto secs = TimestampCodec::fromFormatString("epoch-s", utcCtx());
        QCOMPARE(parsedMs(secs, "1721800000"), 1721800000000LL);
        QCOMPARE(parsedMs(secs, "1721800000.5"), 1721800000500LL);
        QCOMPARE(parsedMs(secs, "123"), -1); // outside the plausible range

        const auto ms = TimestampCodec::fromFormatString("epoch-ms", utcCtx());
        QCOMPARE(parsedMs(ms, "1721800000123"), 1721800000123LL);

        const auto us = TimestampCodec::fromFormatString("epoch-us", utcCtx());
        QCOMPARE(parsedMs(us, "1721800000123456"), 1721800000123LL);
    }

    void uptimeIsMonotonic()
    {
        const auto codec = TimestampCodec::fromFormatString("uptime", utcCtx());
        QVERIFY(codec.isMonotonic());
        QCOMPARE(parsedMs(codec, "12345.678901"), 12345678LL);
        QCOMPARE(parsedMs(codec, "0.000000"), 0LL);
        QCOMPARE(parsedMs(codec, "12345"), 12345000LL);
        QCOMPARE(parsedMs(codec, "abc"), -1);
    }

    void qtPatternFormats()
    {
        const auto codec = TimestampCodec::fromFormatString(
            "yyyy/MM/dd HH:mm:ss", utcCtx());
        QCOMPARE(codec.kind(), TimestampCodec::Kind::QtPattern);
        QCOMPARE(parsedMs(codec, "2026/07/01 12:00:00"),
                 utcMs(2026, 7, 1, 12, 0, 0));
        QCOMPARE(parsedMs(codec, "garbage"), -1);

        // Year-less pattern uses the reference date.
        const auto noYear = TimestampCodec::fromFormatString(
            "MM/dd HH:mm", utcCtx(2026, 7));
        QCOMPARE(parsedMs(noYear, "07/24 06:15"),
                 utcMs(2026, 7, 24, 6, 15, 0));
        QCOMPARE(parsedMs(noYear, "12/30 06:15"),
                 utcMs(2025, 12, 30, 6, 15, 0));

        // Zone-less pattern values convert through the assumed zone.
        const TimeParseContext plusOne { QTimeZone(3600), 0, 12 };
        const auto zoned = TimestampCodec::fromFormatString(
            "yyyy/MM/dd HH:mm:ss", plusOne);
        QCOMPARE(parsedMs(zoned, "2026/07/01 12:00:00"),
                 utcMs(2026, 7, 1, 11, 0, 0));
    }

    void emptyFormatIsInvalid()
    {
        const auto codec = TimestampCodec::fromFormatString("", utcCtx());
        QVERIFY(!codec.isValid());
        qint64 ms = 0;
        QVERIFY(!codec.parse(u"2026-07-01", &ms));
    }

    void detectLadder()
    {
        const auto ctx = utcCtx(2026, 7);
        QCOMPARE(TimestampCodec::detect(
                     { "2026-07-01T12:00:00Z", "2026-07-01T13:00:00Z" }, ctx)
                     .kind(),
                 TimestampCodec::Kind::Iso8601);
        QCOMPARE(TimestampCodec::detect({ "24/Jul/2026:06:15:02 +0000" }, ctx)
                     .kind(),
                 TimestampCodec::Kind::Clf);
        QCOMPARE(TimestampCodec::detect({ "07-24 06:15:02.123" }, ctx).kind(),
                 TimestampCodec::Kind::Logcat);
        QCOMPARE(TimestampCodec::detect({ "0203 12:34:56.789012" }, ctx).kind(),
                 TimestampCodec::Kind::Klog);
        QCOMPARE(TimestampCodec::detect({ "Jul 24 06:15:02" }, ctx).kind(),
                 TimestampCodec::Kind::Rfc3164);
        QCOMPARE(TimestampCodec::detect({ "1721800000123" }, ctx).kind(),
                 TimestampCodec::Kind::EpochMillis);
        QCOMPARE(TimestampCodec::detect({ "1721800000" }, ctx).kind(),
                 TimestampCodec::Kind::EpochSeconds);
        QCOMPARE(TimestampCodec::detect({ "12345.678901", "12346.000001" }, ctx)
                     .kind(),
                 TimestampCodec::Kind::UptimeSeconds);
        QVERIFY(!TimestampCodec::detect({ "hello", "world" }, ctx).isValid());
        QVERIFY(!TimestampCodec::detect({}, ctx).isValid());
        // Majority wins over stray garbage.
        QCOMPARE(TimestampCodec::detect({ "2026-07-01T12:00:00Z", "-", "-",
                                          "2026-07-02T12:00:00Z",
                                          "2026-07-03T12:00:00Z" },
                                        ctx)
                     .kind(),
                 TimestampCodec::Kind::Iso8601);
    }

    void literalDateOnly()
    {
        const auto lit = parseTimeLiteral(u"2026-07-01", utcCtx());
        QCOMPARE(lit.kind, TimeLiteral::Kind::Absolute);
        QCOMPARE(lit.lowerMs, utcMs(2026, 7, 1, 0, 0, 0));
        QCOMPARE(lit.upperMs, utcMs(2026, 7, 2, 0, 0, 0));
    }

    void literalGranularities()
    {
        const auto minute = parseTimeLiteral(u"2026-07-01 12:30", utcCtx());
        QCOMPARE(minute.lowerMs, utcMs(2026, 7, 1, 12, 30, 0));
        QCOMPARE(minute.upperMs, utcMs(2026, 7, 1, 12, 31, 0));

        const auto second = parseTimeLiteral(u"2026-07-01 12:30:45", utcCtx());
        QCOMPARE(second.upperMs - second.lowerMs, 1000LL);

        const auto milli
            = parseTimeLiteral(u"2026-07-01 12:30:45.123", utcCtx());
        QCOMPARE(milli.lowerMs, utcMs(2026, 7, 1, 12, 30, 45, 123));
        QCOMPARE(milli.upperMs - milli.lowerMs, 1LL);
    }

    void literalTimeOfDay()
    {
        const auto lit = parseTimeLiteral(u"12:30", utcCtx());
        QCOMPARE(lit.kind, TimeLiteral::Kind::TimeOfDay);
        QCOMPARE(lit.todLowerMs, qint32(12 * 3600000 + 30 * 60000));
        QCOMPARE(lit.todUpperMs - lit.todLowerMs, 60000);

        const auto lastMinute = parseTimeLiteral(u"23:59", utcCtx());
        QCOMPARE(lastMinute.todUpperMs, qint32(86'400'000));
    }

    void literalEpochAndZone()
    {
        const auto epoch = parseTimeLiteral(u"1721800000", utcCtx());
        QCOMPARE(epoch.kind, TimeLiteral::Kind::Absolute);
        QCOMPARE(epoch.lowerMs, 1721800000000LL);
        QCOMPARE(epoch.upperMs, 1721800001000LL);

        // An explicit zone suffix beats the assumed zone.
        const TimeParseContext plusOne { QTimeZone(3600), 0, 12 };
        const auto zoned
            = parseTimeLiteral(u"2026-07-01T12:00:00Z", plusOne);
        QCOMPARE(zoned.lowerMs, utcMs(2026, 7, 1, 12, 0, 0));
        const auto assumed = parseTimeLiteral(u"2026-07-01 12:00:00", plusOne);
        QCOMPARE(assumed.lowerMs, utcMs(2026, 7, 1, 11, 0, 0));
    }

    void literalCellTextRoundTrip()
    {
        // The shapes a viewer displays must parse back as literals.
        const auto ctx = utcCtx(2026, 7);
        const auto logcat = parseTimeLiteral(u"07-24 06:15:02.123", ctx);
        QCOMPARE(logcat.kind, TimeLiteral::Kind::Absolute);
        QCOMPARE(logcat.lowerMs, utcMs(2026, 7, 24, 6, 15, 2, 123));

        const auto rfc = parseTimeLiteral(u"Jul 24 06:15:02", ctx);
        QCOMPARE(rfc.lowerMs, utcMs(2026, 7, 24, 6, 15, 2));

        const auto klog = parseTimeLiteral(u"0203 12:34:56.789012", ctx);
        QCOMPARE(klog.kind, TimeLiteral::Kind::Absolute);
        QCOMPARE(klog.lowerMs, utcMs(2026, 2, 3, 12, 34, 56, 789));

        const auto clf = parseTimeLiteral(u"24/Jul/2026:06:15:02", ctx);
        QCOMPARE(clf.lowerMs, utcMs(2026, 7, 24, 6, 15, 2));
    }

    void literalInvalid()
    {
        QCOMPARE(parseTimeLiteral(u"garbage", utcCtx()).kind,
                 TimeLiteral::Kind::Invalid);
        QCOMPARE(parseTimeLiteral(u"", utcCtx()).kind,
                 TimeLiteral::Kind::Invalid);
        QCOMPARE(parseTimeLiteral(u"123", utcCtx()).kind, // no epoch fits
                 TimeLiteral::Kind::Invalid);
    }

    void dstDayRange()
    {
        const QTimeZone berlin("Europe/Berlin");
        if (!berlin.isValid())
            QSKIP("Europe/Berlin not available");
        // DST starts 2026-03-29 in the EU: that local day is 23 hours long.
        const TimeParseContext ctx { berlin, 0, 12 };
        const auto lit = parseTimeLiteral(u"2026-03-29", ctx);
        QCOMPARE(lit.lowerMs,
                 QDateTime(QDate(2026, 3, 29), QTime(0, 0), berlin)
                     .toMSecsSinceEpoch());
        QCOMPARE(lit.upperMs,
                 QDateTime(QDate(2026, 3, 30), QTime(0, 0), berlin)
                     .toMSecsSinceEpoch());
        QCOMPARE(lit.upperMs - lit.lowerMs, 23LL * 3600000);
    }

    void offsetTables()
    {
        const auto utc = zoneOffsetTable(QTimeZone::utc(), 0,
                                         utcMs(2030, 1, 1, 0, 0, 0));
        QCOMPARE(utc.size(), size_t(1));
        QCOMPARE(utc[0].second, 0);
        QCOMPARE(zoneOffsetAt(utc, utcMs(2026, 7, 1, 0, 0, 0)), 0);
        QCOMPARE(zoneOffsetAt({}, 0), 0);

        const QTimeZone berlin("Europe/Berlin");
        if (!berlin.isValid())
            QSKIP("Europe/Berlin not available");
        const auto table = zoneOffsetTable(berlin, 0,
                                           utcMs(2030, 1, 1, 0, 0, 0));
        QVERIFY(table.size() > 2);
        QCOMPARE(zoneOffsetAt(table, utcMs(2026, 7, 1, 12, 0, 0)),
                 7200000); // CEST
        QCOMPARE(zoneOffsetAt(table, utcMs(2026, 1, 15, 12, 0, 0)),
                 3600000); // CET
    }
};

QTEST_APPLESS_MAIN(tst_TimestampParse)
#include "tst_timestampparse.moc"
