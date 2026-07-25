#pragma once

// Internal shared helpers for the JSON-per-line parsers (JsonLinesParser,
// DockerJsonParser, GelfParser). Not installed; include from core/src only.

#include "logdor/FormatParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace logdor::jsonlog {

inline QString valueToString(const QJsonValue& v)
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

inline QString severityName(Severity s)
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

inline Severity severityFromName(const QString& name)
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

inline Severity severityFromSyslogPriority(int priority)
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

} // namespace logdor::jsonlog
