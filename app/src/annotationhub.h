#ifndef ANNOTATIONHUB_H
#define ANNOTATIONHUB_H

#include <logdor/Annotation.h>
#include <logdor/AnnotationScan.h>
#include <logdor/FileSource.h>
#include <logdor/LineIndex.h>

#include <QFutureWatcher>
#include <QObject>

#include <memory>

/**
 * Owner of the current file's annotations, shared by every viewer and the
 * Annotations panel — a note made in one view shows in all of them.
 *
 * GUI-thread only. MainWindow owns the single instance (lifetime = app) and
 * handles persistence timing; the hub stamps ids/timestamps/author and
 * computes anchors on edits, and runs re-anchoring off-thread on file load.
 */
class Q_DECL_EXPORT AnnotationHub : public QObject {
    Q_OBJECT
public:
    explicit AnnotationHub(QObject* parent = nullptr);

    //=== File lifecycle (MainWindow) =========================================

    /// Prime the hub for a newly opened file and start anchor verification.
    void beginFile(std::shared_ptr<logdor::FileSource> source,
                   std::shared_ptr<const logdor::LineIndex> index,
                   logdor::AnnotationSet loaded);
    void clear(); // file closed / being replaced
    void startReanchor();

    const logdor::AnnotationSet& set() const { return m_set; }
    const logdor::FileIdentity& identity() const { return m_identity; }
    bool hasFile() const { return m_source != nullptr; }

    /// Text of a source line, for the panel and exports. O(1) reads.
    QString lineText(qint64 line) const;
    qint64 lineCount() const { return m_index ? m_index->lineCount() : 0; }

    QString author() const { return m_author; }
    void setAuthor(const QString& author) { m_author = author; }

    //=== Edits (viewers + panel) =============================================

    /// Stamps id, timestamps, author, and the anchor. Null id on failure.
    QUuid addAnnotation(qint64 startLine, qint64 endLine, const QString& note,
                        const QString& color = QString(),
                        const QString& tag = QString());
    bool updateAnnotation(logdor::Annotation annotation); // restamps modifiedAt
    bool removeAnnotation(const QUuid& id);
    void mergeFrom(const logdor::AnnotationSet& imported); // File > Import

    //=== Persistence support (MainWindow) ====================================

    bool isDirty() const { return m_set.isDirty(); }
    void clearDirty() { m_set.clearDirty(); }

signals:
    void annotationsChanged();
    void reanchorFinished(int verified, int reanchored, int orphaned);

private slots:
    void onReanchorFinished();

private:
    logdor::AnnotationSet m_set;
    logdor::FileIdentity m_identity;
    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    QFutureWatcher<logdor::ReanchorResult> m_reanchorWatcher;
    QString m_author;
};

#endif // ANNOTATIONHUB_H
