#ifndef ANNOTATIONEXPORTER_H
#define ANNOTATIONEXPORTER_H

#include <logdor/Annotation.h>

#include <functional>

/**
 * Render annotations for sharing outside Logdor. @p lineText resolves a
 * source line to its text (see AnnotationHub::lineText).
 */
Q_DECL_EXPORT QByteArray exportAnnotationsHtml(
    const logdor::AnnotationSet& set,
    const std::function<QString(qint64)>& lineText, const QString& title);

Q_DECL_EXPORT QByteArray exportAnnotationsCsv(const logdor::AnnotationSet& set);

#endif // ANNOTATIONEXPORTER_H
