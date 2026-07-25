#include "logdor/GelfParser.h"

#include "JsonLogUtil_p.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace logdor {

namespace {

bool requiredKeys(const QJsonObject& obj)
{
    return obj.contains(QLatin1StringView("version"))
        && obj.contains(QLatin1StringView("host"))
        && obj.value(QLatin1StringView("short_message")).isString();
}

} // namespace

QList<FieldSchema> GelfParser::schema() const
{
    return {
        { QStringLiteral("Timestamp"), FieldType::DateTime,
          FieldHint::Timestamp, QStringLiteral("epoch-s") },
        { QStringLiteral("Level"), FieldType::String, FieldHint::SeverityName },
        { QStringLiteral("Host"), FieldType::String, FieldHint::Identifier },
        { QStringLiteral("Message"), FieldType::String, FieldHint::Message },
        { QStringLiteral("Extra"), FieldType::String, FieldHint::None },
    };
}

void GelfParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    out.fields.clear();
    out.fields.resize(FieldCount);
    out.severity = Severity::None;

    const QJsonDocument doc = QJsonDocument::fromJson(raw.toByteArray());
    if (!doc.isObject() || !requiredKeys(doc.object())) {
        out.fields[Message] = QString::fromUtf8(raw);
        out.ok = false;
        return;
    }
    const QJsonObject obj = doc.object();

    if (const auto ts = obj.value(QLatin1StringView("timestamp"));
        !ts.isUndefined())
        out.fields[Timestamp] = jsonlog::valueToString(ts);

    // level is syslog 0-7 per spec (number or numeric string); tolerate
    // level names from non-conforming emitters. Absent => uncolored - the
    // spec's "default 1" would paint every such row Fatal.
    if (const auto level = obj.value(QLatin1StringView("level"));
        !level.isUndefined()) {
        const QString text = jsonlog::valueToString(level);
        bool okNum = false;
        Severity sev = Severity::None;
        if (const int priority = text.toInt(&okNum); okNum)
            sev = jsonlog::severityFromSyslogPriority(priority);
        else
            sev = jsonlog::severityFromName(text);
        out.severity = sev;
        out.fields[Level]
            = sev == Severity::None ? text : jsonlog::severityName(sev);
    }

    out.fields[Host] = jsonlog::valueToString(obj.value(QLatin1StringView("host")));
    out.fields[Message] = obj.value(QLatin1StringView("short_message")).toString();

    QJsonObject extra;
    if (const auto full = obj.value(QLatin1StringView("full_message"));
        !full.isUndefined())
        extra.insert(QLatin1StringView("full_message"), full);
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.key().startsWith(u'_') && it.key() != QLatin1StringView("_id"))
            extra.insert(it.key(), it.value());
    }
    if (!extra.isEmpty())
        out.fields[Extra] = QString::fromUtf8(
            QJsonDocument(extra).toJson(QJsonDocument::Compact));

    out.ok = true;
}

bool GelfParser::matchesStructure(QByteArrayView raw) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toByteArray());
    return doc.isObject() && requiredKeys(doc.object());
}

} // namespace logdor
