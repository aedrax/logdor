#include "logviewerwidget.h"

#include <QFontMetrics>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QTableView>
#include <QVBoxLayout>

using namespace logdor;

LogViewerWidget::LogViewerWidget(QWidget* parent)
    : QWidget(parent)
    , m_view(new QTableView(this))
    , m_model(new LogTableModel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSortingEnabled(false);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setShowGrid(false);
    m_view->setAlternatingRowColors(true);
    m_view->verticalHeader()->setVisible(false);
    const QFontMetrics fm(m_view->font());
    m_view->verticalHeader()->setDefaultSectionSize(fm.height() + 4);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &LogViewerWidget::onSelectionChanged);
    connect(&m_scanWatcher, &QFutureWatcherBase::finished,
            this, &LogViewerWidget::onScanFinished);
}

LogViewerWidget::~LogViewerWidget()
{
    m_scanWatcher.cancel();
}

void LogViewerWidget::setCoreSource(std::shared_ptr<FileSource> source,
                                    std::shared_ptr<const LineIndex> index)
{
    // Cancel before swapping so a late result can't resurrect the old file.
    m_scanWatcher.cancel();
    m_source = std::move(source);
    m_index = std::move(index);
    m_lastSelection.clear();
    m_syncing = true;
    m_model->setSource(m_source, m_index, m_parser);
    m_syncing = false;
    if (m_source && m_index && !m_lastOptions.query.isEmpty())
        startScan();
}

void LogViewerWidget::setParser(std::shared_ptr<const FormatParser> parser)
{
    m_parser = std::move(parser);
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

void LogViewerWidget::applyFilter(const FilterOptions& options)
{
    m_lastOptions = options;
    if (!m_source || !m_index)
        return;
    startScan();
}

void LogViewerWidget::setExtraPredicate(
    std::function<bool(qint64, QByteArrayView)> predicate, bool refilter)
{
    m_extraPredicate = std::move(predicate);
    if (refilter && m_source && m_index)
        startScan();
}

void LogViewerWidget::startScan()
{
    m_scanWatcher.cancel();

    LineFilter filter;
    filter.query = m_lastOptions.query;
    filter.caseSensitive = m_lastOptions.caseSensitivity == Qt::CaseSensitive;
    filter.regexMode = m_lastOptions.inRegexMode;
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

    // Swap the row set and restore still-visible selection without echoing.
    m_syncing = true;
    m_model->setRowSet(result.rows);
    if (!m_lastSelection.isEmpty()) {
        auto* selection = m_view->selectionModel();
        for (int line : std::as_const(m_lastSelection)) {
            const int row = m_model->rowForSourceLine(line);
            if (row >= 0)
                selection->select(m_model->index(row, 0),
                                  QItemSelectionModel::Select
                                      | QItemSelectionModel::Rows);
        }
    }
    m_syncing = false;

    emit filterApplied(result.matchCount, result.elapsedMs);
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
        if (firstRow < 0)
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
