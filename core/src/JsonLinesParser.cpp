#include "logdor/JsonLinesParser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimeZone>

namespace logdor {

namespace {

const QLatin1StringView kJournalTimestampKey("__REALTIME_TIMESTAMP");

// Alias lists, checked in order; first present key wins.
const QLatin1StringView kTimestampKeys[] = {
    QLatin1StringView("timestamp"), QLatin1StringView("time"),
    QLatin1StringView("ts"), QLatin1StringView("@timestamp"),
    QLatin1StringView("__REALTIME_TIMESTAMP"),
};
const QLatin1StringView kLevelKeys[] = {
    QLatin1StringView("level"), QLatin1StringView("severity"),
    QLatin1StringView("lvl"), QLatin1StringView("PRIORITY"),
};
const QLatin1StringView kSourceKeys[] = {
    QLatin1StringView("SYSLOG_IDENTIFIER"), QLatin1StringView("_SYSTEMD_UNIT"),
    QLatin1StringView("logger"), QLatin1StringView("name"),
    QLatin1StringView("tag"),
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

QString valueToString(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Double: {
        // Integral values (epoch timestamps, PIDs) must not collapse into
        // scientific notation.
        const double d = v.toDouble();
        if (d >= -9.2e18 && d <= 9.2e18 && d == double(qint64(d)))
            return QString::number(qint64(d));
        return QString::number(d, 'g', 15);
    }
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Array:
    case QJsonValue::Object: {
        const QJsonDocument doc = v.isArray() ? QJsonDocument(v.toArray())
                                              : QJsonDocument(v.toObject());
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    default:
        return QString();
    }
}

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
    return valueToString(v);
}

QString severityName(Severity s)
{
    switch (s) {
    case Severity::Verbose: return QStringLiteral("Verbose");
    case Severity::Debug: return QStringLiteral("Debug");
    case Severity::Info: return QStringLiteral("Info");
    case Severity::Warning: return QStringLiteral("Warning");
    case Severity::Error: return QStringLiteral("Error");
    case Severity::Fatal: return QStringLiteral("Fatal");
    case Severity::None: break;
    }
    return QString();
}

Severity severityFromName(const QString& name)
{
    const QString n = name.toLower();
    if (n == u"trace") return Severity::Verbose;
    if (n == u"debug") return Severity::Debug;
    if (n == u"info" || n == u"notice") return Severity::Info;
    if (n == u"warn" || n == u"warning") return Severity::Warning;
    if (n == u"error" || n == u"err") return Severity::Error;
    if (n == u"fatal" || n == u"critical" || n == u"crit" || n == u"panic")
        return Severity::Fatal;
    return Severity::None;
}

Severity severityFromSyslogPriority(int priority)
{
    switch (priority) {
    case 0: case 1: case 2: return Severity::Fatal;
    case 3: return Severity::Error;
    case 4: return Severity::Warning;
    case 5: case 6: return Severity::Info;
    case 7: return Severity::Debug;
    default: return Severity::None;
    }
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
