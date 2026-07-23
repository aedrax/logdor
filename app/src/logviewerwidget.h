#ifndef LOGVIEWERWIDGET_H
#define LOGVIEWERWIDGET_H

#include "logtablemodel.h"
#include "plugininterface.h"

#include <logdor/FilterScan.h>

#include <QFutureWatcher>
#include <QWidget>

class QTableView;

/**
 * Reusable log viewer: QTableView (uniform row heights, row selection, no
 * sorting) over a LogTableModel, plus off-thread filter scanning and
 * echo-safe selection sync. Plugins wrap this and add format-specific chrome.
 */
class Q_DECL_EXPORT LogViewerWidget : public QWidget {
    Q_OBJECT
public:
    explicit LogViewerWidget(QWidget* parent = nullptr);
    ~LogViewerWidget() override;

    // Null pair => file closed: cancels any scan and clears the model.
    void setCoreSource(std::shared_ptr<logdor::FileSource> source,
                       std::shared_ptr<const logdor::LineIndex> index);
    void setParser(std::shared_ptr<const logdor::FormatParser> parser);

    /// Converts FilterOptions + the extra predicate into a cancellable
    /// off-thread logdor::scanFilter; the result swaps in atomically.
    void applyFilter(const FilterOptions& options);

    /// Structured predicate (e.g. logcat level/tag chrome), ANDed with the
    /// text filter. Re-runs the last filter when @p refilter.
    void setExtraPredicate(std::function<bool(qint64, QByteArrayView)> predicate,
                           bool refilter = true);

    LogTableModel* model() const { return m_model; }
    QTableView* tableView() const { return m_view; }

signals:
    void linesSelected(const QList<int>& sourceLines);
    void filterApplied(qint64 matchCount, qint64 elapsedMs);

public slots:
    /// Incoming sync from other plugins: selects the visible rows for these
    /// source lines WITHOUT re-emitting linesSelected; scrolls to the first
    /// visible one. Hidden (filtered-out) lines are skipped.
    void selectSourceLines(const QList<int>& sourceLines);

private slots:
    void onSelectionChanged();
    void onScanFinished();

private:
    void startScan();
    void configureColumns();

    QTableView* m_view = nullptr;
    LogTableModel* m_model = nullptr;
    std::shared_ptr<logdor::FileSource> m_source;
    std::shared_ptr<const logdor::LineIndex> m_index;
    std::shared_ptr<const logdor::FormatParser> m_parser;
    QFutureWatcher<logdor::FilterScanResult> m_scanWatcher;
    FilterOptions m_lastOptions;
    std::function<bool(qint64, QByteArrayView)> m_extraPredicate;
    QList<int> m_lastSelection; // source lines; restored after row-set swaps
    bool m_syncing = false;     // echo-loop guard
};

#endif // LOGVIEWERWIDGET_H
