#include "logdor/DockerJsonParser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace logdor {

namespace {

// The three required keys, all string-valued in every json-file record.
bool requiredStrings(const QJsonObject& obj, QJsonValue* log, QJsonValue* stream,
                     QJsonValue* time)
{
    *log = obj.value(QLatin1StringView("log"));
    *stream = obj.value(QLatin1StringView("stream"));
    *time = obj.value(QLatin1StringView("time"));
    return log->isString() && stream->isString() && time->isString();
}

} // namespace

QList<FieldSchema> DockerJsonParser::schema() const
{
    return {
        { QStringLiteral("Time"), FieldType::DateTime, FieldHint::Timestamp,
          QStringLiteral("iso8601") },
        { QStringLiteral("Stream"), FieldType::String, FieldHint::Identifier },
        { QStringLiteral("Message"), FieldType::String, FieldHint::Message },
    };
}

void DockerJsonParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    out.fields.clear();
    out.fields.resize(FieldCount);
    out.severity = Severity::None;

    const QJsonDocument doc = QJsonDocument::fromJson(raw.toByteArray());
    QJsonValue log, stream, time;
    if (!doc.isObject()
        || !requiredStrings(doc.object(), &log, &stream, &time)) {
        out.fields[Message] = QString::fromUtf8(raw);
        out.ok = false;
        return;
    }

    QString message = log.toString();
    if (message.endsWith(u'\n'))
        message.chop(1);
    if (message.endsWith(u'\r'))
        message.chop(1);

    out.fields[Time] = time.toString();
    out.fields[Stream] = stream.toString();
    out.fields[Message] = message;
    out.ok = true;
}

bool DockerJsonParser::matchesStructure(QByteArrayView raw) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toByteArray());
    QJsonValue log, stream, time;
    return doc.isObject()
        && requiredStrings(doc.object(), &log, &stream, &time);
}

} // namespace logdor
