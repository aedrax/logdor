#ifndef LOGTABLEMODEL_H
#define LOGTABLEMODEL_H

#include <logdor/FileSource.h>
#include <logdor/FormatParser.h>
#include <logdor/LineIndex.h>
#include <logdor/RowSet.h>

#include <QAbstractTableModel>
#include <QCache>
#include <QColor>
#include <QPixmap>

#include <memory>

class AnnotationHub;

/**
 * Schema-driven lazy table model over (FileSource, LineIndex, FormatParser,
 * RowSet). Column 0 is always "No." (1-based source line number); columns
 * 1..N come from the parser schema. data() parses on demand behind a bounded
 * LRU keyed by SOURCE line, so filter changes don't invalidate the cache and
 * memory stays viewport-bounded regardless of file size. GUI-thread only.
 */
class Q_DECL_EXPORT LogTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit LogTableModel(QObject* parent = nullptr);

    // Full reset. All three null/empty => closed state (0 rows/columns except
    // when parser alone changes). parser may be null only while source is null.
    void setSource(std::shared_ptr<logdor::FileSource> source,
                   std::shared_ptr<const logdor::LineIndex> index,
                   std::shared_ptr<const logdor::FormatParser> parser);

    void setRowSet(logdor::RowSet rows); // clears any row order
    const logdor::RowSet& rowSet() const { return m_rows; }

    /**
     * Follow mode: the SAME file grew. Swaps the pointers, drops the parse
     * cache for @p spliceLine (an unterminated final line may have changed),
     * and adopts @p rows - via row insertion when the change is a pure
     * append (rows below spliceLine are identical by construction of
     * RowSet::appended, and the tiny boundary region is verified), else via
     * a model reset. Natural order only; an active row order falls back to
     * a reset.
     */
    void extendSource(std::shared_ptr<logdor::FileSource> source,
                      std::shared_ptr<const logdor::LineIndex> index,
                      qint64 spliceLine, logdor::RowSet rows);
    const std::shared_ptr<const logdor::FormatParser>& parser() const { return m_parser; }

    /**
     * Optional display permutation on top of the row set:
     * order[viewRow] = position within the RowSet (see logdor::sortRows).
     * Empty = natural order. Size must equal rowSet().size().
     */
    void setRowOrder(std::vector<qint32> order);
    bool hasRowOrder() const { return !m_order.empty(); }

    qint64 sourceLineForRow(int row) const;  // -1 when out of range
    int rowForSourceLine(qint64 line) const; // -1 when hidden

    /// Exact legacy logcat palette; invalid QColor for None (no brush).
    static QColor severityColor(logdor::Severity severity);

    /// Annotated lines get a marker (DecorationRole, No. column) and a
    /// tooltip (all columns). May be null.
    void setAnnotationHub(const AnnotationHub* hub);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

private:
    const logdor::ParsedRow* parsedRow(qint64 sourceLine) const;

    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    std::shared_ptr<const logdor::FormatParser> m_parser;
    QList<logdor::FieldSchema> m_schema;
    logdor::RowSet m_rows;
    std::vector<qint32> m_order;   // order[viewRow] = rowSetPos; empty = natural
    std::vector<qint32> m_inverse; // inverse[rowSetPos] = viewRow
    mutable QCache<qint64, logdor::ParsedRow> m_cache { 8192 };
    const AnnotationHub* m_annotationHub = nullptr;
    mutable QHash<QString, QPixmap> m_markerCache; // per annotation color
};

#endif // LOGTABLEMODEL_H
