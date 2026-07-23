#include "annotationhub.h"

AnnotationHub::AnnotationHub(QObject* parent)
    : QObject(parent)
{
    m_author = qEnvironmentVariable("USER");
    if (m_author.isEmpty())
        m_author = qEnvironmentVariable("USERNAME");

    connect(&m_reanchorWatcher, &QFutureWatcherBase::finished,
            this, &AnnotationHub::onReanchorFinished);
}

void AnnotationHub::beginFile(std::shared_ptr<logdor::FileSource> source,
                              std::shared_ptr<const logdor::LineIndex> index,
                              logdor::AnnotationSet loaded)
{
    m_reanchorWatcher.cancel();
    m_source = std::move(source);
    m_index = std::move(index);
    m_identity = logdor::computeFileIdentity(*m_source);
    m_set = std::move(loaded);
    emit annotationsChanged();
    if (!m_set.isEmpty())
        startReanchor();
}

void AnnotationHub::clear()
{
    m_reanchorWatcher.cancel();
    m_source.reset();
    m_index.reset();
    m_identity = {};
    m_set = {};
    emit annotationsChanged();
}

void AnnotationHub::startReanchor()
{
    if (!m_source || !m_index || m_set.isEmpty())
        return;
    m_reanchorWatcher.cancel();
    m_reanchorWatcher.setFuture(
        logdor::reanchorAnnotations(m_set, m_source, m_index));
}

void AnnotationHub::onReanchorFinished()
{
    if (m_reanchorWatcher.future().isCanceled())
        return;
    const logdor::ReanchorResult result = m_reanchorWatcher.future().result();
    const bool wasDirty = m_set.isDirty(); // edits made while scanning
    m_set = result.set;
    if (wasDirty || result.reanchored > 0)
        m_set.markDirty();
    emit annotationsChanged();
    emit reanchorFinished(result.verified, result.reanchored, result.orphaned);
}

QString AnnotationHub::lineText(qint64 line) const
{
    if (!m_source || !m_index || line < 0 || line >= m_index->lineCount())
        return {};
    return QString::fromUtf8(
        m_source->read(m_index->offsetOf(line), m_index->lengthOf(line)));
}

QUuid AnnotationHub::addAnnotation(qint64 startLine, qint64 endLine,
                                   const QString& note, const QString& color,
                                   const QString& tag)
{
    if (!m_source || !m_index || startLine < 0 || endLine < startLine
        || startLine >= m_index->lineCount())
        return {};

    logdor::Annotation annotation;
    annotation.id = QUuid::createUuid();
    annotation.startLine = startLine;
    annotation.endLine = qMin(endLine, m_index->lineCount() - 1);
    annotation.note = note;
    annotation.color = color;
    annotation.tag = tag;
    annotation.author = m_author;
    annotation.createdAt = QDateTime::currentDateTimeUtc();
    annotation.modifiedAt = annotation.createdAt;
    const logdor::LineAnchor anchor =
        logdor::makeAnchor(*m_source, *m_index, startLine);
    annotation.anchorHash = anchor.anchorHash;
    annotation.snippet = anchor.snippet;

    const QUuid id = annotation.id;
    m_set.upsert(std::move(annotation));
    emit annotationsChanged();
    return id;
}

bool AnnotationHub::updateAnnotation(logdor::Annotation annotation)
{
    if (!m_set.find(annotation.id))
        return false;
    annotation.modifiedAt = QDateTime::currentDateTimeUtc();
    m_set.upsert(std::move(annotation));
    emit annotationsChanged();
    return true;
}

bool AnnotationHub::removeAnnotation(const QUuid& id)
{
    if (!m_set.remove(id))
        return false;
    emit annotationsChanged();
    return true;
}

void AnnotationHub::mergeFrom(const logdor::AnnotationSet& imported)
{
    m_set = logdor::mergeAnnotations(m_set, imported);
    emit annotationsChanged();
    startReanchor(); // imported anchors need verification against this file
}
