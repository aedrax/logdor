#include "annotationexporter.h"

namespace {

QString csvEscape(QString value)
{
    if (value.contains(u'"') || value.contains(u',') || value.contains(u'\n'))
        return u'"' + value.replace(QStringLiteral("\""), QStringLiteral("\"\""))
            + u'"';
    return value;
}

} // namespace

QByteArray exportAnnotationsHtml(const logdor::AnnotationSet& set,
                                 const std::function<QString(qint64)>& lineText,
                                 const QString& title)
{
    QString html;
    html += QStringLiteral(
        "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n"
        "<title>%1 - annotations</title>\n<style>\n"
        "body { font-family: sans-serif; margin: 2em; }\n"
        ".note { border-left: 4px solid #4a90d9; margin: 1.2em 0; padding: 0.4em 1em; }\n"
        ".meta { color: #666; font-size: 0.85em; }\n"
        ".lines { font-family: monospace; white-space: pre-wrap; background: #f4f4f4;\n"
        "         padding: 0.5em; margin-top: 0.5em; border-radius: 4px; }\n"
        ".orphaned { border-left-color: #cc4444; }\n"
        ".tag { background: #eee; border-radius: 3px; padding: 0 0.4em; }\n"
        "</style></head>\n<body>\n<h1>%1</h1>\n")
                .arg(title.toHtmlEscaped());

    for (const logdor::Annotation& annotation : set.annotations()) {
        const QString range = annotation.startLine == annotation.endLine
            ? QStringLiteral("line %1").arg(annotation.startLine + 1)
            : QStringLiteral("lines %1-%2").arg(annotation.startLine + 1)
                  .arg(annotation.endLine + 1);
        html += QStringLiteral("<div class=\"note%1\" style=\"%2\">\n")
                    .arg(annotation.orphaned ? QStringLiteral(" orphaned") : QString(),
                         annotation.color.isEmpty()
                             ? QString()
                             : QStringLiteral("border-left-color: %1;")
                                   .arg(annotation.color));
        html += QStringLiteral("<div>%1</div>\n")
                    .arg(annotation.note.toHtmlEscaped()
                             .replace(QStringLiteral("\n"), QStringLiteral("<br>")));
        QString meta = range;
        if (!annotation.author.isEmpty())
            meta += QStringLiteral(" - ") + annotation.author.toHtmlEscaped();
        if (annotation.modifiedAt.isValid())
            meta += QStringLiteral(", ")
                + annotation.modifiedAt.toLocalTime().toString(
                    QStringLiteral("yyyy-MM-dd hh:mm"));
        if (!annotation.tag.isEmpty())
            meta += QStringLiteral(" <span class=\"tag\">#%1</span>")
                        .arg(annotation.tag.toHtmlEscaped());
        if (annotation.orphaned)
            meta += QStringLiteral(" (orphaned - line not found in current file)");
        html += QStringLiteral("<div class=\"meta\">%1</div>\n").arg(meta);

        QString lines;
        if (annotation.orphaned) {
            lines = annotation.snippet;
        } else {
            for (qint64 line = annotation.startLine;
                 line <= annotation.endLine
                 && line - annotation.startLine < 50; ++line) {
                if (!lines.isEmpty())
                    lines += u'\n';
                lines += lineText(line);
            }
        }
        if (!lines.isEmpty())
            html += QStringLiteral("<div class=\"lines\">%1</div>\n")
                        .arg(lines.toHtmlEscaped());
        html += QStringLiteral("</div>\n");
    }
    html += QStringLiteral("</body></html>\n");
    return html.toUtf8();
}

QByteArray exportAnnotationsCsv(const logdor::AnnotationSet& set)
{
    QString csv = QStringLiteral(
        "start_line,end_line,note,color,tag,author,created_at,modified_at,orphaned\n");
    for (const logdor::Annotation& annotation : set.annotations()) {
        csv += QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9\n")
                   .arg(annotation.startLine + 1)
                   .arg(annotation.endLine + 1)
                   .arg(csvEscape(annotation.note), csvEscape(annotation.color),
                        csvEscape(annotation.tag), csvEscape(annotation.author),
                        annotation.createdAt.toUTC().toString(Qt::ISODateWithMs),
                        annotation.modifiedAt.toUTC().toString(Qt::ISODateWithMs),
                        annotation.orphaned ? QStringLiteral("true")
                                            : QStringLiteral("false"));
    }
    return csv.toUtf8();
}
