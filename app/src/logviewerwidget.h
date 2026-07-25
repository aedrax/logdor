#ifndef LOGVIEWERWIDGET_H
#define LOGVIEWERWIDGET_H

#include "logtablemodel.h"
#include "plugininterface.h"

#include <logdor/ColumnScan.h>
#include <logdor/ExportScan.h>
#include <logdor/FilterScan.h>
#include <logdor/HistogramScan.h>
#include <logdor/SortScan.h>

#include <QFutureWatcher>
#include <QJsonObject>
#include <QWidget>

#include <optional>

class HistogramStrip;
class QLabel;
class QMenu;
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

    /**
     * Follow mode: the SAME file grew (PluginInterface::coreSourceExtended
     * semantics). Passthrough views append rows in place; text filters tail
     * scan from firstNewLine minus the context window and splice; query
     * mode tail-extracts the referenced columns, splices the cache, then
     * tail scans. A sorted view or any interrupted earlier extension falls
     * back to a full re-filter. When the view was scrolled to the bottom it
     * stays pinned there.
     */
    void extendCoreSource(std::shared_ptr<logdor::FileSource> source,
                          std::shared_ptr<const logdor::LineIndex> index,
                          qint64 firstNewLine);
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

    /// PluginInterface::setHighlightRules pass-through.
    void setHighlightRules(const QList<HighlightRule>& rules)
    {
        m_model->setHighlightRules(rules);
    }

    /**
     * Restrict visible rows to @p sortedLines (ANDed with the filter and any
     * extra predicate) - the PluginEvent::LinesConstrained mechanism. Null or
     * empty lifts the restriction. Cleared automatically on a new file.
     */
    void setLineConstraint(std::shared_ptr<const std::vector<qint32>> sortedLines);

    LogTableModel* model() const { return m_model; }
    QTableView* tableView() const { return m_view; }

    /**
     * Per-file view state: selection, sort column/order, and the source line
     * at the top of the viewport (a raw scroll offset would be meaningless -
     * row sets rebuild asynchronously). restoreViewState() stashes the state
     * and applies it when the next scan lands; if the saved top line is
     * filtered out, scrolling settles on the nearest surviving row.
     */
    QJsonObject saveViewState() const;
    void restoreViewState(const QJsonObject& state);

signals:
    void linesSelected(const QList<int>& sourceLines);
    /// Ask the shell to add @p term to the filter bar as a field query.
    void filterTermRequested(const QString& term);
    /// A histogram brush: restrict the filter to this UTC range; (0, 0)
    /// clears it (PluginInterface::timeRangeRequested semantics).
    void timeRangeRequested(qint64 fromUtcMs, qint64 toUtcMs);
    /// "Highlight lines like this": ask the shell to create a rule.
    void highlightRequested(const QString& pattern);
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
    void addFilterActions(QMenu* menu, const QModelIndex& clicked);
    void startScan();
    void startTailScan(qint64 spliceLine);
    void finishTailScan(qint64 spliceLine,
                        const logdor::FilterScanResult& result);
    logdor::LineFilter buildLineFilter() const;
    void startSort();
    // ensureColumns for the current m_sortColumn, then sort (shared by
    // header clicks and view-state restore).
    void requestSort();
    void applyPendingRestore();
    void applyPendingScroll();
    int nearestRowForSourceLine(qint64 line) const;
    // Ensure the given columns/severity are cached; returns true when an
    // extraction was started (completion continues the pending work).
    bool ensureColumns(const QList<int>& columns, bool needsSeverity);
    void restoreSelectionSilently();
    void configureColumns();
    void exportVisibleRows();
    void setHistogramVisible(bool visible);
    // Re-bucket the strip for the current row set (no-op while hidden);
    // extracts the time column (and severity) on first use.
    void refreshHistogram();
    int histogramTimeColumn() const;
    void showStatusStrip(const QString& message);
    void clearSortIndicator();

    QTableView* m_view = nullptr;
    QLabel* m_statusStrip = nullptr;
    HistogramStrip* m_histogramStrip = nullptr;
    LogTableModel* m_model = nullptr;
    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    std::shared_ptr<const logdor::FormatParser> m_parser;

    QFutureWatcher<logdor::FilterScanResult> m_scanWatcher;
    QFutureWatcher<logdor::ColumnScanResult> m_extractWatcher;
    QFutureWatcher<logdor::SortResult> m_sortWatcher;
    QFutureWatcher<logdor::HistogramResult> m_histogramWatcher;
    logdor::ColumnCache m_columnCache;

    FilterOptions m_lastOptions;
    // Zone/reference-date for the current file's timestamps; the column
    // cache is only valid for the context it was extracted with.
    logdor::TimeParseContext m_timeContext;
    std::shared_ptr<const logdor::CompiledQuery> m_activeQuery;
    std::function<bool(qint64, QByteArrayView)> m_extraPredicate;
    std::shared_ptr<const std::vector<qint32>> m_lineConstraint;

    bool m_scanAfterExtract = false;
    bool m_sortAfterExtract = false;
    bool m_histogramAfterExtract = false;
    // Follow-mode extension in flight: which splice line the pending tail
    // scan/extract serves; -1 = none. Pinned = keep the view at the bottom.
    qint64 m_tailScanSplice = -1;
    qint64 m_tailExtractSplice = -1;
    bool m_pinnedForExtend = false;
    int m_sortColumn = -1; // view column (0 = No.), -1 = unsorted
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    QList<int> m_lastSelection; // source lines, restored after row-set swaps
    bool m_syncing = false;     // echo-loop guard

    // View-state restore, pending until the next scan lands.
    struct PendingViewState {
        int sortColumn = -1;
        Qt::SortOrder sortOrder = Qt::AscendingOrder;
    };
    std::optional<PendingViewState> m_pendingRestore;
    qint64 m_pendingScrollLine = -1; // applied once the final row order lands
    AnnotationHub* m_annotationHub = nullptr;
};

#endif // LOGVIEWERWIDGET_H
