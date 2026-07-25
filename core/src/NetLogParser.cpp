#include "logdor/NetLogParser.h"

#include "logdor/FileSource.h"
#include "logdor/LineIndex.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimeZone>

namespace logdor {

namespace {

// The constants line is one huge JSON object; anything bigger is not a
// plausible net-export capture (and an adversarial file must not make
// fromFile read without bound).
constexpr qsizetype kConstantsLineCap = 8 * 1024 * 1024;

// "1521580000000" or 1521580000000 => ms; false when neither.
bool jsonToMs(const QJsonValue& v, qint64* out)
{
    if (v.isDouble()) {
        *out = qint64(v.toDouble());
        return true;
    }
    if (v.isString()) {
        bool ok = false;
        *out = v.toString().toLongLong(&ok);
        return ok;
    }
    return false;
}

// name -> int constants table, inverted for per-line lookups.
QHash<int, QString> invert(const QJsonObject& table)
{
    QHash<int, QString> names;
    names.reserve(table.size());
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        if (it.value().isDouble())
            names.insert(int(it.value().toDouble()), it.key());
    }
    return names;
}

// An event line minus Chrome's inter-event comma; empty when the line
// cannot be an event object.
QByteArrayView eventBody(QByteArrayView raw)
{
    QByteArrayView body = raw.trimmed();
    if (body.endsWith(','))
        body.chop(1);
    if (!body.startsWith('{'))
        return {};
    return body;
}

} // namespace

std::shared_ptr<const NetLogParser> NetLogParser::fromFile(
    const FileSource& source, const LineIndex& index)
{
    if (index.lineCount() == 0 || index.lengthOf(0) > kConstantsLineCap)
        return nullptr;

    const QByteArray line0
        = source.read(index.offsetOf(0), index.lengthOf(0)).trimmed();
    if (!line0.startsWith('{'))
        return nullptr;

    // Chrome writes '{"constants": {...},' and only closes the outer object
    // at the end of the capture (never, for a crashed browser): chop the
    // trailing comma and re-balance before giving up.
    QJsonDocument doc = QJsonDocument::fromJson(line0);
    if (!doc.isObject()) {
        QByteArray repaired = line0;
        if (repaired.endsWith(','))
            repaired.chop(1);
        doc = QJsonDocument::fromJson(repaired);
        if (!doc.isObject())
            doc = QJsonDocument::fromJson(repaired + '}');
        if (!doc.isObject())
            return nullptr;
    }

    const QJsonObject constants
        = doc.object().value(QLatin1StringView("constants")).toObject();
    const QJsonObject eventTypes
        = constants.value(QLatin1StringView("logEventTypes")).toObject();
    const QJsonObject sourceTypes
        = constants.value(QLatin1StringView("logSourceType")).toObject();
    const QJsonObject phases
        = constants.value(QLatin1StringView("logEventPhase")).toObject();
    qint64 tickOffsetMs = 0;
    if (eventTypes.isEmpty() || sourceTypes.isEmpty() || phases.isEmpty()
        || !jsonToMs(constants.value(QLatin1StringView("timeTickOffset")),
                     &tickOffsetMs))
        return nullptr;

    auto parser = std::shared_ptr<NetLogParser>(new NetLogParser);
    parser->m_eventTypeNames = invert(eventTypes);
    parser->m_sourceTypeNames = invert(sourceTypes);
    parser->m_phaseBegin
        = int(phases.value(QLatin1StringView("PHASE_BEGIN")).toDouble(1));
    parser->m_phaseEnd
        = int(phases.value(QLatin1StringView("PHASE_END")).toDouble(2));
    parser->m_timeTickOffsetMs = tickOffsetMs;
    return parser;
}

QList<FieldSchema> NetLogParser::schema() const
{
    return {
        { QStringLiteral("Time"), FieldType::DateTime, FieldHint::Timestamp,
          QStringLiteral("iso8601") },
        { QStringLiteral("Source"), FieldType::String, FieldHint::Identifier },
        { QStringLiteral("Phase"), FieldType::String, FieldHint::None },
        { QStringLiteral("Event"), FieldType::String, FieldHint::Identifier },
        { QStringLiteral("Params"), FieldType::String, FieldHint::Message },
    };
}

void NetLogParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    out.fields.clear();
    out.fields.resize(FieldCount);
    out.severity = Severity::None;

    const QByteArrayView body = eventBody(raw);
    const QJsonDocument doc = QJsonDocument::fromJson(body.toByteArray());
    const QJsonObject event = doc.object();
    const QJsonValue type = event.value(QLatin1StringView("type"));
    const QJsonValue source = event.value(QLatin1StringView("source"));
    if (!doc.isObject() || !type.isDouble() || !source.isObject()) {
        out.fields[Params] = QString::fromUtf8(raw);
        out.ok = false;
        return;
    }

    qint64 ticksMs = 0;
    if (jsonToMs(event.value(QLatin1StringView("time")), &ticksMs))
        out.fields[Time] = QDateTime::fromMSecsSinceEpoch(
                               m_timeTickOffsetMs + ticksMs, QTimeZone::UTC)
                               .toString(Qt::ISODateWithMs);

    const QJsonObject sourceObj = source.toObject();
    const int sourceType
        = int(sourceObj.value(QLatin1StringView("type")).toDouble(-1));
    const qint64 sourceId
        = qint64(sourceObj.value(QLatin1StringView("id")).toDouble(-1));
    out.fields[Source] = m_sourceTypeNames.value(sourceType,
                                                 QString::number(sourceType))
        + u' ' + QString::number(sourceId);

    const int phase
        = int(event.value(QLatin1StringView("phase")).toDouble(-1));
    if (phase == m_phaseBegin)
        out.fields[Phase] = QStringLiteral("+");
    else if (phase == m_phaseEnd)
        out.fields[Phase] = QStringLiteral("-");

    const int typeId = int(type.toDouble());
    out.fields[Event]
        = m_eventTypeNames.value(typeId, QString::number(typeId));

    if (const auto params = event.value(QLatin1StringView("params"));
        params.isObject())
        out.fields[Params] = QString::fromUtf8(
            QJsonDocument(params.toObject()).toJson(QJsonDocument::Compact));

    out.ok = true;
}

bool NetLogParser::matchesStructure(QByteArrayView raw) const
{
    const QJsonDocument doc
        = QJsonDocument::fromJson(eventBody(raw).toByteArray());
    if (!doc.isObject())
        return false;
    const QJsonObject event = doc.object();
    return event.value(QLatin1StringView("type")).isDouble()
        && event.value(QLatin1StringView("source")).isObject();
}

} // namespace logdor
