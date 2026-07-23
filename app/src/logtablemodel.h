#ifndef LOGTABLEMODEL_H
#define LOGTABLEMODEL_H

#include <logdor/FileSource.h>
#include <logdor/FormatParser.h>
#include <logdor/LineIndex.h>
#include <logdor/RowSet.h>

#include <QAbstractTableModel>
#include <QCache>
#include <QColor>

#include <memory>

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

    void setRowSet(logdor::RowSet rows);
    const logdor::RowSet& rowSet() const { return m_rows; }
    const std::shared_ptr<const logdor::FormatParser>& parser() const { return m_parser; }

    qint64 sourceLineForRow(int row) const;  // -1 when out of range
    int rowForSourceLine(qint64 line) const; // -1 when hidden

    /// Exact legacy logcat palette; invalid QColor for None (no brush).
    static QColor severityColor(logdor::Severity severity);

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
    mutable QCache<qint64, logdor::ParsedRow> m_cache { 8192 };
};

#endif // LOGTABLEMODEL_H
