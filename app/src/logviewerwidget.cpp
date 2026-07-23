#include "logviewerwidget.h"

#include <QFontMetrics>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QTableView>
#include <QVBoxLayout>

using namespace logdor;

LogViewerWidget::LogViewerWidget(QWidget* parent)
    : QWidget(parent)
    , m_view(new QTableView(this))
    , m_statusStrip(new QLabel(this))
    , m_model(new LogTableModel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view);
    layout->addWidget(m_statusStrip);

    m_statusStrip->setStyleSheet(
        "QLabel { background-color: #FFB6C1; color: black; padding: 3px; }");
    m_statusStrip->hide();

    m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSortingEnabled(false); // sorting is driven manually (off-thread)
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setShowGrid(false);
    m_view->setAlternatingRowColors(true);
    m_view->verticalHeader()->setVisible(false);
    const QFontMetrics fm(m_view->font());
    m_view->verticalHeader()->setDefaultSectionSize(fm.height() + 4);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->horizontalHeader()->setSectionsClickable(true);
    m_view->horizontalHeader()->setSortIndicatorShown(true);
    m_view->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);

    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &LogViewerWidget::onSelectionChanged);
    connect(m_view->horizontalHeader(), &QHeaderView::sectionClicked,
            this, &LogViewerWidget::onHeaderClicked);
    connect(&m_scanWatcher, &QFutureWatcherBase::finished,
            this, &LogViewerWidget::onScanFinished);
    connect(&m_extractWatcher, &QFutureWatcherBase::finished,
            this, &LogViewerWidget::onExtractFinished);
    connect(&m_sortWatcher, &QFutureWatcherBase::finished,
            this, &LogViewerWidget::onSortFinished);
}

LogViewerWidget::~LogViewerWidget()
{
    m_scanWatcher.cancel();
    m_extractWatcher.cancel();
    m_sortWatcher.cancel();
}

void LogViewerWidget::setCoreSource(std::shared_ptr<FileSource> source,
                                    std::shared_ptr<const LineIndex> index)
{
    // Cancel before swapping so late results can't resurrect the old file.
    m_scanWatcher.cancel();
    m_extractWatcher.cancel();
    m_sortWatcher.cancel();
    m_source = std::move(source);
    m_index = std::move(index);
    m_columnCache.clear();
    m_activeQuery.reset();
    m_scanAfterExtract = false;
    m_sortAfterExtract = false;
    m_sortColumn = -1;
    clearSortIndicator();
    m_statusStrip->hide();
    m_lastSelection.clear();
    m_syncing = true;
    m_model->setSource(m_source, m_index, m_parser);
    m_syncing = false;
    if (m_source && m_index && !m_lastOptions.query.isEmpty())
        applyFilter(m_lastOptions);
}

void LogViewerWidget::setParser(std::shared_ptr<const FormatParser> parser)
{
    m_parser = std::move(parser);
    m_extractWatcher.cancel();
    m_sortWatcher.cancel();
    m_columnCache.clear(); // columns are parser-specific
    m_activeQuery.reset();
    m_sortColumn = -1;
    clearSortIndicator();
    m_syncing = true;
    m_model->setSource(m_source, m_index, m_parser);
    m_syncing = false;
    configureColumns();
}

void LogViewerWidget::configureColumns()
{
    if (!m_parser)
        return;
    auto* header = m_view->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    const auto schema = m_parser->schema();
    for (int i = 0; i < schema.size(); ++i) {
        if (schema[i].hint == FieldHint::Message)
            header->setSectionResizeMode(i + 1, QHeaderView::Stretch);
    }
}

void LogViewerWidget::showStatusStrip(const QString& message)
{
    m_statusStrip->setText(message);
    m_statusStrip->show();
}

void LogViewerWidget::clearSortIndicator()
{
    m_view->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
}

//=== Filtering ===============================================================

void LogViewerWidget::applyFilter(const FilterOptions& options)
{
    m_lastOptions = options;
    if (!m_source || !m_index || !m_parser)
        return;

    m_activeQuery.reset();
    m_scanAfterExtract = false;

    if (options.inQueryMode && !options.query.isEmpty()) {
        QueryError error;
        m_activeQuery = CompiledQuery::compile(
            options.query, m_parser->schema(),
            options.caseSensitivity, {}, &error);
        if (!m_activeQuery) {
            // Keep the previous rows; surface the schema-aware message.
            showStatusStrip(tr("Query error: %1").arg(error.message));
            emit queryError(error.message, int(error.position));
            return;
        }
        m_statusStrip->hide();
        if (ensureColumns(m_activeQuery->referencedColumns(),
                          m_activeQuery->needsSeverity())) {
            m_scanAfterExtract = true;
            return; // scan continues in onExtractFinished
        }
    } else {
        m_statusStrip->hide();
    }
    startScan();
}

void LogViewerWidget::setExtraPredicate(
    std::function<bool(qint64, QByteArrayView)> predicate, bool refilter)
{
    m_extraPredicate = std::move(predicate);
    if (refilter && m_source && m_index)
        applyFilter(m_lastOptions);
}

bool LogViewerWidget::ensureColumns(const QList<int>& columns, bool needsSeverity)
{
    const QList<int> missing = m_columnCache.missing(columns);
    const bool severityMissing = needsSeverity && !m_columnCache.hasSeverity();
    if (missing.isEmpty() && !severityMissing)
        return false;
    m_extractWatcher.cancel();
    m_extractWatcher.setFuture(extractColumns(m_source, m_index, m_parser,
                                              missing, severityMissing));
    return true;
}

void LogViewerWidget::onExtractFinished()
{
    if (m_extractWatcher.future().isCanceled())
        return;
    m_columnCache.insert(m_extractWatcher.future().result());
    if (m_scanAfterExtract) {
        m_scanAfterExtract = false;
        startScan();
    }
    if (m_sortAfterExtract) {
        m_sortAfterExtract = false;
        startSort();
    }
}

void LogViewerWidget::startScan()
{
    m_scanWatcher.cancel();

    LineFilter filter;
    if (m_activeQuery) {
        filter.fieldQuery = m_activeQuery;
        filter.columns = m_columnCache.snapshot(
            m_activeQuery->referencedColumns(), m_activeQuery->needsSeverity());
    } else if (!m_lastOptions.inQueryMode) {
        filter.query = m_lastOptions.query;
        filter.regexMode = m_lastOptions.inRegexMode;
    }
    filter.caseSensitive = m_lastOptions.caseSensitivity == Qt::CaseSensitive;
    filter.invert = m_lastOptions.invertFilter;
    filter.contextBefore = m_lastOptions.contextLinesBefore;
    filter.contextAfter = m_lastOptions.contextLinesAfter;
    filter.extraPredicate = m_extraPredicate;

    m_scanWatcher.setFuture(scanFilter(m_source, m_index, std::move(filter)));
}

void LogViewerWidget::onScanFinished()
{
    if (m_scanWatcher.future().isCanceled())
        return;
    const FilterScanResult result = m_scanWatcher.future().result();

    m_syncing = true;
    m_model->setRowSet(result.rows); // clears any sort order
    m_syncing = false;

    if (m_sortColumn >= 0) {
        // Row set changed under an active sort: re-sort, then restore
        // selection when the order lands.
        startSort();
    } else {
        m_syncing = true;
        restoreSelectionSilently();
        m_syncing = false;
    }

    emit filterApplied(result.matchCount, result.elapsedMs);
}

//=== Sorting =================================================================

void LogViewerWidget::onHeaderClicked(int section)
{
    if (!m_source || !m_index || !m_parser)
        return;

    // Cycle: ascending -> descending -> natural order.
    if (m_sortColumn == section) {
        if (m_sortOrder == Qt::AscendingOrder) {
            m_sortOrder = Qt::DescendingOrder;
        } else {
            m_sortColumn = -1;
            clearSortIndicator();
            m_syncing = true;
            m_model->setRowOrder({});
            restoreSelectionSilently();
            m_syncing = false;
            return;
        }
    } else {
        m_sortColumn = section;
        m_sortOrder = Qt::AscendingOrder;
    }
    m_view->horizontalHeader()->setSortIndicator(m_sortColumn, m_sortOrder);

    if (m_sortColumn == 0) {
        // The No. column: natural or reversed-natural, no keys needed.
        startSort();
        return;
    }

    const int schemaColumn = m_sortColumn - 1;
    const auto schema = m_parser->schema();
    if (schemaColumn >= schema.size())
        return;
    const bool severityKind = schema[schemaColumn].hint == FieldHint::SeverityName;
    if (ensureColumns(severityKind ? QList<int>() : QList<int>{ schemaColumn },
                      severityKind)) {
        m_sortAfterExtract = true;
        return; // sort continues in onExtractFinished
    }
    startSort();
}

void LogViewerWidget::startSort()
{
    m_sortWatcher.cancel();

    if (m_sortColumn == 0) {
        // Natural order needs no scan at all.
        std::vector<qint32> order;
        if (m_sortOrder == Qt::DescendingOrder) {
            order.resize(size_t(m_model->rowSet().size()));
            for (size_t i = 0; i < order.size(); ++i)
                order[i] = qint32(order.size() - 1 - i);
        }
        m_syncing = true;
        m_model->setRowOrder(std::move(order));
        restoreSelectionSilently();
        m_syncing = false;
        return;
    }

    const int schemaColumn = m_sortColumn - 1;
    const auto schema = m_parser->schema();
    SortKeyKind kind = SortKeyKind::Text;
    if (schema[schemaColumn].hint == FieldHint::SeverityName)
        kind = SortKeyKind::Severity;
    else if (schema[schemaColumn].type == FieldType::Integer)
        kind = SortKeyKind::Integer;

    m_sortWatcher.setFuture(sortRows(m_model->rowSet(), kind,
                                     m_columnCache.column(schemaColumn),
                                     m_columnCache.severity()));
}

void LogViewerWidget::onSortFinished()
{
    if (m_sortWatcher.future().isCanceled() || m_sortColumn < 0)
        return;
    SortResult result = m_sortWatcher.future().result();
    if (qint64(result.order.size()) != m_model->rowSet().size())
        return; // row set changed since the sort started; a re-sort is queued

    if (m_sortOrder == Qt::DescendingOrder)
        std::reverse(result.order.begin(), result.order.end());

    m_syncing = true;
    m_model->setRowOrder(std::move(result.order));
    restoreSelectionSilently();
    m_syncing = false;
}

//=== Selection sync ==========================================================

void LogViewerWidget::restoreSelectionSilently()
{
    // Caller holds m_syncing.
    if (m_lastSelection.isEmpty())
        return;
    auto* selection = m_view->selectionModel();
    for (int line : std::as_const(m_lastSelection)) {
        const int row = m_model->rowForSourceLine(line);
        if (row >= 0)
            selection->select(m_model->index(row, 0),
                              QItemSelectionModel::Select
                                  | QItemSelectionModel::Rows);
    }
}

void LogViewerWidget::onSelectionChanged()
{
    if (m_syncing)
        return;
    QList<int> lines;
    const auto rows = m_view->selectionModel()->selectedRows();
    lines.reserve(rows.size());
    for (const QModelIndex& index : rows) {
        const qint64 line = m_model->sourceLineForRow(index.row());
        if (line >= 0)
            lines.append(int(line));
    }
    std::sort(lines.begin(), lines.end());
    m_lastSelection = lines;
    emit linesSelected(lines);
}

void LogViewerWidget::selectSourceLines(const QList<int>& sourceLines)
{
    m_syncing = true;
    auto* selection = m_view->selectionModel();
    selection->clearSelection();
    int firstRow = -1;
    for (int line : sourceLines) {
        const int row = m_model->rowForSourceLine(line);
        if (row < 0)
            continue;
        if (firstRow < 0 || row < firstRow)
            firstRow = row;
        selection->select(m_model->index(row, 0),
                          QItemSelectionModel::Select
                              | QItemSelectionModel::Rows);
    }
    if (firstRow >= 0)
        m_view->scrollTo(m_model->index(firstRow, 0),
                         QAbstractItemView::PositionAtCenter);
    m_lastSelection = sourceLines;
    m_syncing = false;
}
