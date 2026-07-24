#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>
#include <QTimeZone>

#include <memory>
#include <utility>
#include <vector>

namespace logdor {

/**
 * Context for timestamps that do not carry enough information on their own:
 * the zone to assume for zone-less formats (RFC3164, logcat, ...) and a
 * reference date for year-less ones. Built by the shell (app setting + file
 * mtime); core only ever takes it as an argument - never global state.
 */
struct TimeParseContext {
    QTimeZone assumedZone = QTimeZone::systemTimeZone();
    int referenceYear = 0;   // 0 = the current year
    int referenceMonth = 12; // parsed month > this => referenceYear - 1
};

/**
 * Sorted (utcMs, offsetMs east of UTC) steps of @p zone over
 * [fromUtcMs, toUtcMs]; the first entry covers everything earlier. Lets
 * worker threads convert between epoch and wall time with a binary search
 * instead of touching QTimeZone. Empty for an invalid zone.
 */
std::vector<std::pair<qint64, qint32>> zoneOffsetTable(const QTimeZone& zone,
                                                       qint64 fromUtcMs,
                                                       qint64 toUtcMs);

/// Offset (ms east of UTC) in effect at @p utcMs; 0 for an empty table.
qint32 zoneOffsetAt(const std::vector<std::pair<qint64, qint32>>& table,
                    qint64 utcMs);

/**
 * A parsed query-language time literal. Granularity becomes a half-open
 * range: "2026-07-01" covers that whole local day, "2026-07-01 12:30" that
 * minute, "12:30" that minute of ANY day (TimeOfDay). Invalid = the text is
 * not a recognizable date or time.
 */
struct TimeLiteral {
    enum class Kind : quint8 { Invalid, Absolute, TimeOfDay };
    Kind kind = Kind::Invalid;
    qint64 lowerMs = 0;    // Absolute: [lowerMs, upperMs) in UTC epoch ms
    qint64 upperMs = 0;
    qint32 todLowerMs = 0; // TimeOfDay: [todLowerMs, todUpperMs) in ms-of-day
    qint32 todUpperMs = 0;
};

/**
 * Parse a user-typed time literal. Accepts ISO dates/datetimes (an explicit
 * zone suffix beats ctx.assumedZone), bare times of day "12:30[:45[.123]]",
 * bare epoch numbers (unit by magnitude), and the year-less built-in shapes
 * (logcat "07-24 06:15:02.123", RFC3164 "Jul 24 06:15:02", CLF) under ctx's
 * reference date - so a cell's display text round-trips as a literal.
 */
TimeLiteral parseTimeLiteral(QStringView text, const TimeParseContext& ctx);

/**
 * A resolved per-column timestamp format: parses field text to UTC epoch
 * milliseconds (ms since boot for monotonic uptime columns). Resolved once
 * per column - from an explicit format string or by detection over sample
 * values - then immutable; parse() is const and thread-safe (zone
 * conversion goes through a precomputed offset table, never QTimeZone).
 */
class TimestampCodec {
public:
    enum class Kind : quint8 {
        Invalid,
        Iso8601,       // 2026-07-01[T ]12:30[:45[.123]][Z|+hh[:mm]]; date-only ok
        QtPattern,     // arbitrary QDateTime::fromString() pattern
        Rfc3164,       // Jul 24 06:15:02 (year-less; "Jul  4" tolerated)
        Logcat,        // 07-24 06:15:02.123 (year-less)
        Clf,           // 24/Jul/2026:06:15:02[ +0200]
        EpochSeconds,  // in [1e8, 1e11): 1973..5138; fraction allowed
        EpochMillis,   // in [1e11, 1e14)
        EpochMicros,   // in [1e14, 1e17)
        UptimeSeconds, // 12345.678 seconds since boot - monotonic, no calendar
    };

    TimestampCodec() = default;

    /**
     * Reserved words: "iso8601", "epoch-s", "epoch-ms", "epoch-us", "uptime";
     * empty => Invalid (caller should detect()). Anything else is a Qt
     * date/time pattern: no 'y' in the pattern enables year inference, no 't'
     * means values are wall times in ctx.assumedZone. The built-in shapes
     * "MM-dd HH:mm:ss.zzz", "dd/MMM/yyyy:HH:mm:ss", and "MMM d HH:mm:ss" are
     * recognized and upgraded to their hand-rolled fast kinds.
     */
    static TimestampCodec fromFormatString(const QString& timeFormat,
                                           const TimeParseContext& ctx);

    /// Tries every kind against @p samples; the kind parsing the most values
    /// wins (ties: the more specific one). Nothing parses => Invalid codec.
    static TimestampCodec detect(const QStringList& samples,
                                 const TimeParseContext& ctx);

    Kind kind() const { return m_kind; }
    bool isValid() const { return m_kind != Kind::Invalid; }
    /// Values are ms since boot (uptime): orderable, but calendar literals
    /// cannot be compared against them.
    bool isMonotonic() const { return m_kind == Kind::UptimeSeconds; }

    /// UTC epoch ms (ms since boot for uptime) into @p msOut; false leaves
    /// the row's value invalid.
    bool parse(QStringView text, qint64* msOut) const;

private:
    static TimestampCodec make(Kind kind, const QString& pattern,
                               const TimeParseContext& ctx);
    qint64 localToUtc(qint64 naiveMs) const;
    int inferredYear(int month) const
    {
        return month > m_refMonth ? m_refYear - 1 : m_refYear;
    }

    Kind m_kind = Kind::Invalid;
    QString m_pattern; // QtPattern only
    bool m_patternHasYear = true;
    bool m_patternHasZone = false;
    int m_refYear = 0;
    int m_refMonth = 12;
    std::shared_ptr<const std::vector<std::pair<qint64, qint32>>> m_offsets;

    friend TimeLiteral parseTimeLiteral(QStringView text,
                                        const TimeParseContext& ctx);
};

} // namespace logdor
