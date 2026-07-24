#include "logdor/ClfParser.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>

namespace logdor {

namespace {

// Legacy CLFEntry regexes, verbatim.
const QRegularExpression reClf(
    "^(\\S+) (\\S+) (\\S+) \\[([\\w:/]+\\s[+\\-]\\d{4})\\] \"([^\"]*)\" (\\d{3}) (\\d+|-)");
const QRegularExpression reTz("\\s([+-]\\d{4})$");

// Legacy timestamp parse: try "t" timezone format first, then fall back to a
// manual offset shift (a legacy quirk that ADDS the offset to local time -
// preserved for display parity, not correctness).
QString formatTimestamp(const QString& timestamp)
{
    QDateTime ts = QDateTime::fromString(timestamp, QStringLiteral("dd/MMM/yyyy:HH:mm:ss t"));
    if (!ts.isValid()) {
        ts = QDateTime::fromString(timestamp, QStringLiteral("dd/MMM/yyyy:HH:mm:ss"));
        if (ts.isValid()) {
            if (auto tzMatch = reTz.match(timestamp); tzMatch.hasMatch()) {
                const QString offset = tzMatch.captured(1);
                int hours = offset.mid(1, 2).toInt();
                int minutes = offset.mid(3, 2).toInt();
                if (offset.startsWith(u'-')) {
                    hours = -hours;
                    minutes = -minutes;
                }
                ts = ts.addSecs(hours * 3600 + minutes * 60);
            }
        }
    }
    // Invalid QDateTime formats to an empty string, same as legacy display.
    return ts.toString(QStringLiteral("dd/MMM/yyyy:HH:mm:ss"));
}

} // namespace

QList<FieldSchema> ClfParser::schema() const
{
    return {
        { QStringLiteral("Remote Host"), FieldType::String, FieldHint::Identifier },
        { QStringLiteral("Identity"), FieldType::String, FieldHint::None },
        { QStringLiteral("User ID"), FieldType::String, FieldHint::None },
        // The format the reformatted display string uses; the legacy offset
        // quirk in formatTimestamp() is preserved as-is (display parity).
        { QStringLiteral("Timestamp"), FieldType::DateTime, FieldHint::Timestamp,
          QStringLiteral("dd/MMM/yyyy:HH:mm:ss") },
        { QStringLiteral("Method"), FieldType::String, FieldHint::None },
        { QStringLiteral("Path"), FieldType::String, FieldHint::Message },
        { QStringLiteral("Protocol"), FieldType::String, FieldHint::None },
        { QStringLiteral("Status"), FieldType::Integer, FieldHint::Numeric },
        { QStringLiteral("Bytes"), FieldType::Integer, FieldHint::Numeric },
    };
}

void ClfParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    const QString line = QString::fromUtf8(raw);
    out.fields.clear();
    out.fields.resize(FieldCount);
    out.severity = Severity::None;

    const auto match = reClf.match(line);
    if (!match.hasMatch()) {
        // Fallback: show the raw line in the stretch column so mixed files
        // still render every line (legacy showed uninitialized garbage here).
        out.fields[Path] = line;
        out.ok = false;
        return;
    }

    out.fields[RemoteHost] = match.captured(1);
    out.fields[Identity] = match.captured(2);
    out.fields[UserId] = match.captured(3);
    out.fields[Timestamp] = formatTimestamp(match.captured(4));

    const QStringList requestParts = match.captured(5).split(u' ');
    if (requestParts.size() >= 3) {
        out.fields[Method] = requestParts[0];
        out.fields[Path] = requestParts[1];
        out.fields[Protocol] = requestParts[2];
    }

    out.fields[Status] = match.captured(6);
    const QString bytes = match.captured(7);
    out.fields[Bytes] = bytes == u"-" ? QStringLiteral("0") : bytes;
    out.ok = true;
}

bool ClfParser::matchesStructure(QByteArrayView raw) const
{
    return reClf.match(QString::fromUtf8(raw)).hasMatch();
}

} // namespace logdor
