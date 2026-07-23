#ifndef LOGVIEWERWIDGET_H
#define LOGVIEWERWIDGET_H

#include "logtablemodel.h"
#include "plugininterface.h"

#include <logdor/ColumnScan.h>
#include <logdor/FilterScan.h>
#include <logdor/SortScan.h>

#include <QFutureWatcher>
#include <QWidget>

class QLabel;
class QTableView;

/**
 * Reusable log viewer: QTableView (uniform row heights, row selection) over
 * a LogTableModel, plus off-thread filtering (text or field-query mode),
 * header-click sorting, and echo-safe selection sync. Field queries and
 * sorting share a per-file column cache: the first one pays a one-time
 * extraction scan, refinements are fast.
 */
class Q_DECL_EXPORT LogViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogViewerWidget(QWidget* parent = nullptr);
    ~LogViewerWidget() override;

    // Null pair => file closed: cancels all background work, clears caches.
    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index);
    void setParser(std::shared_ptr<const logdor::FormatParser> parser);

    /// Text mode filters directly; query mode (options.inQueryMode) compiles
    /// the field query against the parser schema, extracting any missing
    /// columns first. Compile errors keep the previous rows and show a
    /// status strip.
    void applyFilter(const FilterOptions& options);

    /// Structured predicate (e.g. logcat level/tag chrome), ANDed with the
    /// filter. Re-runs the scan when @p refilter.
    void setExtraPredicate(std::function<bool(qint64, QByteArrayView)> predicate,
                           bool refilter = true);

    /// Enables annotation markers/tooltips and the note context menu.
    void setAnnotationHub(AnnotationHub* hub);

    LogTableModel* model() const { return m_model; }
    QTableView* tableView() const { return m_view; }

signals:
    void linesSelected(const QList<int>& sourceLines);
    void filterApplied(qint64 matchCount, qint64 elapsedMs);
    void queryError(const QString& message, int position);

public slots:
    void selectSourceLines(const QList<int>& sourceLines);

private slots:
    void onSelectionChanged();
    void onScanFinished();
    void onExtractFinished();
    void onSortFinished();
    void onHeaderClicked(int section);
    void onContextMenuRequested(const QPoint& pos);

private:
    void startScan();
    void startSort();
    // Ensure the given columns/severity are cached; returns true when an
    // extraction was started (completion continues the pending work).
    bool ensureColumns(const QList<int>& columns, bool needsSeverity);
    void restoreSelectionSilently();
    void configureColumns();
    void showStatusStrip(const QString& message);
    void clearSortIndicator();

    QTableView* m_view = nullptr;
    QLabel* m_statusStrip = nullptr;
    LogTableModel* m_model = nullptr;
    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    std::shared_ptr<const logdor::FormatParser> m_parser;

    QFutureWatcher<logdor::FilterScanResult> m_scanWatcher;
    QFutureWatcher<logdor::ColumnScanResult> m_extractWatcher;
    QFutureWatcher<logdor::SortResult> m_sortWatcher;
    logdor::ColumnCache m_columnCache;

    FilterOptions m_lastOptions;
    std::shared_ptr<const logdor::CompiledQuery> m_activeQuery;
    std::function<bool(qint64, QByteArrayView)> m_extraPredicate;

    bool m_scanAfterExtract = false;
    bool m_sortAfterExtract = false;
    int m_sortColumn = -1; // view column (0 = No.), -1 = unsorted
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    QList<int> m_lastSelection; // source lines, restored after row-set swaps
    bool m_syncing = false;     // echo-loop guard
    AnnotationHub* m_annotationHub = nullptr;
};

#endif // LOGVIEWERWIDGET_H
