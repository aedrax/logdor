#include "logdor/TimestampParse.h"

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <algorithm>
#include <cstring>

namespace logdor {

namespace {

constexpr qint64 kMsPerDay = 86'400'000;

// Epoch magnitude gates, shared by the codec kinds, detection, and literal
// parsing: seconds in [1e8, 1e11) is 1973..5138, millis/micros scaled.
constexpr qint64 kEpochSecMin = 100'000'000;
constexpr qint64 kEpochSecMax = 100'000'000'000;

// Days since 1970-01-01 of a proleptic Gregorian date (Hinnant's algorithm).
qint64 daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const qint64 era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153u * unsigned(m > 2 ? m - 3 : m + 9) + 2) / 5
        + unsigned(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + qint64(doe) - 719468;
}

qint64 wallToNaiveMs(int y, int mo, int d, int h, int mi, int s, int ms)
{
    return daysFromCivil(y, mo, d) * kMsPerDay
        + ((qint64(h) * 60 + mi) * 60 + s) * 1000 + ms;
}

// A wall-clock reading plus what the text did (not) say about it.
struct Wall {
    int year = 1970, month = 1, day = 1;
    int hour = 0, minute = 0, second = 0, msec = 0;
    int offsetSec = 0;
    bool hasOffset = false;   // text carried an explicit UTC offset
    bool hasYear = true;      // false => infer from the reference date
    qint64 granularityMs = 1; // literal ranges: day/minute/second/fraction
};

bool takeInt(QStringView s, qsizetype& pos, int minDigits, int maxDigits,
             int* out)
{
    int v = 0, n = 0;
    while (n < maxDigits && pos + n < s.size() && s[pos + n].isDigit()) {
        v = v * 10 + s[pos + n].digitValue();
        ++n;
    }
    if (n < minDigits)
        return false;
    pos += n;
    *out = v;
    return true;
}

bool takeChar(QStringView s, qsizetype& pos, char16_t c)
{
    if (pos < s.size() && s[pos] == c) {
        ++pos;
        return true;
    }
    return false;
}

bool takeSpaces(QStringView s, qsizetype& pos) // one or more
{
    const qsizetype start = pos;
    while (pos < s.size() && s[pos] == u' ')
        ++pos;
    return pos > start;
}

// ".frac" => milliseconds (first three digits) + the granularity it implies.
// ISO 8601 permits a comma decimal separator (cloud-init, apport, ...).
bool takeFraction(QStringView s, qsizetype& pos, int* msecOut, qint64* granOut)
{
    const qsizetype start = pos;
    if (!takeChar(s, pos, u'.') && !takeChar(s, pos, u','))
        return false;
    int ms = 0, n = 0;
    while (pos < s.size() && s[pos].isDigit()) {
        if (n < 3)
            ms = ms * 10 + s[pos].digitValue();
        ++n;
        ++pos;
    }
    if (n == 0) {
        pos = start;
        return false;
    }
    for (int i = n; i < 3; ++i)
        ms *= 10;
    *msecOut = ms;
    *granOut = n >= 3 ? 1 : (n == 2 ? 10 : 100);
    return true;
}

// HH:MM[:SS[.frac]]
bool takeTime(QStringView s, qsizetype& pos, Wall* w)
{
    if (!takeInt(s, pos, 1, 2, &w->hour) || !takeChar(s, pos, u':')
        || !takeInt(s, pos, 2, 2, &w->minute))
        return false;
    w->granularityMs = 60'000;
    if (takeChar(s, pos, u':')) {
        if (!takeInt(s, pos, 2, 2, &w->second))
            return false;
        w->granularityMs = 1000;
        qint64 gran = 0;
        if (takeFraction(s, pos, &w->msec, &gran))
            w->granularityMs = gran;
    }
    return w->hour < 24 && w->minute < 60 && w->second <= 60;
}

// Z | +hh | +hh:mm | +hhmm (and '-' variants)
bool takeOffset(QStringView s, qsizetype& pos, Wall* w)
{
    if (pos >= s.size())
        return false;
    if (s[pos] == u'Z' || s[pos] == u'z') {
        ++pos;
        w->offsetSec = 0;
        w->hasOffset = true;
        return true;
    }
    const bool neg = s[pos] == u'-';
    if (!neg && s[pos] != u'+')
        return false;
    qsizetype p = pos + 1;
    int hh = 0, mm = 0;
    if (!takeInt(s, p, 2, 2, &hh))
        return false;
    if (takeChar(s, p, u':')) {
        if (!takeInt(s, p, 2, 2, &mm))
            return false;
    } else {
        takeInt(s, p, 2, 2, &mm); // "+0200"; absent => whole-hour offset
    }
    if (hh > 18 || mm > 59)
        return false;
    w->offsetSec = (neg ? -1 : 1) * (hh * 3600 + mm * 60);
    w->hasOffset = true;
    pos = p;
    return true;
}

int monthFromName(QStringView s) // three chars, case-insensitive; 0 = no match
{
    static constexpr const char* kNames[] = { "jan", "feb", "mar", "apr",
                                              "may", "jun", "jul", "aug",
                                              "sep", "oct", "nov", "dec" };
    if (s.size() != 3)
        return 0;
    char buf[3];
    for (int i = 0; i < 3; ++i) {
        const char16_t c = s[i].unicode();
        if (c > 127)
            return 0;
        buf[i] = c >= 'A' && c <= 'Z' ? char(c + 32) : char(c);
    }
    for (int m = 0; m < 12; ++m) {
        if (std::memcmp(buf, kNames[m], 3) == 0)
            return m + 1;
    }
    return 0;
}

// YYYY-MM-DD[( |T)HH:MM[:SS[.frac]][offset]]; date-only => day granularity.
bool parseIsoWall(QStringView s, Wall* w)
{
    qsizetype pos = 0;
    if (!takeInt(s, pos, 4, 4, &w->year) || !takeChar(s, pos, u'-')
        || !takeInt(s, pos, 2, 2, &w->month) || !takeChar(s, pos, u'-')
        || !takeInt(s, pos, 2, 2, &w->day))
        return false;
    if (pos == s.size()) {
        w->granularityMs = kMsPerDay;
        return true;
    }
    if (!(takeChar(s, pos, u'T') || takeChar(s, pos, u't')
          || takeChar(s, pos, u' ')))
        return false;
    if (!takeTime(s, pos, w))
        return false;
    takeOffset(s, pos, w);
    return pos == s.size();
}

// MM-DD HH:MM[:SS[.frac]] (year-less)
bool parseLogcatWall(QStringView s, Wall* w)
{
    qsizetype pos = 0;
    if (!takeInt(s, pos, 2, 2, &w->month) || !takeChar(s, pos, u'-')
        || !takeInt(s, pos, 2, 2, &w->day) || !takeSpaces(s, pos))
        return false;
    if (!takeTime(s, pos, w))
        return false;
    w->hasYear = false;
    return pos == s.size();
}

// MMDD HH:MM:SS[.frac] (year-less; klog/glog - fraction usually 6 digits,
// truncated to ms by takeFraction)
bool parseKlogWall(QStringView s, Wall* w)
{
    qsizetype pos = 0;
    if (!takeInt(s, pos, 2, 2, &w->month) || !takeInt(s, pos, 2, 2, &w->day)
        || !takeSpaces(s, pos))
        return false;
    if (!takeTime(s, pos, w))
        return false;
    w->hasYear = false;
    return pos == s.size();
}

// MMM [d]d HH:MM:SS (year-less; the classic "Jul  4" double space tolerated)
bool parseRfc3164Wall(QStringView s, Wall* w)
{
    if (s.size() < 3)
        return false;
    w->month = monthFromName(s.left(3));
    if (w->month == 0)
        return false;
    qsizetype pos = 3;
    if (!takeSpaces(s, pos) || !takeInt(s, pos, 1, 2, &w->day)
        || !takeSpaces(s, pos))
        return false;
    if (!takeTime(s, pos, w))
        return false;
    w->hasYear = false;
    return pos == s.size();
}

// dd/MMM/yyyy:HH:MM:SS[ +0200]
bool parseClfWall(QStringView s, Wall* w)
{
    qsizetype pos = 0;
    if (!takeInt(s, pos, 1, 2, &w->day) || !takeChar(s, pos, u'/'))
        return false;
    if (pos + 3 > s.size())
        return false;
    w->month = monthFromName(s.mid(pos, 3));
    if (w->month == 0)
        return false;
    pos += 3;
    if (!takeChar(s, pos, u'/') || !takeInt(s, pos, 4, 4, &w->year)
        || !takeChar(s, pos, u':') || !takeTime(s, pos, w))
        return false;
    if (takeSpaces(s, pos) && !takeOffset(s, pos, w))
        return false;
    return pos == s.size();
}

bool parseAllDigits(QStringView s, qint64* out)
{
    if (s.isEmpty() || s.size() > 18)
        return false;
    qint64 v = 0;
    for (QChar c : s) {
        if (!c.isDigit())
            return false;
        v = v * 10 + c.digitValue();
    }
    *out = v;
    return true;
}

// "seconds[.frac]" => milliseconds.
bool parseSecondsToMs(QStringView s, qint64* out)
{
    const qsizetype dot = s.indexOf(u'.');
    qint64 sec = 0;
    if (!parseAllDigits(dot < 0 ? s : s.left(dot), &sec))
        return false;
    int ms = 0;
    if (dot >= 0) {
        const QStringView frac = s.mid(dot + 1);
        if (frac.isEmpty())
            return false;
        int n = 0;
        for (QChar c : frac) {
            if (!c.isDigit())
                return false;
            if (n < 3)
                ms = ms * 10 + c.digitValue();
            ++n;
        }
        for (int i = qMin(n, 3); i < 3; ++i)
            ms *= 10;
    }
    *out = sec * 1000 + ms;
    return true;
}

} // namespace

//=== Zone offset tables ======================================================

std::vector<std::pair<qint64, qint32>> zoneOffsetTable(const QTimeZone& zone,
                                                       qint64 fromUtcMs,
                                                       qint64 toUtcMs)
{
    std::vector<std::pair<qint64, qint32>> table;
    if (!zone.isValid())
        return table;
    const QDateTime from
        = QDateTime::fromMSecsSinceEpoch(fromUtcMs, QTimeZone::utc());
    table.emplace_back(fromUtcMs, qint32(zone.offsetFromUtc(from)) * 1000);
    if (!zone.hasTransitions())
        return table;
    QTimeZone::OffsetData tr = zone.nextTransition(from);
    // ~2 transitions a year; the cap is only a runaway guard.
    for (int i = 0; i < 8192 && tr.atUtc.isValid(); ++i) {
        const qint64 at = tr.atUtc.toMSecsSinceEpoch();
        if (at > toUtcMs)
            break;
        table.emplace_back(at, qint32(tr.offsetFromUtc) * 1000);
        tr = zone.nextTransition(tr.atUtc);
    }
    return table;
}

qint32 zoneOffsetAt(const std::vector<std::pair<qint64, qint32>>& table,
                    qint64 utcMs)
{
    if (table.empty())
        return 0;
    auto it = std::upper_bound(
        table.begin(), table.end(), utcMs,
        [](qint64 v, const std::pair<qint64, qint32>& e) { return v < e.first; });
    if (it != table.begin())
        --it;
    return it->second;
}

//=== TimestampCodec ==========================================================

TimestampCodec TimestampCodec::make(Kind kind, const QString& pattern,
                                    const TimeParseContext& ctx)
{
    TimestampCodec c;
    c.m_kind = kind;
    const QDate today = QDate::currentDate();
    c.m_refYear = ctx.referenceYear > 0 ? ctx.referenceYear : today.year();
    c.m_refMonth = ctx.referenceMonth;
    if (kind == Kind::QtPattern) {
        c.m_pattern = pattern;
        c.m_patternHasYear = pattern.contains(u'y');
        c.m_patternHasZone = pattern.contains(u't');
    }
    // The offset table is only needed by kinds that read wall times without
    // an explicit offset in the text; for UTC the identity fallback is exact.
    const bool needsZone = kind == Kind::Iso8601 || kind == Kind::Rfc3164
        || kind == Kind::Logcat || kind == Kind::Klog || kind == Kind::Clf
        || (kind == Kind::QtPattern && !c.m_patternHasZone);
    if (needsZone && ctx.assumedZone.isValid()
        && ctx.assumedZone != QTimeZone::utc()) {
        const int lastYear = std::max(c.m_refYear, today.year()) + 2;
        c.m_offsets = std::make_shared<
            const std::vector<std::pair<qint64, qint32>>>(zoneOffsetTable(
            ctx.assumedZone, 0, wallToNaiveMs(lastYear, 1, 1, 0, 0, 0, 0)));
    }
    return c;
}

TimestampCodec TimestampCodec::fromFormatString(const QString& timeFormat,
                                                const TimeParseContext& ctx)
{
    const QString fmt = timeFormat.trimmed();
    if (fmt.isEmpty())
        return {};
    const QString lower = fmt.toLower();
    Kind kind = Kind::QtPattern;
    if (lower == QLatin1String("iso8601"))
        kind = Kind::Iso8601;
    else if (lower == QLatin1String("epoch-s"))
        kind = Kind::EpochSeconds;
    else if (lower == QLatin1String("epoch-ms"))
        kind = Kind::EpochMillis;
    else if (lower == QLatin1String("epoch-us"))
        kind = Kind::EpochMicros;
    else if (lower == QLatin1String("uptime"))
        kind = Kind::UptimeSeconds;
    else if (fmt == QLatin1String("MM-dd HH:mm:ss.zzz"))
        kind = Kind::Logcat;
    else if (fmt == QLatin1String("MMdd HH:mm:ss.zzzzzz"))
        kind = Kind::Klog;
    else if (fmt == QLatin1String("dd/MMM/yyyy:HH:mm:ss"))
        kind = Kind::Clf;
    else if (fmt == QLatin1String("MMM d HH:mm:ss"))
        kind = Kind::Rfc3164;
    return make(kind, fmt, ctx);
}

TimestampCodec TimestampCodec::detect(const QStringList& samples,
                                      const TimeParseContext& ctx)
{
    if (samples.isEmpty())
        return {};
    // Specificity order; the first kind wins ties.
    static constexpr Kind kLadder[] = {
        Kind::Iso8601,     Kind::Clf,         Kind::Logcat,
        Kind::Klog,        Kind::Rfc3164,     Kind::EpochMicros,
        Kind::EpochMillis, Kind::EpochSeconds, Kind::UptimeSeconds,
    };
    TimestampCodec best;
    qsizetype bestCount = 0;
    for (Kind kind : kLadder) {
        const TimestampCodec candidate = make(kind, QString(), ctx);
        qsizetype count = 0;
        qint64 ms = 0;
        for (const QString& sample : samples) {
            // A fraction-less integer is not evidence of an uptime column;
            // leave those to the epoch kinds.
            if (kind == Kind::UptimeSeconds && !sample.contains(u'.'))
                continue;
            if (candidate.parse(sample, &ms))
                ++count;
        }
        if (count > bestCount) {
            bestCount = count;
            best = candidate;
        }
    }
    return best;
}

qint64 TimestampCodec::localToUtc(qint64 naiveMs) const
{
    if (!m_offsets || m_offsets->empty())
        return naiveMs;
    // Classic two-step fixup: guess the offset as if the wall time were UTC,
    // then refine once. DST-ambiguous wall times resolve deterministically.
    const qint32 off0 = zoneOffsetAt(*m_offsets, naiveMs);
    const qint32 off1 = zoneOffsetAt(*m_offsets, naiveMs - off0);
    return naiveMs - off1;
}

bool TimestampCodec::parse(QStringView text, qint64* msOut) const
{
    text = text.trimmed();
    if (text.isEmpty())
        return false;

    Wall w;
    switch (m_kind) {
    case Kind::Invalid:
        return false;
    case Kind::EpochSeconds: {
        qint64 ms = 0;
        if (!parseSecondsToMs(text, &ms) || ms < kEpochSecMin * 1000
            || ms >= kEpochSecMax * 1000)
            return false;
        *msOut = ms;
        return true;
    }
    case Kind::EpochMillis: {
        qint64 v = 0;
        if (!parseAllDigits(text, &v) || v < kEpochSecMin * 1000
            || v >= kEpochSecMax * 1000)
            return false;
        *msOut = v;
        return true;
    }
    case Kind::EpochMicros: {
        qint64 v = 0;
        if (!parseAllDigits(text, &v) || v < kEpochSecMin * 1'000'000
            || v >= kEpochSecMax * 1'000'000)
            return false;
        *msOut = v / 1000;
        return true;
    }
    case Kind::UptimeSeconds:
        return parseSecondsToMs(text, msOut);
    case Kind::Iso8601:
        if (!parseIsoWall(text, &w))
            return false;
        break;
    case Kind::Rfc3164:
        if (!parseRfc3164Wall(text, &w))
            return false;
        break;
    case Kind::Logcat:
        if (!parseLogcatWall(text, &w))
            return false;
        break;
    case Kind::Klog:
        if (!parseKlogWall(text, &w))
            return false;
        break;
    case Kind::Clf:
        if (!parseClfWall(text, &w))
            return false;
        break;
    case Kind::QtPattern: {
        const QDateTime dt
            = QDateTime::fromString(text.toString().simplified(), m_pattern);
        if (!dt.isValid())
            return false;
        if (m_patternHasZone) {
            *msOut = dt.toMSecsSinceEpoch();
            return true;
        }
        // Ignore the QDateTime's own (local) spec: conversion to UTC goes
        // through the assumed zone's offset table below.
        const QDate d = dt.date();
        const QTime t = dt.time();
        w.year = d.year();
        w.month = d.month();
        w.day = d.day();
        w.hour = t.hour();
        w.minute = t.minute();
        w.second = t.second();
        w.msec = t.msec();
        w.hasYear = m_patternHasYear;
        break;
    }
    }

    if (!w.hasYear)
        w.year = inferredYear(w.month);
    if (!QDate::isValid(w.year, w.month, w.day))
        return false;
    const qint64 naive = wallToNaiveMs(w.year, w.month, w.day, w.hour,
                                       w.minute, w.second, w.msec);
    *msOut = w.hasOffset ? naive - qint64(w.offsetSec) * 1000
                         : localToUtc(naive);
    return true;
}

//=== Query literals ==========================================================

TimeLiteral parseTimeLiteral(QStringView text, const TimeParseContext& ctx)
{
    TimeLiteral lit;
    text = text.trimmed();
    if (text.isEmpty())
        return lit;

    // Bare time of day: HH:MM[:SS[.frac]].
    {
        Wall w;
        qsizetype pos = 0;
        if (takeTime(text, pos, &w) && pos == text.size()) {
            const qint64 tod
                = ((qint64(w.hour) * 60 + w.minute) * 60 + w.second) * 1000
                + w.msec;
            lit.kind = TimeLiteral::Kind::TimeOfDay;
            lit.todLowerMs = qint32(tod);
            lit.todUpperMs
                = qint32(std::min(tod + w.granularityMs, kMsPerDay));
            return lit;
        }
    }

    // Bare epoch numbers; magnitude decides the unit.
    {
        qint64 v = 0;
        if (parseAllDigits(text, &v)) {
            qint64 lower = -1, gran = 0;
            if (v >= kEpochSecMin && v < kEpochSecMax) {
                lower = v * 1000;
                gran = 1000;
            } else if (v >= kEpochSecMin * 1000 && v < kEpochSecMax * 1000) {
                lower = v;
                gran = 1;
            } else if (v >= kEpochSecMin * 1'000'000
                       && v < kEpochSecMax * 1'000'000) {
                lower = v / 1000;
                gran = 1;
            }
            if (lower >= 0) {
                lit.kind = TimeLiteral::Kind::Absolute;
                lit.lowerMs = lower;
                lit.upperMs = lower + gran;
            }
            return lit; // digits that fit no epoch range stay Invalid
        }
    }

    // Absolute wall-clock shapes, most specific first - the same shapes the
    // detection ladder accepts, so cell display text round-trips.
    Wall w;
    if (!parseIsoWall(text, &w) && !parseClfWall(text, &w)
        && !parseLogcatWall(text, &w) && !parseKlogWall(text, &w)
        && !parseRfc3164Wall(text, &w))
        return lit;

    // Same reference-year and zone rules as column parsing.
    const TimestampCodec c
        = TimestampCodec::make(TimestampCodec::Kind::Iso8601, QString(), ctx);
    if (!w.hasYear)
        w.year = c.inferredYear(w.month);
    if (!QDate::isValid(w.year, w.month, w.day))
        return lit;
    const qint64 naive = wallToNaiveMs(w.year, w.month, w.day, w.hour,
                                       w.minute, w.second, w.msec);
    const qint64 naiveUpper = naive + w.granularityMs;
    if (w.hasOffset) {
        lit.lowerMs = naive - qint64(w.offsetSec) * 1000;
        lit.upperMs = naiveUpper - qint64(w.offsetSec) * 1000;
    } else {
        lit.lowerMs = c.localToUtc(naive);
        lit.upperMs = c.localToUtc(naiveUpper);
    }
    lit.kind = TimeLiteral::Kind::Absolute;
    return lit;
}

} // namespace logdor
