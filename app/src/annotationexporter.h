#ifndef ANNOTATIONEXPORTER_H
#define ANNOTATIONEXPORTER_H

#include "logdorexport.h"

#include <logdor/Annotation.h>

#include <functional>

/**
 * Render annotations for sharing outside Logdor. @p lineText resolves a
 * source line to its text (see AnnotationHub::lineText).
 */
LOGDOR_INTERFACE_EXPORT QByteArray exportAnnotationsHtml(
    const logdor::AnnotationSet& set,
    const std::function<QString(qint64)>& lineText, const QString& title);

LOGDOR_INTERFACE_EXPORT QByteArray exportAnnotationsCsv(const logdor::AnnotationSet& set);

#endif // ANNOTATIONEXPORTER_H
