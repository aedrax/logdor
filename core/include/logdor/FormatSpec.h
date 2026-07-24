#pragma once

#include "logdor/FormatParser.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

namespace logdor {

/**
 * A user-writable log format definition - parse any format without
 * compiling. JSON schema:
 *
 * {
 *   "id": "myformat",                    // required, unique
 *   "displayName": "My Format",          // required
 *   "pattern": "^(?<time>\\S+) (?<msg>.*)$",   // required, named captures
 *   "fields": [                          // required, non-empty
 *     { "name": "Time", "capture": "time", "type": "datetime", "hint": "timestamp" },
 *     { "name": "Message", "capture": "msg", "type": "string", "hint": "message" }
 *   ],
 *   "severity": {                        // optional
 *     "capture": "level",
 *     "map": { "ERR": "error", "WARN": "warning" },   // value (case-insensitive) -> severity
 *     "default": "none"
 *   },
 *   "specificity": 0.8                   // optional, (0,1], default 0.8
 * }
 *
 * types: string | integer | datetime
 * hints: none | numeric | timestamp | severityname | identifier | message
 * severities: none | verbose | debug | info | warning | error | fatal
 *
 * Exactly one field must carry the "message" hint - it doubles as the
 * fallback column for lines the pattern does not match.
 */
struct FormatSpecField {
    QString name;
    QString capture;
    FieldType type = FieldType::String;
    FieldHint hint = FieldHint::None;
};

struct FormatSpec {
    QString id;
    QString displayName;
    QString pattern;
    QList<FormatSpecField> fields;
    QString severityCapture;              // empty = no severity
    QHash<QString, Severity> severityMap; // keys lowercased
    Severity severityDefault = Severity::None;
    double specificity = 0.8;
    QString sourcePath; // provenance for error messages
};

struct FormatSpecError {
    QString sourcePath;
    QString message; // e.g. "fields[2].type: unknown value 'number'"
};

/// Parse + validate one JSON document. std::nullopt => *error describes why.
std::optional<FormatSpec> parseFormatSpec(const QByteArray& json,
                                          const QString& sourcePath,
                                          FormatSpecError* error = nullptr);

/**
 * Load every *.json in @p dirs (sorted per dir; later dirs win on duplicate
 * ids). Invalid files are skipped and reported. The shell decides the
 * directories - core takes paths as arguments.
 */
QList<FormatSpec> loadFormatSpecs(const QStringList& dirs,
                                  QList<FormatSpecError>* errors = nullptr);

} // namespace logdor
