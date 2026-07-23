#include "logdor/FormatSpec.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace logdor {

namespace {

std::optional<FieldType> fieldTypeFromString(const QString& s)
{
    const QString v = s.toLower();
    if (v == u"string") return FieldType::String;
    if (v == u"integer") return FieldType::Integer;
    if (v == u"datetime") return FieldType::DateTime;
    return std::nullopt;
}

std::optional<FieldHint> fieldHintFromString(const QString& s)
{
    const QString v = s.toLower();
    if (v == u"none") return FieldHint::None;
    if (v == u"numeric") return FieldHint::Numeric;
    if (v == u"timestamp") return FieldHint::Timestamp;
    if (v == u"severityname") return FieldHint::SeverityName;
    if (v == u"identifier") return FieldHint::Identifier;
    if (v == u"message") return FieldHint::Message;
    return std::nullopt;
}

std::optional<Severity> severityFromString(const QString& s)
{
    const QString v = s.toLower();
    if (v == u"none" || v == u"unknown") return Severity::None;
    if (v == u"verbose") return Severity::Verbose;
    if (v == u"debug") return Severity::Debug;
    if (v == u"info") return Severity::Info;
    if (v == u"warning" || v == u"warn") return Severity::Warning;
    if (v == u"error") return Severity::Error;
    if (v == u"fatal") return Severity::Fatal;
    return std::nullopt;
}

} // namespace

std::optional<FormatSpec> parseFormatSpec(const QByteArray& json,
                                          const QString& sourcePath,
                                          FormatSpecError* error)
{
    const auto fail = [&](const QString& message) -> std::optional<FormatSpec> {
        if (error)
            *error = FormatSpecError { sourcePath, message };
        return std::nullopt;
    };

    QJsonParseError jsonError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &jsonError);
    if (doc.isNull())
        return fail(QStringLiteral("invalid JSON at offset %1: %2")
                        .arg(jsonError.offset)
                        .arg(jsonError.errorString()));
    if (!doc.isObject())
        return fail(QStringLiteral("top-level value must be an object"));
    const QJsonObject root = doc.object();

    FormatSpec spec;
    spec.sourcePath = sourcePath;

    spec.id = root.value(u"id").toString();
    if (spec.id.isEmpty())
        return fail(QStringLiteral("id: required non-empty string"));
    spec.displayName = root.value(u"displayName").toString();
    if (spec.displayName.isEmpty())
        return fail(QStringLiteral("displayName: required non-empty string"));

    spec.pattern = root.value(u"pattern").toString();
    if (spec.pattern.isEmpty())
        return fail(QStringLiteral("pattern: required non-empty string"));
    const QRegularExpression regex(spec.pattern);
    if (!regex.isValid())
        return fail(QStringLiteral("pattern: invalid regex at offset %1: %2")
                        .arg(regex.patternErrorOffset())
                        .arg(regex.errorString()));
    const QStringList captures = regex.namedCaptureGroups();

    const QJsonValue fieldsValue = root.value(u"fields");
    if (!fieldsValue.isArray() || fieldsValue.toArray().isEmpty())
        return fail(QStringLiteral("fields: required non-empty array"));

    int messageHints = 0;
    QSet<QString> seenNames;
    const QJsonArray fieldsArray = fieldsValue.toArray();
    for (int i = 0; i < fieldsArray.size(); ++i) {
        const auto at = [&](const char* key) {
            return QStringLiteral("fields[%1].%2").arg(i).arg(QLatin1String(key));
        };
        if (!fieldsArray[i].isObject())
            return fail(QStringLiteral("fields[%1]: must be an object").arg(i));
        const QJsonObject fieldObject = fieldsArray[i].toObject();

        FormatSpecField field;
        field.name = fieldObject.value(u"name").toString();
        if (field.name.isEmpty())
            return fail(at("name") + QStringLiteral(": required non-empty string"));
        if (seenNames.contains(field.name.toLower()))
            return fail(at("name")
                        + QStringLiteral(": duplicate field name '%1'").arg(field.name));
        seenNames.insert(field.name.toLower());

        field.capture = fieldObject.value(u"capture").toString();
        if (field.capture.isEmpty())
            return fail(at("capture") + QStringLiteral(": required non-empty string"));
        if (!captures.contains(field.capture))
            return fail(at("capture")
                        + QStringLiteral(": named group '%1' not found in pattern")
                              .arg(field.capture));

        if (fieldObject.contains(u"type")) {
            const auto type = fieldTypeFromString(fieldObject.value(u"type").toString());
            if (!type)
                return fail(at("type")
                            + QStringLiteral(": unknown value '%1'")
                                  .arg(fieldObject.value(u"type").toString()));
            field.type = *type;
        }
        if (fieldObject.contains(u"hint")) {
            const auto hint = fieldHintFromString(fieldObject.value(u"hint").toString());
            if (!hint)
                return fail(at("hint")
                            + QStringLiteral(": unknown value '%1'")
                                  .arg(fieldObject.value(u"hint").toString()));
            field.hint = *hint;
        }
        if (field.hint == FieldHint::Message)
            ++messageHints;
        spec.fields.append(field);
    }
    if (messageHints != 1)
        return fail(QStringLiteral(
            "fields: exactly one field must have the \"message\" hint "
            "(the fallback column); found %1").arg(messageHints));

    if (root.contains(u"severity")) {
        if (!root.value(u"severity").isObject())
            return fail(QStringLiteral("severity: must be an object"));
        const QJsonObject sev = root.value(u"severity").toObject();
        spec.severityCapture = sev.value(u"capture").toString();
        if (spec.severityCapture.isEmpty())
            return fail(QStringLiteral("severity.capture: required non-empty string"));
        if (!captures.contains(spec.severityCapture))
            return fail(QStringLiteral(
                "severity.capture: named group '%1' not found in pattern")
                            .arg(spec.severityCapture));
        const QJsonObject map = sev.value(u"map").toObject();
        for (auto it = map.begin(); it != map.end(); ++it) {
            const auto severity = severityFromString(it.value().toString());
            if (!severity)
                return fail(QStringLiteral("severity.map['%1']: unknown severity '%2'")
                                .arg(it.key(), it.value().toString()));
            spec.severityMap.insert(it.key().toLower(), *severity);
        }
        if (sev.contains(u"default")) {
            const auto fallback = severityFromString(sev.value(u"default").toString());
            if (!fallback)
                return fail(QStringLiteral("severity.default: unknown severity '%1'")
                                .arg(sev.value(u"default").toString()));
            spec.severityDefault = *fallback;
        }
    }

    if (root.contains(u"specificity")) {
        spec.specificity = root.value(u"specificity").toDouble();
        if (spec.specificity <= 0.0 || spec.specificity > 1.0)
            return fail(QStringLiteral("specificity: must be in (0, 1]"));
    }

    return spec;
}

QList<FormatSpec> loadFormatSpecs(const QStringList& dirs,
                                  QList<FormatSpecError>* errors)
{
    QHash<QString, int> indexById;
    QList<FormatSpec> specs;

    for (const QString& dirPath : dirs) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;
        const QStringList files = dir.entryList({ QStringLiteral("*.json") },
                                                QDir::Files, QDir::Name);
        for (const QString& fileName : files) {
            const QString path = dir.absoluteFilePath(fileName);
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                if (errors)
                    errors->append({ path, QStringLiteral("cannot open: %1")
                                               .arg(file.errorString()) });
                continue;
            }
            FormatSpecError error;
            if (auto spec = parseFormatSpec(file.readAll(), path, &error)) {
                // Later directories (user dirs) override earlier ones.
                if (const auto it = indexById.constFind(spec->id);
                    it != indexById.constEnd()) {
                    specs[it.value()] = std::move(*spec);
                } else {
                    indexById.insert(spec->id, specs.size());
                    specs.append(std::move(*spec));
                }
            } else if (errors) {
                errors->append(error);
            }
        }
    }
    return specs;
}

} // namespace logdor
