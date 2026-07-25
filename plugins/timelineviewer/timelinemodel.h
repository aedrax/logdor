#ifndef TIMELINEMODEL_H
#define TIMELINEMODEL_H

#include "timelinefile.h"

#include "../../app/src/highlightrules.h"

#include <logdor/TimelineMerge.h>

#include <QAbstractTableModel>
#include <QCache>
#include <QHash>

#include <memory>
#include <vector>

/**
 * Table over a merged timeline: File | Time | Severity | Message. Rows are
 * (fileId, line) pairs in merged time order; Time and Severity come straight
 * from the per-file extracted lanes, Message from an on-demand parseLine
 * with an LRU keyed (fileId, line) - deliberately NOT a generalization of
 * LogTableModel, whose caches and searches assume one file.
 */
class TimelineModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { FileColumn, TimeColumn, SeverityColumn, MessageColumn };

    explicit TimelineModel(QObject* parent = nullptr);

    /// Replace the merged order and the files it refers to. The per-file
    /// pointers (source, index, parser, lanes) are immutable once Ready, so
    /// holding them here is safe while the viewer mutates its own list.
    void setMerged(std::vector<logdor::TimelineRow> order,
                   QHash<qint32, std::shared_ptr<const TimelineFile>> files);
    void clearMerged();

    /// The merged row behind a proxy-free model row.
    logdor::TimelineRow timelineRow(int row) const
    {
        return m_order[orderIndex(row)];
    }
    qint64 mergedCount() const { return qint64(m_order.size()); }
    /// The stored (always time-ascending) order and its files - snapshot
    /// inputs for the timeline's off-thread TSV export.
    const std::vector<logdor::TimelineRow>& order() const { return m_order; }
    QHash<qint32, std::shared_ptr<const TimelineFile>> files() const
    {
        return m_files;
    }

    /// Newest-first presentation; the stored order stays ascending.
    void setDescending(bool descending);
    bool isDescending() const { return m_descending; }

    /// First presentation row whose event time is at or past @p utcMs
    /// (binary search - the stored order is time-ascending); -1 when none.
    int rowForTime(qint64 utcMs) const;

    /// App-wide highlight rules, applied to each row's raw source line.
    void setHighlightRules(const QList<HighlightRule>& rules);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

private:
    const logdor::ParsedRow* parsedRow(const TimelineFile& file,
                                       qint32 line) const;
    size_t orderIndex(int row) const
    {
        return m_descending ? m_order.size() - 1 - size_t(row) : size_t(row);
    }

    bool m_descending = false;
    HighlightMatcher m_highlights;
    std::vector<logdor::TimelineRow> m_order;
    QHash<qint32, std::shared_ptr<const TimelineFile>> m_files;
    QHash<qint32, int> m_messageField; // fileId -> schema field for Message
    mutable QCache<qint64, logdor::ParsedRow> m_cache { 8192 };
};

#endif // TIMELINEMODEL_H
