#include "logdor/JsonLinesParser.h"

#include "JsonLogUtil_p.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimeZone>

namespace logdor {

namespace {

const QLatin1StringView kJournalTimestampKey("__REALTIME_TIMESTAMP");
const QLatin1StringView kInstantKey("instant"); // log4j2 JsonTemplateLayout

// Alias lists, checked in order; first present key wins.
// timeMillis (log4j2 JsonLayout) stays raw like pino's numeric "time" -
// codec detection resolves the epoch-ms lane for temporal filtering.
const QLatin1StringView kTimestampKeys[] = {
    QLatin1StringView("timestamp"), QLatin1StringView("time"),
    QLatin1StringView("ts"), QLatin1StringView("@timestamp"),
    QLatin1StringView("__REALTIME_TIMESTAMP"),
    QLatin1StringView("timeMillis"), QLatin1StringView("instant"),
};
const QLatin1StringView kLevelKeys[] = {
    QLatin1StringView("level"), QLatin1StringView("severity"),
    QLatin1StringView("lvl"), QLatin1StringView("PRIORITY"),
};
const QLatin1StringView kSourceKeys[] = {
    QLatin1StringView("SYSLOG_IDENTIFIER"), QLatin1StringView("_SYSTEMD_UNIT"),
    QLatin1StringView("logger"), QLatin1StringView("loggerName"),
    QLatin1StringView("name"), QLatin1StringView("tag"),
};
const QLatin1StringView kMessageKeys[] = {
    QLatin1StringView("message"), QLatin1StringView("msg"),
    QLatin1StringView("MESSAGE"),
};

template <size_t N>
QJsonValue firstOf(const QJsonObject& obj, const QLatin1StringView (&keys)[N],
                   QLatin1StringView* which = nullptr)
{
    for (const auto& key : keys) {
        if (const auto it = obj.constFind(key); it != obj.constEnd()) {
            if (which)
                *which = key;
            return *it;
        }
    }
    return QJsonValue(QJsonValue::Undefined);
}

using jsonlog::severityFromName;
using jsonlog::severityFromSyslogPriority;
using jsonlog::severityName;
using jsonlog::valueToString;

QString formatTimestamp(const QJsonValue& v, QLatin1StringView key)
{
    if (key == kJournalTimestampKey) {
        // journald: string (or number) of microseconds since the epoch.
        bool okNum = false;
        const qint64 us = v.isString() ? v.toString().toLongLong(&okNum)
                                       : (okNum = v.isDouble(), qint64(v.toDouble()));
        if (okNum)
            return QDateTime::fromMSecsSinceEpoch(us / 1000, QTimeZone::UTC)
                .toString(Qt::ISODateWithMs);
    }
    if (key == kInstantKey && v.isObject()) {
        // log4j2 JsonTemplateLayout: {"epochSecond":N,"nanoOfSecond":N}.
        const QJsonObject o = v.toObject();
        if (const auto sec = o.constFind(QLatin1StringView("epochSecond"));
            sec != o.constEnd() && sec->isDouble()) {
            const qint64 ms = qint64(sec->toDouble()) * 1000
                + qint64(o.value(QLatin1StringView("nanoOfSecond")).toDouble())
                    / 1'000'000;
            return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC)
                .toString(Qt::ISODateWithMs);
        }
    }
    return valueToString(v);
}

} // namespace

QList<FieldSchema> JsonLinesParser::schema() const
{
    return {
        { QStringLiteral("Timestamp"), FieldType::DateTime, FieldHint::Timestamp },
        { QStringLiteral("Level"), FieldType::String, FieldHint::SeverityName },
        { QStringLiteral("Source"), FieldType::String, FieldHint::Identifier },
        { QStringLiteral("Message"), FieldType::String, FieldHint::Message },
    };
}

void JsonLinesParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    out.fields.clear();
    out.fields.resize(FieldCount);
    out.severity = Severity::None;

    const QJsonDocument doc = QJsonDocument::fromJson(raw.toByteArray());
    if (!doc.isObject()) {
        out.fields[Message] = QString::fromUtf8(raw);
        out.ok = false;
        return;
    }
    const QJsonObject obj = doc.object();

    QLatin1StringView tsKey;
    if (const auto ts = firstOf(obj, kTimestampKeys, &tsKey); !ts.isUndefined())
        out.fields[Timestamp] = formatTimestamp(ts, tsKey);

    if (const auto level = firstOf(obj, kLevelKeys); !level.isUndefined()) {
        const QString text = valueToString(level);
        Severity sev = severityFromName(text);
        if (sev == Severity::None) {
            bool okNum = false;
            if (const int priority = text.toInt(&okNum); okNum)
                sev = severityFromSyslogPriority(priority);
        }
        out.severity = sev;
        out.fields[Level] = sev == Severity::None ? text : severityName(sev);
    }

    if (const auto source = firstOf(obj, kSourceKeys); !source.isUndefined())
        out.fields[Source] = valueToString(source);

    // A valid object with no message alias still parses; show the whole
    // line so nothing is lost.
    const auto message = firstOf(obj, kMessageKeys);
    out.fields[Message] = message.isUndefined() ? QString::fromUtf8(raw)
                                                : valueToString(message);
    out.ok = true;
}

bool JsonLinesParser::matchesStructure(QByteArrayView raw) const
{
    return QJsonDocument::fromJson(raw.toByteArray()).isObject();
}

} // namespace logdor
