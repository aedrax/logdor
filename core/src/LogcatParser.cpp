#include "logdor/LogcatParser.h"

#include <QRegularExpression>

namespace logdor {

namespace {

// One regex per `logcat -v` variant, tried in specificity order. Immutable
// after init; match() is thread-safe. Audited against real captures of every
// variant (logs/logcat_*.log) - deviations from the legacy regexes:
//  - brief: tag must not contain ':', otherwise brief steals tag-format
//    lines whose message contains "(123):" and mangles tag/pid.
//  - process: anchored so the tag comes from the TRAILING "(tag)", not the
//    first parenthesised text inside the message.
//  - tag: tags may contain '/' (e.g. "FingerprintProvider/default").
const QRegularExpression reThreadTime(
    R"((\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+(\d+)\s+(\d+)\s+([VDIWEF])\s+([^:]+):\s*(.*))");
const QRegularExpression reLong(
    R"(\[\s*(\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+(\d+):\s*(\d+)\s+([VDIWEF])/([^]]+)\s*\]\s*(.*))");
const QRegularExpression reTime(
    R"((\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2}\.\d{3})\s+([VDIWEF])/([^(]+?)\s*\(\s*(\d+)\):\s*(.*))");
const QRegularExpression reBrief(
    R"(([VDIWEF])/([^(:]+)\(\s*(\d+)\):\s+(.*))");
const QRegularExpression reProcess(
    R"(^([VDIWEF])\(\s*(\d+)\)\s+(.*?)\s+\(([^)]+)\)$)");
const QRegularExpression reThread(
    R"(([VDIWEF])\(\s*(\d+):\s*(\d+)\)\s+(.*))");
const QRegularExpression reTag(
    R"(([VDIWEF])/([^:]+):\s*(.*))");

Severity severityFromChar(QChar c)
{
    switch (c.toLatin1()) {
    case 'V': return Severity::Verbose;
    case 'D': return Severity::Debug;
    case 'I': return Severity::Info;
    case 'W': return Severity::Warning;
    case 'E': return Severity::Error;
    case 'F': return Severity::Fatal;
    default: return Severity::None;
    }
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
    // Unstructured lines (raw format, `-v long` message/continuation lines)
    // show an empty Level, not a wall of "Unknown".
    default: return QString();
    }
}

void setFields(ParsedRow& out, const QString& time, const QString& pid,
               const QString& tid, Severity severity, const QString& tag,
               const QString& message)
{
    out.fields.clear();
    out.fields.append(time);
    out.fields.append(pid);
    out.fields.append(tid);
    out.fields.append(severityName(severity));
    out.fields.append(tag);
    out.fields.append(message);
    out.severity = severity;
}

// Legacy try-order. Returns false only when nothing but the raw fallback fits.
bool parseStructured(const QString& text, ParsedRow& out)
{
    if (auto m = reThreadTime.match(text); m.hasMatch()) {
        setFields(out, m.captured(1), m.captured(2), m.captured(3),
                  severityFromChar(m.captured(4).at(0)),
                  m.captured(5).trimmed(), m.captured(6));
        return true;
    }
    if (auto m = reLong.match(text); m.hasMatch()) {
        setFields(out, m.captured(1), m.captured(2), m.captured(3),
                  severityFromChar(m.captured(4).at(0)),
                  m.captured(5).trimmed(), m.captured(6));
        return true;
    }
    if (auto m = reTime.match(text); m.hasMatch()) {
        setFields(out, m.captured(1), {}, {},
                  severityFromChar(m.captured(2).at(0)),
                  m.captured(3).trimmed(), m.captured(5));
        out.fields[LogcatParser::Pid] = m.captured(4);
        return true;
    }
    if (auto m = reBrief.match(text); m.hasMatch()) {
        setFields(out, {}, m.captured(3), {},
                  severityFromChar(m.captured(1).at(0)),
                  m.captured(2).trimmed(), m.captured(4));
        return true;
    }
    if (auto m = reProcess.match(text); m.hasMatch()) {
        setFields(out, {}, m.captured(2), {},
                  severityFromChar(m.captured(1).at(0)),
                  m.captured(4).trimmed(), m.captured(3));
        return true;
    }
    if (auto m = reThread.match(text); m.hasMatch()) {
        setFields(out, {}, m.captured(2), m.captured(3),
                  severityFromChar(m.captured(1).at(0)), {}, m.captured(4));
        return true;
    }
    if (auto m = reTag.match(text); m.hasMatch()) {
        setFields(out, {}, {}, {},
                  severityFromChar(m.captured(1).at(0)),
                  m.captured(2).trimmed(), m.captured(3));
        return true;
    }
    return false;
}

} // namespace

QList<FieldSchema> LogcatParser::schema() const
{
    return {
        { QStringLiteral("Time"), FieldType::DateTime, FieldHint::Timestamp,
          QStringLiteral("MM-dd HH:mm:ss.zzz") },
        { QStringLiteral("PID"), FieldType::Integer, FieldHint::Numeric },
        { QStringLiteral("TID"), FieldType::Integer, FieldHint::Numeric },
        { QStringLiteral("Level"), FieldType::String, FieldHint::SeverityName },
        { QStringLiteral("Tag"), FieldType::String, FieldHint::Identifier },
        { QStringLiteral("Message"), FieldType::String, FieldHint::Message },
    };
}

void LogcatParser::parseLine(QByteArrayView raw, ParsedRow& out) const
{
    const QString text = QString::fromUtf8(raw);
    if (parseStructured(text, out)) {
        out.ok = true;
        return;
    }
    // Raw fallback: message only, empty level.
    setFields(out, {}, {}, {}, Severity::None, {}, text);
    out.ok = false;
}

bool LogcatParser::matchesStructure(QByteArrayView raw) const
{
    ParsedRow scratch;
    return parseStructured(QString::fromUtf8(raw), scratch);
}

} // namespace logdor
